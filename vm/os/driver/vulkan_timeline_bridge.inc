/* Experimental guest timeline transport. Included by vulkan_wsi_shim.c.
 * Values are published only after the backing Vulkan semaphore completes.
 * This prototype supports permanent OPAQUE_FD imports into fresh timelines.
 */
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>

typedef int32_t (*TimelineCounterProc)(void *, uint64_t, uint64_t *);
typedef int32_t (*TimelineSignalProc)(void *, const void *);
typedef void (*TimelineDestroyProc)(void *, uint64_t, const void *);
typedef struct {
  uint32_t sType;
  const void *pNext;
  uint64_t semaphore;
  uint64_t value;
} TimelineSignalInfo;
typedef struct {
  uint64_t magic;
  uint64_t value;
} TimelineShared;
typedef struct TimelineEntry {
  void *device;
  uint64_t semaphore;
  int exportable;
  int fd;
  TimelineShared *shared;
  TimelineCounterProc counter;
  TimelineSignalProc signal;
  pthread_t worker;
  int started;
  unsigned stop;
  struct TimelineEntry *next;
} TimelineEntry;

#define TIMELINE_MAGIC UINT64_C(0x53544c54494d4501)
static pthread_mutex_t timeline_lock = PTHREAD_MUTEX_INITIALIZER;
static TimelineEntry *timeline_entries;

static int timeline_bridge_enabled(void) {
  const char *setting = getenv("STEARLIGHT_TIMELINE_BRIDGE");
  return setting && !strcmp(setting, "1");
}

static void *timeline_device_proc(void *device, const char *name) {
  if (!resolve_real_vk_get_device_proc_addr(active_vulkan_instance)) return NULL;
  return real_get_device_proc_addr(device, name);
}

static TimelineEntry *timeline_find(void *device, uint64_t semaphore) {
  for (TimelineEntry *entry = timeline_entries; entry; entry = entry->next)
    if (entry->device == device && entry->semaphore == semaphore) return entry;
  return NULL;
}

static void *timeline_worker(void *opaque) {
  TimelineEntry *entry = opaque;
  const struct timespec interval = {0, 2000000};
  while (!__atomic_load_n(&entry->stop, __ATOMIC_ACQUIRE)) {
    uint64_t completed = 0;
    if (entry->counter(entry->device, entry->semaphore, &completed) != 0) {
      shim_log("timeline bridge: backing counter query failed");
      break;
    }
    uint64_t shared = __atomic_load_n(&entry->shared->value, __ATOMIC_ACQUIRE);
    while (completed > shared &&
           !__atomic_compare_exchange_n(&entry->shared->value, &shared,
              completed, 0, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE)) {}
    shared = __atomic_load_n(&entry->shared->value, __ATOMIC_ACQUIRE);
    if (shared > completed) {
      const TimelineSignalInfo info = {1000207005u, NULL, entry->semaphore, shared};
      if (entry->signal(entry->device, &info) != 0) {
        shim_log("timeline bridge: backing signal failed");
        break;
      }
    }
    nanosleep(&interval, NULL);
  }
  return NULL;
}

static void timeline_release(TimelineEntry *entry) {
  if (entry->started) {
    __atomic_store_n(&entry->stop, 1, __ATOMIC_RELEASE);
    pthread_join(entry->worker, NULL);
    entry->started = 0;
  }
  if (entry->shared) munmap(entry->shared, sizeof(*entry->shared));
  if (entry->fd >= 0) close(entry->fd);
  entry->shared = NULL;
  entry->fd = -1;
}

static int timeline_register(void *device, uint64_t semaphore, int exportable) {
  TimelineEntry *entry = calloc(1, sizeof(*entry));
  if (!entry) return -1;
  entry->device = device;
  entry->semaphore = semaphore;
  entry->exportable = exportable;
  entry->fd = -1;
  entry->counter = (TimelineCounterProc)timeline_device_proc(device, "vkGetSemaphoreCounterValue");
  entry->signal = (TimelineSignalProc)timeline_device_proc(device, "vkSignalSemaphore");
  if (!entry->counter)
    entry->counter = (TimelineCounterProc)timeline_device_proc(device, "vkGetSemaphoreCounterValueKHR");
  if (!entry->signal)
    entry->signal = (TimelineSignalProc)timeline_device_proc(device, "vkSignalSemaphoreKHR");
  if (!entry->counter || !entry->signal) {
    shim_log("timeline bridge: neither core nor KHR timeline entry points are available");
    free(entry);
    return -8;
  }
  pthread_mutex_lock(&timeline_lock);
  entry->next = timeline_entries;
  timeline_entries = entry;
  pthread_mutex_unlock(&timeline_lock);
  return 0;
}

static int timeline_start(TimelineEntry *entry) {
  __atomic_store_n(&entry->stop, 0, __ATOMIC_RELEASE);
  if (pthread_create(&entry->worker, NULL, timeline_worker, entry)) return -1;
  entry->started = 1;
  return 0;
}

static int timeline_export(void *device, const VkSemaphoreGetFdInfoCompat *info, int *out_fd) {
  if (info->handleType != 1u) return -1000072003;
  pthread_mutex_lock(&timeline_lock);
  TimelineEntry *entry = timeline_find(device, info->semaphore);
  int result = -1000072003;
  if (!entry || !entry->exportable) goto done;
  if (!entry->shared) {
    uint64_t completed;
    if (entry->counter(device, info->semaphore, &completed) != 0) goto done;
    entry->fd = memfd_create("stearlight-timeline", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (entry->fd < 0 || ftruncate(entry->fd, sizeof(TimelineShared))) goto fail;
    entry->shared = mmap(NULL, sizeof(TimelineShared), PROT_READ | PROT_WRITE,
                         MAP_SHARED, entry->fd, 0);
    if (entry->shared == MAP_FAILED) { entry->shared = NULL; goto fail; }
    entry->shared->magic = TIMELINE_MAGIC;
    __atomic_store_n(&entry->shared->value, completed, __ATOMIC_RELEASE);
    if (fcntl(entry->fd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_SEAL) < 0)
      goto fail;
    if (timeline_start(entry)) goto fail;
    shim_log("timeline bridge: exported a real timeline payload");
  }
  *out_fd = fcntl(entry->fd, F_DUPFD_CLOEXEC, 0);
  result = *out_fd < 0 ? -1 : 0;
  goto done;
fail:
  timeline_release(entry);
  result = -1;
done:
  pthread_mutex_unlock(&timeline_lock);
  return result;
}

static int timeline_import(void *device, const VkImportSemaphoreFdInfoCompat *info) {
  if (info->handleType != 1u || info->flags || info->fd < 0) return -1000072003;
  struct stat status;
  if (fstat(info->fd, &status) || status.st_size != sizeof(TimelineShared)) return -1000072003;
  TimelineShared *shared = mmap(NULL, sizeof(*shared), PROT_READ | PROT_WRITE,
                                MAP_SHARED, info->fd, 0);
  if (shared == MAP_FAILED) return -1000072003;
  if (shared->magic != TIMELINE_MAGIC) {
    munmap(shared, sizeof(*shared));
    return -1000072003;
  }
  pthread_mutex_lock(&timeline_lock);
  TimelineEntry *entry = timeline_find(device, info->semaphore);
  int result = -1000072003;
  uint64_t local;
  /* Existing payload replacement needs handle remapping. Fail explicitly
   * until that path exists, rather than carrying the old counter forward. */
  if (!entry || entry->shared || entry->counter(device, info->semaphore, &local) || local)
    goto done;
  int owned_fd = fcntl(info->fd, F_DUPFD_CLOEXEC, 0);
  if (owned_fd < 0) { result = -1; goto done; }
  entry->fd = owned_fd;
  entry->shared = shared;
  if (timeline_start(entry)) {
    entry->shared = NULL;
    close(owned_fd);
    entry->fd = -1;
    result = -1;
    goto done;
  }
  shared = NULL;
  close(info->fd); /* A successful OPAQUE_FD import consumes its descriptor. */
  shim_log("timeline bridge: imported a real timeline payload");
  result = 0;
done:
  pthread_mutex_unlock(&timeline_lock);
  if (shared) munmap(shared, sizeof(*shared));
  return result;
}

static void timeline_remove(void *device, uint64_t semaphore, int all) {
  pthread_mutex_lock(&timeline_lock);
  TimelineEntry **link = &timeline_entries;
  while (*link) {
    TimelineEntry *entry = *link;
    if (entry->device == device && (all || entry->semaphore == semaphore)) {
      *link = entry->next;
      timeline_release(entry);
      free(entry);
    } else link = &entry->next;
  }
  pthread_mutex_unlock(&timeline_lock);
}

static void timeline_destroy_semaphore(void *device, uint64_t semaphore, const void *allocator) {
  TimelineDestroyProc destroy = (TimelineDestroyProc)timeline_device_proc(device, "vkDestroySemaphore");
  timeline_remove(device, semaphore, 0);
  if (destroy) destroy(device, semaphore, allocator);
}

static void timeline_destroy_device(void *device, const void *allocator) {
  typedef void (*DestroyDeviceProc)(void *, const void *);
  DestroyDeviceProc destroy = (DestroyDeviceProc)timeline_device_proc(device, "vkDestroyDevice");
  timeline_remove(device, 0, 1);
  if (destroy) destroy(device, allocator);
}

static void timeline_external_properties(void *physical_device,
                                                     const void *opaque_info,
                                                     void *opaque_properties) {
  typedef void (*PropertiesProc)(void *, const void *, void *);
  PropertiesProc query = (PropertiesProc)real_get_instance_proc_addr(
      active_vulkan_instance, "vkGetPhysicalDeviceExternalSemaphoreProperties");
  if (query) query(physical_device, opaque_info, opaque_properties);
  typedef struct { uint32_t sType; const void *pNext; uint32_t handleType; } Info;
  typedef struct { uint32_t sType; void *pNext; uint32_t from, compatible, features; } Properties;
  const Info *info = opaque_info;
  const VkBaseInStructureCompat *next = info->pNext;
  while (next && next->sType != 1000207002u) next = next->pNext;
  if (timeline_bridge_enabled() && info->handleType == 1u && next &&
      ((const VkSemaphoreTypeCreateInfoCompat *)next)->semaphoreType == 1u) {
    Properties *properties = opaque_properties;
    properties->from = properties->compatible = 1u;
    properties->features = 3u;
  }
}
