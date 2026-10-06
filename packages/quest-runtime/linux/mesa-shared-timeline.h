// SPDX-License-Identifier: MIT
// KGSL has no exportable timeline semaphore on Quest 2. This payload is
// shared across Linux processes; queue signals are published only after the
// corresponding KGSL GPU submission has completed. Mesa's submit worker
// handles wait-before-signal without blocking the application's submit call.
#include <linux/futex.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <vector>

struct quest_timeline_payload {
   uint64_t magic;
   uint64_t value;
   uint32_t epoch;
};

struct quest_timeline_sync {
   struct vk_sync vk;
   int fd;
   struct quest_timeline_payload *payload;
};

static VkResult
quest_timeline_init(struct vk_device *device, struct vk_sync *sync, uint64_t value)
{
   auto *s = container_of(sync, struct quest_timeline_sync, vk);
   s->fd = memfd_create("stearlight-vulkan-timeline", MFD_CLOEXEC);
   s->payload = nullptr;
   if (s->fd < 0)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
   if (ftruncate(s->fd, 4096)) {
      close(s->fd);
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
   }
   void *map = mmap(nullptr, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, s->fd, 0);
   if (map == MAP_FAILED) {
      close(s->fd);
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
   }
   s->payload = static_cast<quest_timeline_payload *>(map);
   s->payload->magic = UINT64_C(0x5351544c00000001);
   __atomic_store_n(&s->payload->value, value, __ATOMIC_RELEASE);
   return VK_SUCCESS;
}

static void
quest_timeline_finish(struct vk_device *, struct vk_sync *sync)
{
   auto *s = container_of(sync, struct quest_timeline_sync, vk);
   munmap(s->payload, 4096);
   close(s->fd);
}

static VkResult
quest_timeline_signal(struct vk_device *, struct vk_sync *sync, uint64_t value)
{
   auto *s = container_of(sync, struct quest_timeline_sync, vk);
   uint64_t previous = __atomic_load_n(&s->payload->value, __ATOMIC_ACQUIRE);
   while (previous < value &&
          !__atomic_compare_exchange_n(&s->payload->value, &previous, value,
                                       false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {}
   __atomic_add_fetch(&s->payload->epoch, 1, __ATOMIC_RELEASE);
   syscall(SYS_futex, &s->payload->epoch, FUTEX_WAKE, INT_MAX, nullptr, nullptr, 0);
   return VK_SUCCESS;
}

static VkResult
quest_timeline_get_value(struct vk_device *, struct vk_sync *sync, uint64_t *value)
{
   auto *s = container_of(sync, struct quest_timeline_sync, vk);
   *value = __atomic_load_n(&s->payload->value, __ATOMIC_ACQUIRE);
   return VK_SUCCESS;
}

static VkResult
quest_timeline_wait(struct vk_device *device, struct vk_sync *sync,
                    uint64_t value, enum vk_sync_wait_flags, uint64_t deadline)
{
   auto *s = container_of(sync, struct quest_timeline_sync, vk);
   for (;;) {
      uint32_t epoch = __atomic_load_n(&s->payload->epoch, __ATOMIC_ACQUIRE);
      if (__atomic_load_n(&s->payload->value, __ATOMIC_ACQUIRE) >= value)
         return VK_SUCCESS;
      if (vk_device_is_lost(device))
         return VK_ERROR_DEVICE_LOST;
      uint64_t now = os_time_get_nano();
      if (deadline != UINT64_MAX && now >= deadline)
         return VK_TIMEOUT;
      // Periodically check device loss even for an infinite Vulkan wait.
      uint64_t remaining = deadline == UINT64_MAX ? 50000000 :
                            MIN2(deadline - now, UINT64_C(50000000));
      struct timespec timeout;
      timespec_from_nsec(&timeout, remaining);
      int ret = syscall(SYS_futex, &s->payload->epoch, FUTEX_WAIT, epoch,
                        &timeout, nullptr, 0);
      if (ret < 0 && errno != EAGAIN && errno != EINTR && errno != ETIMEDOUT)
         return vk_error(device, VK_ERROR_UNKNOWN);
   }
}

static VkResult
quest_timeline_import(struct vk_device *device, struct vk_sync *sync, int fd)
{
   struct stat status;
   if (fstat(fd, &status) || !S_ISREG(status.st_mode) || status.st_size != 4096)
      return vk_error(device, VK_ERROR_INVALID_EXTERNAL_HANDLE);
   int owned = os_dupfd_cloexec(fd);
   if (owned < 0)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
   void *map = mmap(nullptr, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, owned, 0);
   if (map == MAP_FAILED) {
      close(owned);
      return vk_error(device, VK_ERROR_INVALID_EXTERNAL_HANDLE);
   }
   auto *payload = static_cast<quest_timeline_payload *>(map);
   if (payload->magic != UINT64_C(0x5351544c00000001)) {
      munmap(map, 4096);
      close(owned);
      return vk_error(device, VK_ERROR_INVALID_EXTERNAL_HANDLE);
   }
   auto *s = container_of(sync, struct quest_timeline_sync, vk);
   munmap(s->payload, 4096);
   close(s->fd);
   s->payload = payload;
   s->fd = owned;
   return VK_SUCCESS;
}

static VkResult
quest_timeline_export(struct vk_device *device, struct vk_sync *sync, int *fd)
{
   auto *s = container_of(sync, struct quest_timeline_sync, vk);
   *fd = os_dupfd_cloexec(s->fd);
   return *fd < 0 ? vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY) : VK_SUCCESS;
}

static const struct vk_sync_type quest_timeline_type = {
   .size = sizeof(struct quest_timeline_sync),
   .features = static_cast<vk_sync_features>(VK_SYNC_FEATURE_TIMELINE |
                 VK_SYNC_FEATURE_GPU_WAIT | VK_SYNC_FEATURE_CPU_WAIT |
                 VK_SYNC_FEATURE_CPU_SIGNAL | VK_SYNC_FEATURE_WAIT_PENDING),
   .init = quest_timeline_init,
   .finish = quest_timeline_finish,
   .signal = quest_timeline_signal,
   .get_value = quest_timeline_get_value,
   .wait = quest_timeline_wait,
   .import_opaque_fd = quest_timeline_import,
   .export_opaque_fd = quest_timeline_export,
};

static VkResult
quest_kgsl_queue_submit(struct tu_queue *queue, void *submit,
                        struct vk_sync_wait *waits, uint32_t wait_count,
                        struct vk_sync_signal *signals, uint32_t signal_count,
                        struct tu_u_trace_submission_data *trace)
{
   std::vector<struct vk_sync_wait> native_waits;
   std::vector<struct vk_sync_signal> native_signals;
   bool shared_signal = false;
   for (uint32_t i = 0; i < wait_count; i++) {
      if (waits[i].sync->type == &quest_timeline_type) {
         // WAIT_PENDING in Mesa's worker already waited for completion.
         uint64_t value;
         quest_timeline_get_value(&queue->device->vk, waits[i].sync, &value);
         if (value < waits[i].wait_value)
            return vk_device_set_lost(&queue->device->vk, "Timeline wait was not resolved");
      } else {
         native_waits.push_back(waits[i]);
      }
   }
   for (uint32_t i = 0; i < signal_count; i++) {
      if (signals[i].sync->type == &quest_timeline_type)
         shared_signal = true;
      else
         native_signals.push_back(signals[i]);
   }
   struct vk_kgsl_syncobj completion = {};
   if (shared_signal) {
      kgsl_syncobj_init(&completion.syncobj, false);
      native_signals.push_back({&completion.vk, ~(VkPipelineStageFlags2)0, 0});
   }
   VkResult result = kgsl_queue_submit(queue, submit, native_waits.data(),
                                       native_waits.size(), native_signals.data(),
                                       native_signals.size(), trace);
   if (shared_signal) {
      if (result == VK_SUCCESS)
         result = kgsl_syncobj_wait(queue->device, &completion.syncobj, UINT64_MAX);
      kgsl_syncobj_destroy(&completion.syncobj);
      if (result == VK_SUCCESS) {
         for (uint32_t i = 0; i < signal_count; i++) {
            if (signals[i].sync->type == &quest_timeline_type)
               quest_timeline_signal(&queue->device->vk, signals[i].sync,
                                      signals[i].signal_value);
         }
      }
   }
   return result;
}
