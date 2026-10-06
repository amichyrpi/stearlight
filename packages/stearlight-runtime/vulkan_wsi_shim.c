#define _GNU_SOURCE

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <unistd.h>

/* Match the fake HMD's shared stereo mirror, not the app overlay size. */
static int vm_low_resolution(void) {
  const char *value = getenv("STEARLIGHT_VM_LOW_RESOLUTION");
  return value && strcmp(value, "1") == 0;
}
#define DISPLAY_WIDTH (vm_low_resolution() ? 1440u : 2880u)
#define DISPLAY_HEIGHT (vm_low_resolution() ? 800u : 1600u)

typedef void *(*PFN_vkGetInstanceProcAddr)(void *instance, const char *name);
typedef void *(*PFN_vkGetDeviceProcAddr)(void *device, const char *name);
typedef void *(*PFN_dlsym)(void *handle, const char *name);
typedef int32_t (*PFN_vkEnumerateInstanceExtensionProperties)(
    const char *layer_name, uint32_t *count, void *properties);
typedef int32_t (*PFN_vkEnumerateDeviceExtensionProperties)(
    void *physical_device, const char *layer_name, uint32_t *count,
    void *properties);
typedef void (*PFN_vkGetPhysicalDeviceFeatures2)(void *physical_device,
                                                 void *features);
typedef int32_t (*PFN_vkWaitForPresentKHR)(void *device, uint64_t swapchain,
                                          uint64_t present_id,
                                          uint64_t timeout);
typedef int32_t (*PFN_vkQueuePresentKHR)(void *queue,
                                        const void *present_info);
typedef int32_t (*PFN_vkQueueSubmit)(void *queue, uint32_t submit_count,
                                     const void *submits, void *fence);
typedef int32_t (*PFN_vkQueueSubmit2)(void *queue, uint32_t submit_count,
                                      const void *submits, void *fence);
typedef int32_t (*PFN_vkQueueBindSparse)(void *queue, uint32_t bind_info_count,
                                         const void *bind_infos, void *fence);
typedef int32_t (*PFN_vkQueueWaitIdle)(void *queue);
typedef int32_t (*PFN_vkDeviceWaitIdle)(void *device);
typedef int32_t (*PFN_vkGetSemaphoreFdKHR)(void *device,
                                           const void *get_info, int *fd);
typedef int32_t (*PFN_vkImportSemaphoreFdKHR)(void *device,
                                              const void *import_info);
typedef int32_t (*PFN_vkCreateInstance)(const void *create_info,
                                        const void *allocator,
                                        void **instance);
typedef int32_t (*PFN_vkCreateDevice)(void *physical_device,
                                      const void *create_info,
                                      const void *allocator, void **device);
typedef void (*PFN_vkGetPhysicalDeviceQueueFamilyProperties)(
    void *physical_device, uint32_t *count, void *properties);
typedef void (*PFN_vkGetPhysicalDeviceQueueFamilyProperties2)(
    void *physical_device, uint32_t *count, void *properties);
typedef void (*PFN_vkGetDeviceQueue)(void *device, uint32_t family_index,
                                     uint32_t queue_index, void **queue);
typedef void (*PFN_vkGetDeviceQueue2)(void *device, const void *queue_info,
                                      void **queue);
typedef int32_t (*PFN_vkCreateCommandPool)(void *device,
                                           const void *create_info,
                                           const void *allocator,
                                           void **command_pool);
typedef int32_t (*PFN_vkCreateSemaphore)(void *device,
                                         const void *create_info,
                                         const void *allocator,
                                         void **semaphore);
typedef int32_t (*PFN_vkCreateXlibSurfaceKHR)(void *instance,
                                               const void *create_info,
                                               const void *allocator,
                                               void **surface);
typedef int32_t (*PFN_vkCreateXcbSurfaceKHR)(void *instance,
                                              const void *create_info,
                                              const void *allocator,
                                              void **surface);

typedef struct XDisplay XDisplay;
typedef struct xcb_connection_t xcb_connection_t;
typedef unsigned long XWindow;
typedef xcb_connection_t *(*PFN_XGetXCBConnection)(XDisplay *display);
typedef XDisplay *(*PFN_XOpenDisplay)(const char *name);
typedef XWindow (*PFN_XDefaultRootWindow)(XDisplay *display);
typedef XWindow (*PFN_XCreateSimpleWindow)(XDisplay *display, XWindow parent,
                                           int x, int y, unsigned int width,
                                           unsigned int height,
                                           unsigned int border_width,
                                           unsigned long border,
                                           unsigned long background);
typedef int (*PFN_XMapWindow)(XDisplay *display, XWindow window);
typedef int (*PFN_XStoreName)(XDisplay *display, XWindow window,
                              const char *name);
typedef int (*PFN_XFlush)(XDisplay *display);

typedef struct {
  uint32_t width;
  uint32_t height;
} VkExtent2DCompat;

typedef struct {
  uint32_t width;
  uint32_t height;
  uint32_t depth;
} VkExtent3DCompat;

typedef struct {
  uint32_t queueFlags;
  uint32_t queueCount;
  uint32_t timestampValidBits;
  VkExtent3DCompat minImageTransferGranularity;
} VkQueueFamilyPropertiesCompat;

typedef struct {
  uint32_t sType;
  void *pNext;
  VkQueueFamilyPropertiesCompat queueFamilyProperties;
} VkQueueFamilyProperties2Compat;

typedef struct {
  char extensionName[256];
  uint32_t specVersion;
} VkExtensionPropertiesCompat;

typedef struct {
  int32_t x;
  int32_t y;
} VkOffset2DCompat;

typedef struct {
  void *display;
  const char *displayName;
  VkExtent2DCompat physicalDimensions;
  VkExtent2DCompat physicalResolution;
  uint32_t supportedTransforms;
  uint32_t planeReorderPossible;
  uint32_t persistentContent;
} VkDisplayPropertiesCompat;

typedef struct {
  void *currentDisplay;
  uint32_t currentStackIndex;
} VkDisplayPlanePropertiesCompat;

typedef struct {
  VkExtent2DCompat visibleRegion;
  uint32_t refreshRate;
} VkDisplayModeParametersCompat;

typedef struct {
  void *displayMode;
  VkDisplayModeParametersCompat parameters;
} VkDisplayModePropertiesCompat;

typedef struct {
  uint32_t supportedAlpha;
  VkOffset2DCompat minSrcPosition;
  VkOffset2DCompat maxSrcPosition;
  VkExtent2DCompat minSrcExtent;
  VkExtent2DCompat maxSrcExtent;
  VkOffset2DCompat minDstPosition;
  VkOffset2DCompat maxDstPosition;
  VkExtent2DCompat minDstExtent;
  VkExtent2DCompat maxDstExtent;
} VkDisplayPlaneCapabilitiesCompat;

typedef struct {
  uint32_t sType;
  void *pNext;
  VkDisplayPropertiesCompat displayProperties;
} VkDisplayProperties2Compat;

typedef struct {
  uint32_t sType;
  void *pNext;
  VkDisplayPlanePropertiesCompat displayPlaneProperties;
} VkDisplayPlaneProperties2Compat;

typedef struct {
  uint32_t sType;
  void *pNext;
  VkDisplayModePropertiesCompat displayModeProperties;
} VkDisplayModeProperties2Compat;

typedef struct {
  uint32_t sType;
  void *pNext;
  void *displayMode;
  uint32_t planeIndex;
} VkDisplayPlaneInfo2Compat;

typedef struct {
  uint32_t sType;
  void *pNext;
  VkDisplayPlaneCapabilitiesCompat capabilities;
} VkDisplayPlaneCapabilities2Compat;

typedef struct {
  uint32_t sType;
  const void *pNext;
  uint32_t flags;
  const void *pApplicationInfo;
  uint32_t enabledLayerCount;
  const char *const *ppEnabledLayerNames;
  uint32_t enabledExtensionCount;
  const char *const *ppEnabledExtensionNames;
} VkInstanceCreateInfoCompat;

typedef struct {
  uint32_t sType;
  const void *pNext;
  uint32_t flags;
  uint32_t queueCreateInfoCount;
  const void *pQueueCreateInfos;
  uint32_t enabledLayerCount;
  const char *const *ppEnabledLayerNames;
  uint32_t enabledExtensionCount;
  const char *const *ppEnabledExtensionNames;
  const void *pEnabledFeatures;
} VkDeviceCreateInfoCompat;

typedef struct {
  uint32_t sType;
  const void *pNext;
  uint32_t flags;
  uint32_t queueFamilyIndex;
  uint32_t queueCount;
  const float *pQueuePriorities;
} VkDeviceQueueCreateInfoCompat;

typedef struct {
  uint32_t sType;
  const void *pNext;
  uint32_t flags;
  uint32_t queueFamilyIndex;
} VkCommandPoolCreateInfoCompat;

typedef struct {
  uint32_t sType;
  const void *pNext;
  uint32_t flags;
  uint32_t queueFamilyIndex;
  uint32_t queueIndex;
} VkDeviceQueueInfo2Compat;

typedef struct {
  uint32_t sType;
  const void *pNext;
} VkBaseInStructureCompat;

typedef struct {
  uint32_t sType;
  void *pNext;
} VkBaseOutStructureCompat;

typedef struct {
  uint32_t sType;
  void *pNext;
  uint32_t presentWait;
} VkPresentWaitFeaturesCompat;

typedef struct {
  uint32_t sType;
  void *pNext;
  uint32_t presentId;
} VkPresentIdFeaturesCompat;

typedef struct {
  uint32_t sType;
  void *pNext;
  uint32_t timelineSemaphore;
} VkTimelineSemaphoreFeaturesCompat;

typedef struct {
  uint32_t sType;
  void *pNext;
  uint32_t features_before_timeline[23];
  uint32_t timelineSemaphore;
} VkVulkan12FeaturesCompat;

typedef struct {
  uint32_t *feature;
  uint32_t original_value;
} VkTimelineFeatureRestoreCompat;

typedef struct {
  uint32_t sType;
  const void *pNext;
  uint32_t waitSemaphoreCount;
  const uint64_t *pWaitSemaphores;
  uint32_t swapchainCount;
  const uint64_t *pSwapchains;
  const uint32_t *pImageIndices;
  int32_t *pResults;
} VkPresentInfoCompat;

#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR_COMPAT \
  1000248000u
#define VK_STRUCTURE_TYPE_PRESENT_ID_KHR_COMPAT 1000294000u
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR_COMPAT \
  1000294001u
#define VK_STRUCTURE_TYPE_DEVICE_QUEUE_GLOBAL_PRIORITY_CREATE_INFO_EXT_COMPAT \
  1000174000u
#define VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2_COMPAT 1000059005u
#define VK_QUEUE_GRAPHICS_BIT_COMPAT 0x00000001u
#define VK_QUEUE_COMPUTE_BIT_COMPAT 0x00000002u
#define VK_QUEUE_TRANSFER_BIT_COMPAT 0x00000004u
#define VK_STRUCTURE_TYPE_DEVICE_QUEUE_INFO_2_COMPAT 1000145003u
#define MAX_PNEXT_RESTORE 64u

typedef struct {
  uint32_t sType;
  const void *pNext;
  uint32_t flags;
  XDisplay *dpy;
  XWindow window;
} VkXlibSurfaceCreateInfoCompat;

typedef struct {
  uint32_t sType;
  const void *pNext;
  uint32_t flags;
  xcb_connection_t *connection;
  uint32_t window;
} VkXcbSurfaceCreateInfoCompat;

typedef struct {
  uint32_t sType;
  const void *pNext;
  uint64_t semaphore;
  uint32_t handleType;
} VkSemaphoreGetFdInfoCompat;

typedef struct {
  uint32_t sType;
  const void *pNext;
  uint32_t flags;
} VkSemaphoreCreateInfoCompat;

typedef struct {
  uint32_t sType;
  const void *pNext;
  uint32_t handleTypes;
} VkExportSemaphoreCreateInfoCompat;

typedef struct {
  uint32_t sType;
  const void *pNext;
  uint32_t semaphoreType;
  uint64_t initialValue;
} VkSemaphoreTypeCreateInfoCompat;

typedef struct {
  uint32_t sType;
  const void *pNext;
  uint64_t semaphore;
  uint32_t flags;
  uint32_t handleType;
  int fd;
} VkImportSemaphoreFdInfoCompat;

static PFN_vkGetInstanceProcAddr real_get_instance_proc_addr;
static PFN_vkGetDeviceProcAddr real_get_device_proc_addr;
static PFN_dlsym real_dlsym;
static PFN_vkEnumerateInstanceExtensionProperties real_enumerate_instance_extensions;
static PFN_vkEnumerateDeviceExtensionProperties real_enumerate_device_extensions;
static PFN_vkGetPhysicalDeviceFeatures2 real_get_physical_device_features2;
static PFN_vkQueuePresentKHR real_queue_present;
static PFN_vkCreateInstance real_create_instance;
static PFN_vkCreateDevice real_create_device;
static PFN_vkGetPhysicalDeviceQueueFamilyProperties
    real_get_queue_family_properties;
static PFN_vkGetPhysicalDeviceQueueFamilyProperties2
    real_get_queue_family_properties2;
static PFN_vkGetDeviceQueue real_get_device_queue;
static PFN_vkGetDeviceQueue2 real_get_device_queue2;
static PFN_vkCreateCommandPool real_create_command_pool;
static int synthetic_present_id_available;
static int synthetic_present_wait_available;
static int synthetic_present_wait_logged;
static int synthetic_global_priority_available;
static int synthetic_global_priority_logged;
static int device_extensions_reported;
static int timeline_extension_hidden_logged;
static int timeline_feature_mask_logged;
static int layer_device_extensions_reported;
static void *active_vulkan_instance;
static void *active_vulkan_device;
static void *vulkan_loader_handle;
static void *queue_alias_physical_device;
static VkQueueFamilyPropertiesCompat queue_alias_backing_properties;
static uint32_t queue_alias_backing_family = UINT32_MAX;
static uint32_t queue_alias_family = UINT32_MAX;
static int queue_alias_checked;
static int queue_alias_available;
static volatile unsigned int queue_alias_operations_lock;
static int queue_alias_serialization_logged;
static int queue_property_query_logged;
static int queue_handle_query_logged;
static XDisplay *x_display;
static XWindow x_window;
static void *x11_xcb_handle;

static const char fake_display_name[] = "Stearlight Gamescope Display";
static void *const fake_display = (void *)(uintptr_t)0x5a1;
static void *const fake_display_mode = (void *)(uintptr_t)0x5a2;

void *vkGetInstanceProcAddr(void *instance, const char *name);
void *vkGetDeviceProcAddr(void *device, const char *name);
int32_t vkEnumerateInstanceExtensionProperties(const char *layer_name,
                                               uint32_t *count,
                                               void *properties);
int32_t vkEnumerateDeviceExtensionProperties(void *physical_device,
                                             const char *layer_name,
                                             uint32_t *count,
                                             void *properties);
void vkGetPhysicalDeviceFeatures2(void *physical_device, void *features);
void vkGetPhysicalDeviceFeatures2KHR(void *physical_device, void *features);
int32_t vkWaitForPresentKHR(void *device, uint64_t swapchain,
                            uint64_t present_id, uint64_t timeout);
int32_t vkQueuePresentKHR(void *queue, const void *present_info);
int32_t vkQueueSubmit(void *queue, uint32_t submit_count,
                      const void *submits, void *fence);
int32_t vkQueueSubmit2(void *queue, uint32_t submit_count,
                       const void *submits, void *fence);
int32_t vkQueueSubmit2KHR(void *queue, uint32_t submit_count,
                          const void *submits, void *fence);
int32_t vkQueueBindSparse(void *queue, uint32_t bind_info_count,
                          const void *bind_infos, void *fence);
int32_t vkQueueWaitIdle(void *queue);
int32_t vkDeviceWaitIdle(void *device);
int32_t vkGetSemaphoreFdKHR(void *device, const void *get_info, int *fd);
int32_t vkImportSemaphoreFdKHR(void *device, const void *import_info);
static void *wrap_semaphore_fd_api(const char *name, void *proc);
int32_t vkCreateSemaphore(void *device, const void *create_info,
                          const void *allocator, void **semaphore);
static void *wrap_semaphore_create_api(const char *name, void *proc);
static int timeline_bridge_enabled(void);

static int device_extension_hidden(const char *name) {
  return !timeline_bridge_enabled() && !strcmp(name, "VK_KHR_timeline_semaphore");
}
static int lock_aliased_queue_operations(void);
static void unlock_aliased_queue_operations(int locked);
int32_t vkCreateInstance(const void *create_info, const void *allocator,
                         void **instance);
int32_t vkCreateXlibSurfaceKHR(void *instance, const void *create_info,
                               const void *allocator, void **surface);
int32_t vkCreateDevice(void *physical_device, const void *create_info,
                       const void *allocator, void **device);
void vkGetPhysicalDeviceQueueFamilyProperties(void *physical_device,
                                              uint32_t *count,
                                              void *properties);
void vkGetPhysicalDeviceQueueFamilyProperties2(void *physical_device,
                                               uint32_t *count,
                                               void *properties);
void vkGetPhysicalDeviceQueueFamilyProperties2KHR(void *physical_device,
                                                  uint32_t *count,
                                                  void *properties);
void vkGetDeviceQueue(void *device, uint32_t family_index,
                      uint32_t queue_index, void **queue);
void vkGetDeviceQueue2(void *device, const void *queue_info, void **queue);
int32_t vkCreateCommandPool(void *device, const void *create_info,
                            const void *allocator, void **command_pool);
static void shim_log(const char *message);

static void resolve_vulkan_loader(void) {
  if (!vulkan_loader_handle) {
    vulkan_loader_handle =
        dlopen("libvulkan.so.1", RTLD_NOW | RTLD_NOLOAD);
    if (!vulkan_loader_handle)
      vulkan_loader_handle = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
  }
  if (!vulkan_loader_handle || !real_dlsym) return;

  PFN_vkGetInstanceProcAddr gipa =
      (PFN_vkGetInstanceProcAddr)real_dlsym(
          vulkan_loader_handle, "vkGetInstanceProcAddr");
  if (gipa && gipa != &vkGetInstanceProcAddr) real_get_instance_proc_addr = gipa;

  PFN_vkCreateInstance create_instance =
      (PFN_vkCreateInstance)real_dlsym(vulkan_loader_handle,
                                      "vkCreateInstance");
  if (create_instance && create_instance != &vkCreateInstance)
    real_create_instance = create_instance;

  PFN_vkEnumerateInstanceExtensionProperties enumerate_extensions =
      (PFN_vkEnumerateInstanceExtensionProperties)real_dlsym(
          vulkan_loader_handle, "vkEnumerateInstanceExtensionProperties");
  if (enumerate_extensions &&
      enumerate_extensions != &vkEnumerateInstanceExtensionProperties)
    real_enumerate_instance_extensions = enumerate_extensions;

  PFN_vkEnumerateDeviceExtensionProperties enumerate_device_extensions =
      (PFN_vkEnumerateDeviceExtensionProperties)real_dlsym(
          vulkan_loader_handle, "vkEnumerateDeviceExtensionProperties");
  if (enumerate_device_extensions &&
      enumerate_device_extensions != &vkEnumerateDeviceExtensionProperties)
    real_enumerate_device_extensions = enumerate_device_extensions;

  PFN_vkGetPhysicalDeviceFeatures2 get_features2 =
      (PFN_vkGetPhysicalDeviceFeatures2)real_dlsym(
          vulkan_loader_handle, "vkGetPhysicalDeviceFeatures2");
  if (get_features2 && get_features2 != &vkGetPhysicalDeviceFeatures2)
    real_get_physical_device_features2 = get_features2;

  PFN_vkGetDeviceProcAddr get_device_proc_addr =
      (PFN_vkGetDeviceProcAddr)real_dlsym(
          vulkan_loader_handle, "vkGetDeviceProcAddr");
  if (get_device_proc_addr != &vkGetDeviceProcAddr)
    real_get_device_proc_addr = get_device_proc_addr;

  char message[128];
  snprintf(message, sizeof(message),
           "Vulkan loader resolved gipa=%s create_instance=%s enumerate_extensions=%s",
           real_get_instance_proc_addr ? "yes" : "no",
           real_create_instance ? "yes" : "no",
           real_enumerate_instance_extensions ? "yes" : "no");
  shim_log(message);
}

static PFN_vkGetDeviceProcAddr resolve_real_vk_get_device_proc_addr(
    void *instance) {
  if (real_get_device_proc_addr == &vkGetDeviceProcAddr)
    real_get_device_proc_addr = NULL;
  if (!real_get_device_proc_addr && !vulkan_loader_handle)
    resolve_vulkan_loader();
  if (!real_get_device_proc_addr && instance && real_get_instance_proc_addr) {
    PFN_vkGetDeviceProcAddr proc =
        (PFN_vkGetDeviceProcAddr)real_get_instance_proc_addr(
            instance, "vkGetDeviceProcAddr");
    if (proc != &vkGetDeviceProcAddr) real_get_device_proc_addr = proc;
  }
  if (!real_get_device_proc_addr && vulkan_loader_handle && real_dlsym) {
    PFN_vkGetDeviceProcAddr proc =
        (PFN_vkGetDeviceProcAddr)real_dlsym(
            vulkan_loader_handle, "vkGetDeviceProcAddr");
    if (proc != &vkGetDeviceProcAddr) real_get_device_proc_addr = proc;
  }
  return real_get_device_proc_addr;
}

static PFN_vkGetDeviceQueue resolve_real_vk_get_device_queue(void *device) {
  if (real_get_device_queue == &vkGetDeviceQueue) real_get_device_queue = NULL;
  if (!real_get_device_queue && !vulkan_loader_handle)
    resolve_vulkan_loader();
  if (!real_get_device_queue && vulkan_loader_handle && real_dlsym) {
    PFN_vkGetDeviceQueue proc = (PFN_vkGetDeviceQueue)real_dlsym(
        vulkan_loader_handle, "vkGetDeviceQueue");
    if (proc != &vkGetDeviceQueue) real_get_device_queue = proc;
  }
  if (!real_get_device_queue && device &&
      resolve_real_vk_get_device_proc_addr(active_vulkan_instance)) {
    PFN_vkGetDeviceQueue proc = (PFN_vkGetDeviceQueue)
        real_get_device_proc_addr(device, "vkGetDeviceQueue");
    if (proc != &vkGetDeviceQueue) real_get_device_queue = proc;
  }
  return real_get_device_queue;
}

static PFN_vkGetDeviceQueue2 resolve_real_vk_get_device_queue2(void *device) {
  if (real_get_device_queue2 == &vkGetDeviceQueue2)
    real_get_device_queue2 = NULL;
  if (!real_get_device_queue2 && !vulkan_loader_handle)
    resolve_vulkan_loader();
  if (!real_get_device_queue2 && vulkan_loader_handle && real_dlsym) {
    PFN_vkGetDeviceQueue2 proc = (PFN_vkGetDeviceQueue2)real_dlsym(
        vulkan_loader_handle, "vkGetDeviceQueue2");
    if (proc != &vkGetDeviceQueue2) real_get_device_queue2 = proc;
  }
  if (!real_get_device_queue2 && device &&
      resolve_real_vk_get_device_proc_addr(active_vulkan_instance)) {
    PFN_vkGetDeviceQueue2 proc = (PFN_vkGetDeviceQueue2)
        real_get_device_proc_addr(device, "vkGetDeviceQueue2");
    if (proc != &vkGetDeviceQueue2) real_get_device_queue2 = proc;
  }
  return real_get_device_queue2;
}

static PFN_vkCreateCommandPool resolve_real_vk_create_command_pool(
    void *device) {
  if (real_create_command_pool == &vkCreateCommandPool)
    real_create_command_pool = NULL;
  if (!real_create_command_pool && !vulkan_loader_handle)
    resolve_vulkan_loader();
  if (!real_create_command_pool && vulkan_loader_handle && real_dlsym) {
    PFN_vkCreateCommandPool proc = (PFN_vkCreateCommandPool)real_dlsym(
        vulkan_loader_handle, "vkCreateCommandPool");
    if (proc != &vkCreateCommandPool) real_create_command_pool = proc;
  }
  if (!real_create_command_pool && device &&
      resolve_real_vk_get_device_proc_addr(active_vulkan_instance)) {
    PFN_vkCreateCommandPool proc = (PFN_vkCreateCommandPool)
        real_get_device_proc_addr(device, "vkCreateCommandPool");
    if (proc != &vkCreateCommandPool) real_create_command_pool = proc;
  }
  return real_create_command_pool;
}

static PFN_vkQueuePresentKHR resolve_real_vk_queue_present(void *device) {
  if (real_queue_present == &vkQueuePresentKHR) real_queue_present = NULL;
  if (!real_queue_present && !vulkan_loader_handle) resolve_vulkan_loader();
  if (!real_queue_present && vulkan_loader_handle && real_dlsym) {
    PFN_vkQueuePresentKHR proc = (PFN_vkQueuePresentKHR)real_dlsym(
        vulkan_loader_handle, "vkQueuePresentKHR");
    if (proc != &vkQueuePresentKHR) real_queue_present = proc;
  }
  if (!real_queue_present && device &&
      resolve_real_vk_get_device_proc_addr(active_vulkan_instance)) {
    PFN_vkQueuePresentKHR proc = (PFN_vkQueuePresentKHR)
        real_get_device_proc_addr(device, "vkQueuePresentKHR");
    if (proc != &vkQueuePresentKHR) real_queue_present = proc;
  }
  return real_queue_present;
}

static void shim_log(const char *message) {
  int fd = open("/opt/steam/logs/stearlight-vulkan-wsi.log",
                O_WRONLY | O_CREAT | O_APPEND, 0644);
  if (fd >= 0) {
    int written = dprintf(fd, "%s\n", message);
    (void)written;
    close(fd);
  }
  fd = open("/dev/ttyAMA0", O_WRONLY | O_NOCTTY | O_APPEND);
  if (fd >= 0) {
    int written = dprintf(fd, "STEARLIGHT WSI SHIM: %s\n", message);
    (void)written;
    close(fd);
  }
}

static PFN_vkGetPhysicalDeviceQueueFamilyProperties
resolve_queue_family_properties(void *instance) {
  if (real_get_queue_family_properties ==
      &vkGetPhysicalDeviceQueueFamilyProperties)
    real_get_queue_family_properties = NULL;
  if (!real_get_queue_family_properties && instance &&
      real_get_instance_proc_addr) {
    PFN_vkGetPhysicalDeviceQueueFamilyProperties proc =
        (PFN_vkGetPhysicalDeviceQueueFamilyProperties)
            real_get_instance_proc_addr(
                instance, "vkGetPhysicalDeviceQueueFamilyProperties");
    if (proc != &vkGetPhysicalDeviceQueueFamilyProperties)
      real_get_queue_family_properties = proc;
  }
  if (!real_get_queue_family_properties && vulkan_loader_handle &&
      real_dlsym) {
    PFN_vkGetPhysicalDeviceQueueFamilyProperties proc =
        (PFN_vkGetPhysicalDeviceQueueFamilyProperties)real_dlsym(
            vulkan_loader_handle,
            "vkGetPhysicalDeviceQueueFamilyProperties");
    if (proc != &vkGetPhysicalDeviceQueueFamilyProperties)
      real_get_queue_family_properties = proc;
  }
  return real_get_queue_family_properties;
}

static PFN_vkGetPhysicalDeviceQueueFamilyProperties2
resolve_queue_family_properties2(void *instance, const char *name) {
  if (real_get_queue_family_properties2 ==
          &vkGetPhysicalDeviceQueueFamilyProperties2 ||
      real_get_queue_family_properties2 ==
          &vkGetPhysicalDeviceQueueFamilyProperties2KHR)
    real_get_queue_family_properties2 = NULL;
  if (!real_get_queue_family_properties2 && instance &&
      real_get_instance_proc_addr) {
    PFN_vkGetPhysicalDeviceQueueFamilyProperties2 proc =
        (PFN_vkGetPhysicalDeviceQueueFamilyProperties2)
            real_get_instance_proc_addr(instance, name);
    if (!proc)
      proc = (PFN_vkGetPhysicalDeviceQueueFamilyProperties2)
          real_get_instance_proc_addr(
              instance,
              !strcmp(name, "vkGetPhysicalDeviceQueueFamilyProperties2")
                  ? "vkGetPhysicalDeviceQueueFamilyProperties2KHR"
                  : "vkGetPhysicalDeviceQueueFamilyProperties2");
    if (proc != &vkGetPhysicalDeviceQueueFamilyProperties2 &&
        proc != &vkGetPhysicalDeviceQueueFamilyProperties2KHR)
      real_get_queue_family_properties2 = proc;
  }
  if (!real_get_queue_family_properties2 && vulkan_loader_handle &&
      real_dlsym) {
    PFN_vkGetPhysicalDeviceQueueFamilyProperties2 proc =
        (PFN_vkGetPhysicalDeviceQueueFamilyProperties2)real_dlsym(
            vulkan_loader_handle, name);
    if (proc != &vkGetPhysicalDeviceQueueFamilyProperties2 &&
        proc != &vkGetPhysicalDeviceQueueFamilyProperties2KHR)
      real_get_queue_family_properties2 = proc;
  }
  return real_get_queue_family_properties2;
}

static PFN_vkCreateDevice resolve_real_vk_create_device(void *instance) {
  if (real_create_device == &vkCreateDevice) real_create_device = NULL;
  if (!vulkan_loader_handle || !real_dlsym) resolve_vulkan_loader();

  if (!real_create_device && instance && real_get_instance_proc_addr) {
    PFN_vkCreateDevice proc =
        (PFN_vkCreateDevice)real_get_instance_proc_addr(instance,
                                                       "vkCreateDevice");
    if (proc != &vkCreateDevice) real_create_device = proc;
  }

  if (!real_create_device && vulkan_loader_handle && real_dlsym) {
    PFN_vkCreateDevice proc = (PFN_vkCreateDevice)real_dlsym(
        vulkan_loader_handle, "vkCreateDevice");
    if (proc != &vkCreateDevice) {
      real_create_device = proc;
      shim_log("resolved vkCreateDevice from the Vulkan loader handle");
    }
  }
  return real_create_device;
}

int32_t vkEnumerateInstanceExtensionProperties(const char *layer_name,
                                               uint32_t *count,
                                               void *properties) {
  if (!real_enumerate_instance_extensions ||
      real_enumerate_instance_extensions ==
          &vkEnumerateInstanceExtensionProperties)
    resolve_vulkan_loader();
  if (!real_enumerate_instance_extensions || !count) return -3;
  if (layer_name && layer_name[0])
    return real_enumerate_instance_extensions(layer_name, count, properties);

  uint32_t capacity = properties ? *count : 0;
  int32_t result = real_enumerate_instance_extensions(layer_name, count,
                                                       properties);
  if (result != 0 && result != 5) return result;

  uint32_t actual_count = *count;
  VkExtensionPropertiesCompat *extensions = properties;
  for (uint32_t i = 0; extensions && i < actual_count; ++i) {
    if (!strcmp(extensions[i].extensionName, "VK_KHR_display")) return result;
  }
  if (!properties) {
    *count = actual_count + 1;
    return result;
  }
  if (capacity <= actual_count) {
    *count = capacity;
    return 5;
  }

  VkExtensionPropertiesCompat *display = &extensions[actual_count];
  memset(display, 0, sizeof(*display));
  memcpy(display->extensionName, "VK_KHR_display", sizeof("VK_KHR_display"));
  display->specVersion = 23;
  *count = actual_count + 1;
  shim_log("advertised synthetic VK_KHR_display extension");
  return result;
}

static int refresh_compute_queue_alias(void *physical_device);

static int device_supports_extension(void *physical_device,
                                     const char *extension_name) {
  if (!real_enumerate_device_extensions || !physical_device) return 0;
  uint32_t count = 0;
  int32_t result = real_enumerate_device_extensions(
      physical_device, NULL, &count, NULL);
  if (result != 0 || !count) return 0;
  VkExtensionPropertiesCompat *extensions = calloc(count, sizeof(*extensions));
  if (!extensions) return 0;
  result = real_enumerate_device_extensions(physical_device, NULL, &count,
                                            extensions);
  int found = 0;
  if (result == 0 || result == 5)
    for (uint32_t i = 0; i < count; ++i)
      if (!strcmp(extensions[i].extensionName, extension_name)) found = 1;
  free(extensions);
  return found;
}

int32_t vkEnumerateDeviceExtensionProperties(void *physical_device,
                                             const char *layer_name,
                                             uint32_t *count,
                                             void *properties) {
  if (!real_enumerate_device_extensions && real_get_instance_proc_addr &&
      active_vulkan_instance) {
    PFN_vkEnumerateDeviceExtensionProperties enumerate_extensions =
        (PFN_vkEnumerateDeviceExtensionProperties)real_get_instance_proc_addr(
            active_vulkan_instance, "vkEnumerateDeviceExtensionProperties");
    if (enumerate_extensions &&
        enumerate_extensions != &vkEnumerateDeviceExtensionProperties)
      real_enumerate_device_extensions = enumerate_extensions;
  }
  if (!real_enumerate_device_extensions || !count) return -3;
  if (layer_name && layer_name[0]) {
    if (!layer_device_extensions_reported) {
      shim_log("enumerating layer-specific Vulkan device extensions");
      layer_device_extensions_reported = 1;
    }
    return real_enumerate_device_extensions(physical_device, layer_name, count,
                                            properties);
  }

  int has_present_wait =
      device_supports_extension(physical_device, "VK_KHR_present_wait");
  int has_present_id =
      device_supports_extension(physical_device, "VK_KHR_present_id");
  int has_swapchain =
      device_supports_extension(physical_device, "VK_KHR_swapchain");
  int has_global_priority =
      device_supports_extension(physical_device, "VK_EXT_global_priority");
  synthetic_present_id_available = !has_present_id && has_swapchain;
  synthetic_present_wait_available =
      !has_present_wait && has_swapchain &&
      (has_present_id || synthetic_present_id_available);
  synthetic_global_priority_available =
      !has_global_priority && refresh_compute_queue_alias(physical_device);
  if (!device_extensions_reported) {
    char message[256];
    snprintf(message, sizeof(message),
             "device extensions: present_wait=%s present_id=%s swapchain=%s synthetic_wait=%s synthetic_id=%s global_priority=%s synthetic_global_priority=%s",
             has_present_wait ? "yes" : "no",
             has_present_id ? "yes" : "no",
             has_swapchain ? "yes" : "no",
             synthetic_present_wait_available ? "yes" : "no",
             synthetic_present_id_available ? "yes" : "no",
             has_global_priority ? "yes" : "no",
             synthetic_global_priority_available ? "yes" : "no");
    shim_log(message);
    device_extensions_reported = 1;
  }

  uint32_t actual_count = 0;
  int32_t result = real_enumerate_device_extensions(
      physical_device, NULL, &actual_count, NULL);
  if (result != 0) return result;
  VkExtensionPropertiesCompat *actual_extensions = NULL;
  if (actual_count) {
    actual_extensions = calloc(actual_count, sizeof(*actual_extensions));
    if (!actual_extensions) return -1;
    uint32_t capacity = actual_count;
    result = real_enumerate_device_extensions(
        physical_device, NULL, &capacity, actual_extensions);
    if (result != 0 && result != 5) {
      free(actual_extensions);
      return result;
    }
    actual_count = capacity;
  }
  uint32_t visible_actual_count = 0;
  for (uint32_t i = 0; i < actual_count; ++i) {
    if (!device_extension_hidden(actual_extensions[i].extensionName))
      ++visible_actual_count;
  }
  if (!timeline_bridge_enabled() && device_supports_extension(physical_device,
                                "VK_KHR_timeline_semaphore") &&
      !timeline_extension_hidden_logged) {
    shim_log("hiding VK_KHR_timeline_semaphore to avoid unsupported timeline semaphore export");
    timeline_extension_hidden_logged = 1;
  }
  uint32_t synthetic_count =
      (synthetic_present_id_available ? 1u : 0u) +
      (synthetic_present_wait_available ? 1u : 0u) +
      (synthetic_global_priority_available ? 1u : 0u);
  int bridge_fd_extension = timeline_bridge_enabled() &&
      !device_supports_extension(physical_device, "VK_KHR_external_semaphore_fd");
  synthetic_count += bridge_fd_extension ? 1u : 0u;
  if (!properties) {
    *count = visible_actual_count + synthetic_count;
    if ((synthetic_present_id_available || synthetic_present_wait_available) && !synthetic_present_wait_logged) {
      shim_log("advertised synthetic VK_KHR_present_id and VK_KHR_present_wait");
      synthetic_present_wait_logged = 1;
    }
    if (synthetic_global_priority_available &&
        !synthetic_global_priority_logged) {
      shim_log("advertised synthetic VK_EXT_global_priority for the aliased queue");
      synthetic_global_priority_logged = 1;
    }
    free(actual_extensions);
    return 0;
  }

  uint32_t capacity = *count;
  uint32_t written = 0;
  int incomplete = 0;
  VkExtensionPropertiesCompat *extensions = properties;
  for (uint32_t i = 0; i < actual_count; ++i) {
    if (device_extension_hidden(actual_extensions[i].extensionName))
      continue;
    if (written < capacity)
      extensions[written++] = actual_extensions[i];
    else
      incomplete = 1;
  }
  if (bridge_fd_extension) {
    if (written < capacity) {
      VkExtensionPropertiesCompat *extension = &extensions[written++];
      memset(extension, 0, sizeof(*extension));
      memcpy(extension->extensionName, "VK_KHR_external_semaphore_fd",
             sizeof("VK_KHR_external_semaphore_fd"));
      extension->specVersion = 1;
    } else incomplete = 1;
  }
  if (synthetic_present_id_available) {
    if (written < capacity) {
      VkExtensionPropertiesCompat *present_id = &extensions[written++];
      memset(present_id, 0, sizeof(*present_id));
      memcpy(present_id->extensionName, "VK_KHR_present_id",
             sizeof("VK_KHR_present_id"));
      present_id->specVersion = 1;
    } else {
      incomplete = 1;
    }
  }
  if (synthetic_present_wait_available) {
    if (written < capacity) {
      VkExtensionPropertiesCompat *present_wait = &extensions[written++];
      memset(present_wait, 0, sizeof(*present_wait));
      memcpy(present_wait->extensionName, "VK_KHR_present_wait",
             sizeof("VK_KHR_present_wait"));
      present_wait->specVersion = 1;
    } else {
      incomplete = 1;
    }
  }
  if (synthetic_global_priority_available) {
    if (written < capacity) {
      VkExtensionPropertiesCompat *global_priority = &extensions[written++];
      memset(global_priority, 0, sizeof(*global_priority));
      memcpy(global_priority->extensionName, "VK_EXT_global_priority",
             sizeof("VK_EXT_global_priority"));
      global_priority->specVersion = 2;
    } else {
      incomplete = 1;
    }
  }
  *count = written;
  free(actual_extensions);
  if ((synthetic_present_id_available || synthetic_present_wait_available) && !synthetic_present_wait_logged) {
    shim_log("advertised synthetic VK_KHR_present_id and VK_KHR_present_wait");
    synthetic_present_wait_logged = 1;
  }
  return incomplete ? 5 : 0;
}

static void set_synthetic_present_features(void *features) {
  if (!features || (!synthetic_present_id_available &&
                    !synthetic_present_wait_available))
    return;
  VkBaseOutStructureCompat *base = features;
  VkBaseOutStructureCompat *next = base->pNext;
  while (next) {
    if (next->sType ==
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR_COMPAT &&
        synthetic_present_id_available) {
      ((VkPresentIdFeaturesCompat *)next)->presentId = 1;
      if (synthetic_present_id_available) shim_log("enabled synthetic VK_KHR_present_id feature");
    } else if (
        next->sType ==
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR_COMPAT &&
        synthetic_present_wait_available) {
      ((VkPresentWaitFeaturesCompat *)next)->presentWait = 1;
      if (synthetic_present_wait_available) shim_log("enabled synthetic VK_KHR_present_wait feature");
    }
    next = next->pNext;
  }
}

static void mask_timeline_semaphore_features(
    void *chain, VkTimelineFeatureRestoreCompat *restore,
    size_t *restore_count) {
  if (timeline_bridge_enabled()) return;
  VkBaseOutStructureCompat *next = chain;
  size_t scanned = 0;
  while (next && scanned++ < 64 && (uintptr_t)next >= 4096) {
    uint32_t *feature = NULL;
    if (next->sType == 1000207000u) {
      feature = &((VkTimelineSemaphoreFeaturesCompat *)next)->timelineSemaphore;
    } else if (next->sType == 51u) {
      feature = &((VkVulkan12FeaturesCompat *)next)->timelineSemaphore;
    }
    if (feature) {
      if (!timeline_feature_mask_logged) {
        shim_log("masking Vulkan timeline semaphore support for binary FD export");
        timeline_feature_mask_logged = 1;
      }
      if (restore && restore_count && *feature &&
          *restore_count < MAX_PNEXT_RESTORE) {
        restore[*restore_count].feature = feature;
        restore[*restore_count].original_value = *feature;
        ++*restore_count;
      }
      *feature = 0;
    }
    next = next->pNext;
  }
}

static void restore_timeline_semaphore_features(
    VkTimelineFeatureRestoreCompat *restore, size_t restore_count) {
  while (restore_count > 0) {
    --restore_count;
    *restore[restore_count].feature = restore[restore_count].original_value;
  }
}

void vkGetPhysicalDeviceFeatures2(void *physical_device, void *features) {
  if (!real_get_physical_device_features2 && real_get_instance_proc_addr &&
      active_vulkan_instance) {
    PFN_vkGetPhysicalDeviceFeatures2 get_features2 =
        (PFN_vkGetPhysicalDeviceFeatures2)real_get_instance_proc_addr(
            active_vulkan_instance, "vkGetPhysicalDeviceFeatures2");
    if (!get_features2)
      get_features2 = (PFN_vkGetPhysicalDeviceFeatures2)
          real_get_instance_proc_addr(active_vulkan_instance,
                                      "vkGetPhysicalDeviceFeatures2KHR");
    if (get_features2 && get_features2 != &vkGetPhysicalDeviceFeatures2 &&
        get_features2 != &vkGetPhysicalDeviceFeatures2KHR)
      real_get_physical_device_features2 = get_features2;
  }
  if (real_get_physical_device_features2)
    real_get_physical_device_features2(physical_device, features);
  set_synthetic_present_features(features);
  if (features && (uintptr_t)features >= 4096)
    mask_timeline_semaphore_features(
        ((VkBaseOutStructureCompat *)features)->pNext, NULL, NULL);
}

void vkGetPhysicalDeviceFeatures2KHR(void *physical_device, void *features) {
  vkGetPhysicalDeviceFeatures2(physical_device, features);
}

int32_t vkWaitForPresentKHR(void *device, uint64_t swapchain,
                            uint64_t present_id, uint64_t timeout) {
  if (synthetic_present_wait_available) {
    shim_log("completed synthetic vkWaitForPresentKHR");
    return 0;
  }
  if (resolve_real_vk_get_device_proc_addr(active_vulkan_instance)) {
    PFN_vkWaitForPresentKHR wait_for_present =
        (PFN_vkWaitForPresentKHR)real_get_device_proc_addr(
            device, "vkWaitForPresentKHR");
    if (wait_for_present && wait_for_present != &vkWaitForPresentKHR) {
      int32_t result = wait_for_present(device, swapchain, present_id, timeout);
      static unsigned trace_count;
      if (getenv("STEARLIGHT_VULKAN_TRACE") &&
          __atomic_fetch_add(&trace_count, 1, __ATOMIC_RELAXED) < 8) {
        char message[160];
        snprintf(message, sizeof(message), "present wait id=%llu timeout=%llu result=%d",
                 (unsigned long long)present_id, (unsigned long long)timeout, result);
        shim_log(message);
      }
      return result;
    }
  }
  return -7;
}

int32_t vkQueuePresentKHR(void *queue, const void *present_info) {
  if (!resolve_real_vk_queue_present(NULL) || !present_info) return -7;

  const VkPresentInfoCompat *info = present_info;
  const VkBaseInStructureCompat *next = info->pNext;
  int locked = lock_aliased_queue_operations();
  int32_t result;
  if (synthetic_present_id_available && next &&
      next->sType == VK_STRUCTURE_TYPE_PRESENT_ID_KHR_COMPAT) {
    VkPresentInfoCompat modified = *info;
    modified.pNext = next->pNext;
    shim_log("stripped synthetic VkPresentIdKHR before queue present");
    result = real_queue_present(queue, &modified);
  } else {
    result = real_queue_present(queue, present_info);
  }
  unlock_aliased_queue_operations(locked);
  static unsigned trace_count;
  if (getenv("STEARLIGHT_VULKAN_TRACE") &&
      __atomic_fetch_add(&trace_count, 1, __ATOMIC_RELAXED) < 8) {
    char message[160];
    snprintf(message, sizeof(message), "queue present count=%u chain=%u result=%d",
             info->swapchainCount, next ? next->sType : 0u, result);
    shim_log(message);
  }
  return result;
}

static int lock_aliased_queue_operations(void) {
  if (!queue_alias_available) return 0;
  while (__atomic_exchange_n(&queue_alias_operations_lock, 1,
                             __ATOMIC_ACQUIRE))
    sched_yield();
  if (!queue_alias_serialization_logged) {
    queue_alias_serialization_logged = 1;
    shim_log("serializing host queue operations for the aliased graphics/compute queue");
  }
  return 1;
}

static void unlock_aliased_queue_operations(int locked) {
  if (locked)
    __atomic_store_n(&queue_alias_operations_lock, 0, __ATOMIC_RELEASE);
}

static void *resolve_real_queue_proc(const char *name, void *wrapper) {
  if (!active_vulkan_device ||
      !resolve_real_vk_get_device_proc_addr(active_vulkan_instance))
    return NULL;
  void *proc = real_get_device_proc_addr(active_vulkan_device, name);
  if (proc == wrapper) {
    if (!vulkan_loader_handle || !real_dlsym) resolve_vulkan_loader();
    if (vulkan_loader_handle && real_dlsym)
      proc = real_dlsym(vulkan_loader_handle, name);
  }
  return proc == wrapper ? NULL : proc;
}

static void *resolve_real_loader_proc(const char *name, void *wrapper) {
  if (!vulkan_loader_handle || !real_dlsym) resolve_vulkan_loader();
  if (vulkan_loader_handle && real_dlsym) {
    void *proc = real_dlsym(vulkan_loader_handle, name);
    if (proc && proc != wrapper) return proc;
  }
  return resolve_real_queue_proc(name, wrapper);
}

#include "vulkan_timeline_bridge.inc"

static void *wrap_queue_api(const char *name, void *proc) {
  if (!name || !proc) return NULL;
  if (!strcmp(name, "vkQueueSubmit"))
    return (void *)(uintptr_t)&vkQueueSubmit;
  if (!strcmp(name, "vkQueueSubmit2"))
    return (void *)(uintptr_t)&vkQueueSubmit2;
  if (!strcmp(name, "vkQueueSubmit2KHR"))
    return (void *)(uintptr_t)&vkQueueSubmit2KHR;
  if (!strcmp(name, "vkQueueBindSparse"))
    return (void *)(uintptr_t)&vkQueueBindSparse;
  if (!strcmp(name, "vkQueueWaitIdle"))
    return (void *)(uintptr_t)&vkQueueWaitIdle;
  if (!strcmp(name, "vkDeviceWaitIdle"))
    return (void *)(uintptr_t)&vkDeviceWaitIdle;
  if (!strcmp(name, "vkQueuePresentKHR"))
    return (void *)(uintptr_t)&vkQueuePresentKHR;
  return NULL;
}

static void *wrap_semaphore_fd_api(const char *name, void *proc) {
  if (!name || (!proc && !timeline_bridge_enabled())) return NULL;
  if (!strcmp(name, "vkGetSemaphoreFdKHR"))
    return (void *)(uintptr_t)&vkGetSemaphoreFdKHR;
  if (!strcmp(name, "vkImportSemaphoreFdKHR"))
    return (void *)(uintptr_t)&vkImportSemaphoreFdKHR;
  return NULL;
}

static void *wrap_semaphore_create_api(const char *name, void *proc) {
  if (name && proc && !strcmp(name, "vkCreateSemaphore"))
    return (void *)(uintptr_t)&vkCreateSemaphore;
  if (name && proc && timeline_bridge_enabled()) {
    if (!strcmp(name, "vkDestroySemaphore")) return (void *)(uintptr_t)&timeline_destroy_semaphore;
    if (!strcmp(name, "vkDestroyDevice")) return (void *)(uintptr_t)&timeline_destroy_device;
    if (!strcmp(name, "vkGetPhysicalDeviceExternalSemaphoreProperties") ||
        !strcmp(name, "vkGetPhysicalDeviceExternalSemaphorePropertiesKHR"))
      return (void *)(uintptr_t)&timeline_external_properties;
  }
  return NULL;
}

int32_t vkCreateSemaphore(void *device, const void *create_info,
                          const void *allocator, void **semaphore) {
  PFN_vkCreateSemaphore create = (PFN_vkCreateSemaphore)
      resolve_real_loader_proc("vkCreateSemaphore",
                               (void *)(uintptr_t)&vkCreateSemaphore);
  if (!create) {
    shim_log("vkCreateSemaphore driver proc is unavailable");
    return -7;
  }

  if (!create_info || (uintptr_t)create_info < 4096) {
    int32_t result = create(device, create_info, allocator, semaphore);
    char message[160];
    snprintf(message, sizeof(message),
             "vkCreateSemaphore result=%d device=%p info=%p semaphore=%p",
             result, device, create_info,
             semaphore ? *semaphore : NULL);
    shim_log(message);
    return result;
  }

  const VkSemaphoreCreateInfoCompat *info = create_info;
  uint32_t export_handle_types = 0;
  uint32_t semaphore_type = 0;
  VkSemaphoreTypeCreateInfoCompat *type_info = NULL;
  const VkBaseInStructureCompat *next = info->pNext;
  for (uint32_t i = 0; next && i < MAX_PNEXT_RESTORE; ++i) {
    if ((uintptr_t)next < 4096) break;
    if (next->sType == 1000077000u) {
      export_handle_types =
          ((const VkExportSemaphoreCreateInfoCompat *)next)->handleTypes;
    } else if (next->sType == 1000207002u) {
      type_info = (VkSemaphoreTypeCreateInfoCompat *)(uintptr_t)next;
      semaphore_type =
          ((const VkSemaphoreTypeCreateInfoCompat *)next)->semaphoreType;
    }
    next = next->pNext;
  }

  int downgraded_timeline_semaphore =
      !timeline_bridge_enabled() && type_info && semaphore_type == 1u && (export_handle_types & 1u);
  VkExportSemaphoreCreateInfoCompat *export_info = NULL;
  if (timeline_bridge_enabled() && semaphore_type == 1u) {
    for (VkBaseInStructureCompat *node = (void *)info->pNext; node; node = (void *)node->pNext) {
      if (node->sType == 1000077000u) {
        export_info = (void *)node;
        export_info->handleTypes &= ~1u;
        break;
      }
    }
  }
  if (downgraded_timeline_semaphore) {
    type_info->semaphoreType = 0;
    shim_log("downgrading exported timeline semaphore to binary for SwiftShader FD export");
  }
  int32_t result = create(device, create_info, allocator, semaphore);
  if (export_info) export_info->handleTypes = export_handle_types;
  if (result == 0 && timeline_bridge_enabled() && semaphore_type == 1u) {
    int tracked = timeline_register(device, (uint64_t)(uintptr_t)*semaphore,
                                   (export_handle_types & 1u) != 0);
    if (tracked) {
      timeline_destroy_semaphore(device, (uint64_t)(uintptr_t)*semaphore, allocator);
      *semaphore = NULL;
      result = tracked;
    }
  }
  if (downgraded_timeline_semaphore) type_info->semaphoreType = semaphore_type;

  char message[256];
  snprintf(message, sizeof(message),
           "vkCreateSemaphore result=%d device=%p info=%p sType=%u flags=%u semaphoreType=%u exportHandleTypes=0x%x downgraded=%s semaphore=%p",
           result, device, create_info, info->sType, info->flags,
           semaphore_type, export_handle_types,
           downgraded_timeline_semaphore ? "yes" : "no",
           semaphore ? *semaphore : NULL);
  shim_log(message);
  return result;
}

int32_t vkGetSemaphoreFdKHR(void *device, const void *get_info, int *fd) {
  if (!device) device = active_vulkan_device;
  if (!device) {
    shim_log("vkGetSemaphoreFdKHR has no device handle");
    return -3;
  }
  if (!get_info || (uintptr_t)get_info < 4096 || !fd ||
      (uintptr_t)fd < 4096) {
    char message[192];
    snprintf(message, sizeof(message),
             "vkGetSemaphoreFdKHR rejected pointers device=%p info=%p fd=%p",
             device, get_info, (void *)fd);
    shim_log(message);
    return -1000072003;
  }

  const VkSemaphoreGetFdInfoCompat *info = get_info;
  if (timeline_bridge_enabled()) return timeline_export(device, info, fd);
  char message[224];
  snprintf(message, sizeof(message),
           "vkGetSemaphoreFdKHR device=%p active=%p info=%p sType=%u semaphore=0x%llx handleType=%u fd=%p",
           device, active_vulkan_device, get_info, info->sType,
           (unsigned long long)info->semaphore, info->handleType, (void *)fd);
  shim_log(message);

  PFN_vkGetSemaphoreFdKHR get_fd = (PFN_vkGetSemaphoreFdKHR)
      resolve_real_loader_proc("vkGetSemaphoreFdKHR",
                               (void *)(uintptr_t)&vkGetSemaphoreFdKHR);
  if (!get_fd) {
    shim_log("vkGetSemaphoreFdKHR driver proc is unavailable");
    return -7;
  }
  snprintf(message, sizeof(message), "vkGetSemaphoreFdKHR forwarding proc=%p",
           (void *)(uintptr_t)get_fd);
  shim_log(message);
  return get_fd(device, get_info, fd);
}

int32_t vkImportSemaphoreFdKHR(void *device, const void *import_info) {
  if (!device) device = active_vulkan_device;
  if (!device) {
    shim_log("vkImportSemaphoreFdKHR has no device handle");
    return -3;
  }
  if (!import_info || (uintptr_t)import_info < 4096) {
    char message[192];
    snprintf(message, sizeof(message),
             "vkImportSemaphoreFdKHR rejected pointers device=%p info=%p",
             device, import_info);
    shim_log(message);
    return -1000072003;
  }

  const VkImportSemaphoreFdInfoCompat *info = import_info;
  if (timeline_bridge_enabled()) return timeline_import(device, info);
  char message[224];
  snprintf(message, sizeof(message),
           "vkImportSemaphoreFdKHR device=%p active=%p info=%p sType=%u semaphore=0x%llx flags=%u handleType=%u fd=%d",
           device, active_vulkan_device, import_info, info->sType,
           (unsigned long long)info->semaphore, info->flags, info->handleType,
           info->fd);
  shim_log(message);

  PFN_vkImportSemaphoreFdKHR import_fd = (PFN_vkImportSemaphoreFdKHR)
      resolve_real_loader_proc("vkImportSemaphoreFdKHR",
                               (void *)(uintptr_t)&vkImportSemaphoreFdKHR);
  if (!import_fd) {
    shim_log("vkImportSemaphoreFdKHR driver proc is unavailable");
    return -7;
  }
  return import_fd(device, import_info);
}

int32_t vkQueueSubmit(void *queue, uint32_t submit_count,
                      const void *submits, void *fence) {
  PFN_vkQueueSubmit submit =
      (PFN_vkQueueSubmit)resolve_real_queue_proc(
          "vkQueueSubmit", (void *)(uintptr_t)&vkQueueSubmit);
  if (!submit) return -3;
  int locked = lock_aliased_queue_operations();
  int32_t result = submit(queue, submit_count, submits, fence);
  unlock_aliased_queue_operations(locked);
  return result;
}

static int32_t queue_submit2(void *queue, uint32_t submit_count,
                             const void *submits, void *fence,
                             const char *name, void *wrapper) {
  PFN_vkQueueSubmit2 submit2 =
      (PFN_vkQueueSubmit2)resolve_real_queue_proc(name, wrapper);
  if (!submit2 && strcmp(name, "vkQueueSubmit2KHR"))
    submit2 = (PFN_vkQueueSubmit2)resolve_real_queue_proc(
        "vkQueueSubmit2KHR", wrapper);
  if (!submit2 && strcmp(name, "vkQueueSubmit2"))
    submit2 = (PFN_vkQueueSubmit2)resolve_real_queue_proc(
        "vkQueueSubmit2", wrapper);
  if (!submit2) return -3;
  int locked = lock_aliased_queue_operations();
  int32_t result = submit2(queue, submit_count, submits, fence);
  unlock_aliased_queue_operations(locked);
  return result;
}

int32_t vkQueueSubmit2(void *queue, uint32_t submit_count,
                       const void *submits, void *fence) {
  return queue_submit2(queue, submit_count, submits, fence, "vkQueueSubmit2",
                       (void *)(uintptr_t)&vkQueueSubmit2);
}

int32_t vkQueueSubmit2KHR(void *queue, uint32_t submit_count,
                          const void *submits, void *fence) {
  return queue_submit2(queue, submit_count, submits, fence,
                       "vkQueueSubmit2KHR",
                       (void *)(uintptr_t)&vkQueueSubmit2KHR);
}

int32_t vkQueueBindSparse(void *queue, uint32_t bind_info_count,
                          const void *bind_infos, void *fence) {
  PFN_vkQueueBindSparse bind_sparse =
      (PFN_vkQueueBindSparse)resolve_real_queue_proc(
          "vkQueueBindSparse", (void *)(uintptr_t)&vkQueueBindSparse);
  if (!bind_sparse) return -3;
  int locked = lock_aliased_queue_operations();
  int32_t result = bind_sparse(queue, bind_info_count, bind_infos, fence);
  unlock_aliased_queue_operations(locked);
  return result;
}

int32_t vkQueueWaitIdle(void *queue) {
  PFN_vkQueueWaitIdle wait_idle =
      (PFN_vkQueueWaitIdle)resolve_real_queue_proc(
          "vkQueueWaitIdle", (void *)(uintptr_t)&vkQueueWaitIdle);
  if (!wait_idle) return -3;
  int locked = lock_aliased_queue_operations();
  int32_t result = wait_idle(queue);
  unlock_aliased_queue_operations(locked);
  return result;
}

int32_t vkDeviceWaitIdle(void *device) {
  PFN_vkDeviceWaitIdle wait_idle =
      (PFN_vkDeviceWaitIdle)resolve_real_queue_proc(
          "vkDeviceWaitIdle", (void *)(uintptr_t)&vkDeviceWaitIdle);
  if (!wait_idle) return -3;
  int locked = lock_aliased_queue_operations();
  int32_t result = wait_idle(device);
  unlock_aliased_queue_operations(locked);
  return result;
}

static void cache_vk_create_device(void **instance, int32_t result) {
  if (result != 0 || !instance || !*instance) return;
  active_vulkan_instance = *instance;
  (void)resolve_real_vk_create_device(*instance);
  if (real_get_instance_proc_addr) {
    PFN_vkEnumerateDeviceExtensionProperties enumerate_extensions =
        (PFN_vkEnumerateDeviceExtensionProperties)real_get_instance_proc_addr(
            *instance, "vkEnumerateDeviceExtensionProperties");
    if (enumerate_extensions &&
        enumerate_extensions != &vkEnumerateDeviceExtensionProperties)
      real_enumerate_device_extensions = enumerate_extensions;
    PFN_vkGetPhysicalDeviceFeatures2 get_features2 =
        (PFN_vkGetPhysicalDeviceFeatures2)real_get_instance_proc_addr(
            *instance, "vkGetPhysicalDeviceFeatures2");
    if (!get_features2)
      get_features2 = (PFN_vkGetPhysicalDeviceFeatures2)
          real_get_instance_proc_addr(*instance,
                                      "vkGetPhysicalDeviceFeatures2KHR");
    if (get_features2 && get_features2 != &vkGetPhysicalDeviceFeatures2 &&
        get_features2 != &vkGetPhysicalDeviceFeatures2KHR)
      real_get_physical_device_features2 = get_features2;
  }
  shim_log(real_create_device ? "Vulkan loader vkCreateDevice proc=yes"
                              : "Vulkan loader vkCreateDevice proc=no");
}

static int refresh_compute_queue_alias(void *physical_device) {
  if (queue_alias_checked && queue_alias_physical_device == physical_device)
    return queue_alias_available;

  queue_alias_checked = 1;
  queue_alias_available = 0;
  queue_alias_physical_device = physical_device;
  queue_alias_backing_family = UINT32_MAX;
  queue_alias_family = UINT32_MAX;
  memset(&queue_alias_backing_properties, 0,
         sizeof(queue_alias_backing_properties));
  if (!physical_device || !active_vulkan_instance ||
      !real_get_instance_proc_addr)
    return 0;

  (void)resolve_queue_family_properties(active_vulkan_instance);
  if (!real_get_queue_family_properties ||
      real_get_queue_family_properties ==
          &vkGetPhysicalDeviceQueueFamilyProperties) {
    shim_log("real vkGetPhysicalDeviceQueueFamilyProperties is unavailable to the shim");
    return 0;
  }

  uint32_t family_count = 0;
  real_get_queue_family_properties(physical_device, &family_count, NULL);
  if (family_count != 1) {
    char message[96];
    snprintf(message, sizeof(message),
             "physical device exposes %u queue families; compute alias skipped",
             family_count);
    shim_log(message);
    return 0;
  }

  VkQueueFamilyPropertiesCompat properties;
  memset(&properties, 0, sizeof(properties));
  real_get_queue_family_properties(physical_device, &family_count,
                                  &properties);
  const uint32_t required_flags = VK_QUEUE_GRAPHICS_BIT_COMPAT |
                                 VK_QUEUE_COMPUTE_BIT_COMPAT |
                                 VK_QUEUE_TRANSFER_BIT_COMPAT;
  if (family_count != 1 || properties.queueCount == 0 ||
      (properties.queueFlags & required_flags) != required_flags) {
    char message[128];
    snprintf(message, sizeof(message),
             "physical queue family flags=0x%x count=%u; compute alias skipped",
             properties.queueFlags, properties.queueCount);
    shim_log(message);
    return 0;
  }

  queue_alias_backing_properties = properties;
  queue_alias_backing_family = 0;
  queue_alias_family = 1;
  queue_alias_available = 1;
  shim_log("created a synthetic compute queue family backed by the combined graphics/compute queue");
  return 1;
}

static uint32_t map_queue_family_index(uint32_t family_index) {
  return queue_alias_available && family_index == queue_alias_family
             ? queue_alias_backing_family
             : family_index;
}

void vkGetPhysicalDeviceQueueFamilyProperties(void *physical_device,
                                              uint32_t *count,
                                              void *properties) {
  if (!queue_property_query_logged) {
    shim_log("intercepted vkGetPhysicalDeviceQueueFamilyProperties");
    queue_property_query_logged = 1;
  }
  (void)resolve_queue_family_properties(active_vulkan_instance);
  if (!real_get_queue_family_properties ||
      real_get_queue_family_properties ==
          &vkGetPhysicalDeviceQueueFamilyProperties ||
      !count)
    return;
  if (!refresh_compute_queue_alias(physical_device)) {
    real_get_queue_family_properties(physical_device, count, properties);
    return;
  }

  uint32_t actual_count = 0;
  real_get_queue_family_properties(physical_device, &actual_count, NULL);
  if (!properties) {
    *count = actual_count + 1;
    return;
  }

  uint32_t capacity = *count;
  uint32_t actual_capacity = capacity < actual_count ? capacity : actual_count;
  uint32_t written = actual_capacity;
  real_get_queue_family_properties(physical_device, &written, properties);
  VkQueueFamilyPropertiesCompat *output = properties;
  /* Gamescope needs graphics and compute on its graphics queue. Keep the
   * backing family's real capabilities when adding the compute-only alias. */
  if (capacity > actual_count) {
    output[actual_count] = queue_alias_backing_properties;
    output[actual_count].queueFlags =
        (queue_alias_backing_properties.queueFlags &
         ~(VK_QUEUE_GRAPHICS_BIT_COMPAT | VK_QUEUE_COMPUTE_BIT_COMPAT)) |
        VK_QUEUE_COMPUTE_BIT_COMPAT;
    *count = actual_count + 1;
  } else {
    *count = written;
  }
}

void vkGetPhysicalDeviceQueueFamilyProperties2(void *physical_device,
                                               uint32_t *count,
                                               void *properties) {
  if (!queue_property_query_logged) {
    shim_log("intercepted vkGetPhysicalDeviceQueueFamilyProperties2");
    queue_property_query_logged = 1;
  }
  (void)resolve_queue_family_properties2(
      active_vulkan_instance, "vkGetPhysicalDeviceQueueFamilyProperties2");
  if (!real_get_queue_family_properties2 || !count) return;
  if (!refresh_compute_queue_alias(physical_device)) {
    real_get_queue_family_properties2(physical_device, count, properties);
    return;
  }

  uint32_t actual_count = 0;
  real_get_queue_family_properties2(physical_device, &actual_count, NULL);
  if (!properties) {
    *count = actual_count + 1;
    return;
  }

  uint32_t capacity = *count;
  uint32_t actual_capacity = capacity < actual_count ? capacity : actual_count;
  uint32_t written = actual_capacity;
  real_get_queue_family_properties2(physical_device, &written, properties);
  VkQueueFamilyProperties2Compat *output = properties;
  /* Preserve the real combined family, as in the original query above. */
  if (capacity > actual_count) {
    output[actual_count].queueFamilyProperties =
        queue_alias_backing_properties;
    output[actual_count].queueFamilyProperties.queueFlags =
        (queue_alias_backing_properties.queueFlags &
         ~(VK_QUEUE_GRAPHICS_BIT_COMPAT | VK_QUEUE_COMPUTE_BIT_COMPAT)) |
        VK_QUEUE_COMPUTE_BIT_COMPAT;
    *count = actual_count + 1;
  } else {
    *count = written;
  }
}

void vkGetPhysicalDeviceQueueFamilyProperties2KHR(void *physical_device,
                                                  uint32_t *count,
                                                  void *properties) {
  vkGetPhysicalDeviceQueueFamilyProperties2(physical_device, count,
                                            properties);
}

void vkGetDeviceQueue(void *device, uint32_t family_index,
                      uint32_t queue_index, void **queue) {
  if (!resolve_real_vk_get_device_queue(device)) {
    shim_log("real vkGetDeviceQueue is unavailable to the shim");
    return;
  }
  uint32_t mapped_family = map_queue_family_index(family_index);
  if (mapped_family != family_index || !queue_handle_query_logged) {
    char message[128];
    snprintf(message, sizeof(message),
             "vkGetDeviceQueue family=%u queue=%u mapped_family=%u",
             family_index, queue_index, mapped_family);
    shim_log(message);
    queue_handle_query_logged = 1;
  }
  real_get_device_queue(device, mapped_family, queue_index, queue);
}

void vkGetDeviceQueue2(void *device, const void *queue_info, void **queue) {
  if (!resolve_real_vk_get_device_queue2(device) || !queue_info) {
    return;
  }
  const VkDeviceQueueInfo2Compat *info = queue_info;
  uint32_t mapped_family = map_queue_family_index(info->queueFamilyIndex);
  if (mapped_family != info->queueFamilyIndex || !queue_handle_query_logged) {
    char message[128];
    snprintf(message, sizeof(message),
             "vkGetDeviceQueue2 family=%u queue=%u mapped_family=%u",
             info->queueFamilyIndex, info->queueIndex, mapped_family);
    shim_log(message);
    queue_handle_query_logged = 1;
  }
  if (mapped_family == info->queueFamilyIndex) {
    real_get_device_queue2(device, queue_info, queue);
    return;
  }
  shim_log("mapped vkGetDeviceQueue2 for the synthetic compute family");
  VkDeviceQueueInfo2Compat modified = *info;
  modified.queueFamilyIndex = mapped_family;
  real_get_device_queue2(device, &modified, queue);
}

int32_t vkCreateCommandPool(void *device, const void *create_info,
                            const void *allocator, void **command_pool) {
  if (!resolve_real_vk_create_command_pool(device) || !create_info)
    return -3;
  const VkCommandPoolCreateInfoCompat *info = create_info;
  uint32_t mapped_family = map_queue_family_index(info->queueFamilyIndex);
  if (mapped_family == info->queueFamilyIndex)
    return real_create_command_pool(device, create_info, allocator,
                                    command_pool);
  shim_log("mapped command pool to the combined backing queue family");
  VkCommandPoolCreateInfoCompat modified = *info;
  modified.queueFamilyIndex = mapped_family;
  return real_create_command_pool(device, &modified, allocator, command_pool);
}

static VkDisplayPropertiesCompat make_display_properties(void) {
  VkDisplayPropertiesCompat properties = {0};
  properties.display = fake_display;
  properties.displayName = fake_display_name;
  properties.physicalDimensions.width = 200;
  properties.physicalDimensions.height = 110;
  properties.physicalResolution.width = DISPLAY_WIDTH;
  properties.physicalResolution.height = DISPLAY_HEIGHT;
  properties.supportedTransforms = 1;
  return properties;
}

static VkDisplayModePropertiesCompat make_display_mode_properties(void) {
  VkDisplayModePropertiesCompat properties = {0};
  properties.displayMode = fake_display_mode;
  properties.parameters.visibleRegion.width = DISPLAY_WIDTH;
  properties.parameters.visibleRegion.height = DISPLAY_HEIGHT;
  properties.parameters.refreshRate = 60000;
  return properties;
}

static VkDisplayPlaneCapabilitiesCompat make_plane_capabilities(void) {
  VkDisplayPlaneCapabilitiesCompat capabilities = {0};
  capabilities.supportedAlpha = 1;
  capabilities.minSrcExtent.width = 1;
  capabilities.minSrcExtent.height = 1;
  capabilities.maxSrcExtent.width = DISPLAY_WIDTH;
  capabilities.maxSrcExtent.height = DISPLAY_HEIGHT;
  capabilities.minDstExtent.width = 1;
  capabilities.minDstExtent.height = 1;
  capabilities.maxDstExtent.width = DISPLAY_WIDTH;
  capabilities.maxDstExtent.height = DISPLAY_HEIGHT;
  return capabilities;
}

static int32_t fake_display_properties(void *physical_device,
                                      uint32_t *count, void *properties) {
  (void)physical_device;
  if (!count) return -3;
  if (!properties) {
    *count = 1;
    return 0;
  }
  if (*count < 1) {
    *count = 0;
    return 5;
  }
  *(VkDisplayPropertiesCompat *)properties = make_display_properties();
  *count = 1;
  return 0;
}

static int32_t fake_display_properties2(void *physical_device,
                                        uint32_t *count, void *properties) {
  (void)physical_device;
  if (!count) return -3;
  if (!properties) {
    *count = 1;
    return 0;
  }
  if (*count < 1) {
    *count = 0;
    return 5;
  }
  ((VkDisplayProperties2Compat *)properties)[0].displayProperties =
      make_display_properties();
  *count = 1;
  return 0;
}

static int32_t fake_plane_properties(void *physical_device, uint32_t *count,
                                     void *properties) {
  (void)physical_device;
  if (!count) return -3;
  if (!properties) {
    *count = 1;
    return 0;
  }
  if (*count < 1) {
    *count = 0;
    return 5;
  }
  VkDisplayPlanePropertiesCompat plane = {fake_display, 0};
  *(VkDisplayPlanePropertiesCompat *)properties = plane;
  *count = 1;
  return 0;
}

static int32_t fake_plane_properties2(void *physical_device, uint32_t *count,
                                      void *properties) {
  (void)physical_device;
  if (!count) return -3;
  if (!properties) {
    *count = 1;
    return 0;
  }
  if (*count < 1) {
    *count = 0;
    return 5;
  }
  VkDisplayPlanePropertiesCompat plane = {fake_display, 0};
  ((VkDisplayPlaneProperties2Compat *)properties)[0].displayPlaneProperties =
      plane;
  *count = 1;
  return 0;
}

static int32_t fake_supported_displays(void *physical_device, uint32_t plane,
                                       uint32_t *count, void *displays) {
  (void)physical_device;
  if (!count) return -3;
  if (plane != 0) {
    *count = 0;
    return 0;
  }
  if (!displays) {
    *count = 1;
    return 0;
  }
  if (*count < 1) {
    *count = 0;
    return 5;
  }
  ((void **)displays)[0] = fake_display;
  *count = 1;
  return 0;
}

static int32_t fake_display_modes(void *physical_device, void *display,
                                  uint32_t *count, void *properties) {
  (void)physical_device;
  if (!count) return -3;
  if (display != fake_display) {
    *count = 0;
    return 0;
  }
  if (!properties) {
    *count = 1;
    return 0;
  }
  if (*count < 1) {
    *count = 0;
    return 5;
  }
  *(VkDisplayModePropertiesCompat *)properties =
      make_display_mode_properties();
  *count = 1;
  return 0;
}

static int32_t fake_display_modes2(void *physical_device, void *display,
                                   uint32_t *count, void *properties) {
  (void)physical_device;
  if (!count) return -3;
  if (display != fake_display) {
    *count = 0;
    return 0;
  }
  if (!properties) {
    *count = 1;
    return 0;
  }
  if (*count < 1) {
    *count = 0;
    return 5;
  }
  ((VkDisplayModeProperties2Compat *)properties)[0].displayModeProperties =
      make_display_mode_properties();
  *count = 1;
  return 0;
}

static int32_t fake_plane_capabilities(void *physical_device, void *mode,
                                       uint32_t plane, void *capabilities) {
  (void)physical_device;
  if (!capabilities || mode != fake_display_mode || plane != 0) return -3;
  *(VkDisplayPlaneCapabilitiesCompat *)capabilities =
      make_plane_capabilities();
  return 0;
}

static int32_t fake_plane_capabilities2(void *physical_device,
                                        const VkDisplayPlaneInfo2Compat *info,
                                        VkDisplayPlaneCapabilities2Compat *out) {
  if (!info || !out) return -3;
  return fake_plane_capabilities(physical_device, info->displayMode,
                                 info->planeIndex, &out->capabilities);
}

static int32_t fake_create_display_mode(void *physical_device, void *display,
                                        const void *create_info,
                                        const void *allocator, void **mode) {
  (void)physical_device;
  (void)create_info;
  (void)allocator;
  if (display != fake_display || !mode) return -3;
  *mode = fake_display_mode;
  return 0;
}

static int32_t fake_acquire_display(void *physical_device, int32_t fd,
                                    void *display) {
  (void)physical_device;
  (void)fd;
  (void)display;
  return 0;
}

static int32_t fake_get_display(void *physical_device, int32_t fd,
                                uint32_t connector, void **display) {
  (void)physical_device;
  (void)fd;
  (void)connector;
  if (!display) return -3;
  *display = fake_display;
  return 0;
}

static int32_t fake_acquire_xlib_display(void *physical_device,
                                         XDisplay *display, void *vk_display) {
  (void)physical_device;
  (void)display;
  (void)vk_display;
  return 0;
}

static int32_t fake_get_randr_display(void *physical_device,
                                      XDisplay *display, XWindow output,
                                      void **vk_display) {
  (void)physical_device;
  (void)display;
  (void)output;
  if (!vk_display) return -3;
  *vk_display = fake_display;
  return 0;
}

static int32_t fake_release_display(void *physical_device, void *display) {
  (void)physical_device;
  (void)display;
  return 0;
}

static int32_t fake_display_power_control(void *device, void *display,
                                          const void *power_info) {
  (void)device;
  (void)display;
  (void)power_info;
  return 0;
}

static int32_t fake_register_display_event(void *device, void *display,
                                            const void *event_info,
                                            const void *allocator,
                                            void **fence) {
  (void)device;
  (void)display;
  (void)event_info;
  (void)allocator;
  if (fence) *fence = NULL;
  return -7;
}

int32_t vkCreateXlibSurfaceKHR(void *instance, const void *create_info,
                               const void *allocator, void **surface) {
  if (!create_info || !surface) return -3;

  const VkXlibSurfaceCreateInfoCompat *xlib_info = create_info;
  XDisplay *display = xlib_info->dpy ? xlib_info->dpy : x_display;
  XWindow window = xlib_info->window ? xlib_info->window : x_window;
  if (!display || !window) {
    shim_log("Xlib surface request has no display or window");
    return -7;
  }

  if (!real_dlsym) resolve_vulkan_loader();
  PFN_XGetXCBConnection get_xcb_connection =
      real_dlsym ? (PFN_XGetXCBConnection)real_dlsym(
                       RTLD_DEFAULT, "XGetXCBConnection")
                 : NULL;
  if (!get_xcb_connection && !x11_xcb_handle)
    x11_xcb_handle = dlopen("libX11-xcb.so.1", RTLD_NOW | RTLD_LOCAL);
  if (!get_xcb_connection && x11_xcb_handle && real_dlsym)
    get_xcb_connection = (PFN_XGetXCBConnection)real_dlsym(
        x11_xcb_handle, "XGetXCBConnection");
  if (!get_xcb_connection) {
    shim_log("XGetXCBConnection is unavailable");
    return -7;
  }

  xcb_connection_t *connection = get_xcb_connection(display);
  if (!connection) {
    shim_log("could not get the XCB connection for the Xlib display");
    return -7;
  }
  if (!real_get_instance_proc_addr) resolve_vulkan_loader();
  PFN_vkCreateXcbSurfaceKHR create_xcb_surface =
      real_get_instance_proc_addr
          ? (PFN_vkCreateXcbSurfaceKHR)real_get_instance_proc_addr(
                instance, "vkCreateXcbSurfaceKHR")
          : NULL;
  if (!create_xcb_surface) {
    shim_log("vkCreateXcbSurfaceKHR is unavailable");
    return -7;
  }

  VkXcbSurfaceCreateInfoCompat xcb_info = {0};
  xcb_info.sType = 1000005000u;
  xcb_info.pNext = xlib_info->pNext;
  xcb_info.flags = xlib_info->flags;
  xcb_info.connection = connection;
  xcb_info.window = (uint32_t)window;
  int32_t result =
      create_xcb_surface(instance, &xcb_info, allocator, surface);
  char message[128];
  snprintf(message, sizeof(message), "bridged Xlib surface through XCB result=%d",
           result);
  shim_log(message);
  return result;
}

static int32_t fake_create_display_surface(void *instance,
                                           const void *display_info,
                                           const void *allocator,
                                           void **surface) {
  (void)display_info;
  if (!surface || !real_get_instance_proc_addr) return -3;

  if (!x_display && real_dlsym) {
    PFN_XOpenDisplay open_display =
        (PFN_XOpenDisplay)real_dlsym(RTLD_DEFAULT, "XOpenDisplay");
    if (open_display) x_display = open_display(NULL);
  }
  if (!x_display) {
    shim_log("could not open the Gamescope X display");
    return -7;
  }

  if (!x_window && real_dlsym) {
    PFN_XDefaultRootWindow root_window =
        (PFN_XDefaultRootWindow)real_dlsym(RTLD_DEFAULT,
                                           "XDefaultRootWindow");
    PFN_XCreateSimpleWindow create_window =
        (PFN_XCreateSimpleWindow)real_dlsym(RTLD_DEFAULT,
                                           "XCreateSimpleWindow");
    if (!root_window || !create_window) {
      shim_log("Xlib root/window functions are unavailable");
      return -7;
    }
    x_window = create_window(x_display, root_window(x_display), 0, 0,
                             DISPLAY_WIDTH, DISPLAY_HEIGHT, 0, 0, 0);
    if (!x_window) {
      shim_log("could not create the Gamescope backing window");
      return -7;
    }
    PFN_XStoreName store_name =
        (PFN_XStoreName)real_dlsym(RTLD_DEFAULT, "XStoreName");
    PFN_XMapWindow map_window =
        (PFN_XMapWindow)real_dlsym(RTLD_DEFAULT, "XMapWindow");
    PFN_XFlush flush = (PFN_XFlush)real_dlsym(RTLD_DEFAULT, "XFlush");
    if (store_name) store_name(x_display, x_window,
                               "SteamVR virtual display");
    if (map_window) map_window(x_display, x_window);
    if (flush) flush(x_display);
    shim_log("created a 1280x720 Gamescope backing window");
  }

  VkXlibSurfaceCreateInfoCompat info = {0};
  info.sType = 1000004000u;
  info.dpy = x_display;
  info.window = x_window;
  return vkCreateXlibSurfaceKHR(instance, &info, allocator, surface);
}

static void *display_proc(const char *name) {
  if (!name) return NULL;
#define MATCH(symbol, function) \
  if (!strcmp(name, symbol)) return (void *)(uintptr_t)&function
  MATCH("vkGetPhysicalDeviceDisplayPropertiesKHR", fake_display_properties);
  MATCH("vkGetPhysicalDeviceDisplayProperties2KHR", fake_display_properties2);
  MATCH("vkGetPhysicalDeviceDisplayPlanePropertiesKHR", fake_plane_properties);
  MATCH("vkGetPhysicalDeviceDisplayPlaneProperties2KHR",
        fake_plane_properties2);
  MATCH("vkGetDisplayPlaneSupportedDisplaysKHR", fake_supported_displays);
  MATCH("vkGetDisplayModePropertiesKHR", fake_display_modes);
  MATCH("vkGetDisplayModeProperties2KHR", fake_display_modes2);
  MATCH("vkGetDisplayPlaneCapabilitiesKHR", fake_plane_capabilities);
  MATCH("vkGetDisplayPlaneCapabilities2KHR", fake_plane_capabilities2);
  MATCH("vkCreateDisplayModeKHR", fake_create_display_mode);
  MATCH("vkCreateDisplayPlaneSurfaceKHR", fake_create_display_surface);
  MATCH("vkCreateXlibSurfaceKHR", vkCreateXlibSurfaceKHR);
  MATCH("vkAcquireDrmDisplayEXT", fake_acquire_display);
  MATCH("vkGetDrmDisplayEXT", fake_get_display);
  MATCH("vkAcquireXlibDisplayEXT", fake_acquire_xlib_display);
  MATCH("vkGetRandROutputDisplayEXT", fake_get_randr_display);
  MATCH("vkReleaseDisplayEXT", fake_release_display);
  MATCH("vkDisplayPowerControlEXT", fake_display_power_control);
  MATCH("vkRegisterDisplayEventEXT", fake_register_display_event);
#undef MATCH
  return NULL;
}

static PFN_vkCreateInstance resolve_real_vk_create_instance(void) {
  if (real_create_instance == &vkCreateInstance)
    real_create_instance = NULL;
  if (real_get_instance_proc_addr == &vkGetInstanceProcAddr)
    real_get_instance_proc_addr = NULL;

  resolve_vulkan_loader();

  if (!real_create_instance && real_get_instance_proc_addr) {
    PFN_vkCreateInstance proc =
        (PFN_vkCreateInstance)real_get_instance_proc_addr(
            NULL, "vkCreateInstance");
    if (proc != &vkCreateInstance) real_create_instance = proc;
  }
  if (!real_create_instance && real_dlsym) {
    PFN_vkCreateInstance proc = (PFN_vkCreateInstance)real_dlsym(
        RTLD_NEXT, "vkCreateInstance");
    if (proc != &vkCreateInstance) real_create_instance = proc;
  }
  return real_create_instance;
}

int32_t vkCreateInstance(const void *create_info, const void *allocator,
                         void **instance) {
  PFN_vkCreateInstance create_instance = resolve_real_vk_create_instance();
  if (!create_instance) {
    shim_log("could not resolve the Vulkan loader's vkCreateInstance");
    return -3;
  }
  if (!create_info) {
    int32_t result = create_instance(create_info, allocator, instance);
    cache_vk_create_device(instance, result);
    return result;
  }

  const VkInstanceCreateInfoCompat *original = create_info;
  if (getenv("STEARLIGHT_VULKAN_TRACE")) {
    for (uint32_t i = 0; i < original->enabledExtensionCount; ++i) {
      char message[288];
      snprintf(message, sizeof(message), "instance requested %s", original->ppEnabledExtensionNames[i]);
      shim_log(message);
    }
  }
  const char *xlib_extension = "VK_KHR_xlib_surface";
  const char *xcb_extension = "VK_KHR_xcb_surface";
  const char *display_extension = "VK_KHR_display";
  int has_xcb_extension = 0;
  int stripped_xlib_extension = 0;
  int stripped_display_extension = 0;
  size_t retained_count = 0;
  for (uint32_t i = 0; i < original->enabledExtensionCount; ++i) {
    const char *extension = original->ppEnabledExtensionNames[i];
    if (!strcmp(extension, display_extension)) {
      stripped_display_extension = 1;
      continue;
    }
    if (!strcmp(extension, xlib_extension)) {
      stripped_xlib_extension = 1;
      continue;
    }
    if (!strcmp(extension, xcb_extension)) has_xcb_extension = 1;
    ++retained_count;
  }
  if (!stripped_display_extension && !stripped_xlib_extension &&
      has_xcb_extension) {
    int32_t result = create_instance(create_info, allocator, instance);
    cache_vk_create_device(instance, result);
    return result;
  }

  size_t count = retained_count + (has_xcb_extension ? 0 : 1);
  const char **extensions = calloc(count ? count : 1, sizeof(*extensions));
  if (!extensions) return -1;
  size_t next = 0;
  for (uint32_t i = 0; i < original->enabledExtensionCount; ++i) {
    const char *extension = original->ppEnabledExtensionNames[i];
    if (strcmp(extension, display_extension) &&
        strcmp(extension, xlib_extension))
      extensions[next++] = original->ppEnabledExtensionNames[i];
  }
  if (!has_xcb_extension) extensions[next++] = xcb_extension;
  VkInstanceCreateInfoCompat modified = *original;
  modified.enabledExtensionCount = (uint32_t)next;
  modified.ppEnabledExtensionNames = extensions;
  if (stripped_display_extension)
    shim_log("stripped synthetic VK_KHR_display before Vulkan loader creation");
  if (stripped_xlib_extension)
    shim_log("replaced VK_KHR_xlib_surface with VK_KHR_xcb_surface");
  if (!has_xcb_extension)
    shim_log("added VK_KHR_xcb_surface to the Vulkan instance");
  int32_t result = create_instance(&modified, allocator, instance);
  cache_vk_create_device(instance, result);
  char result_message[128];
  snprintf(result_message, sizeof(result_message),
           "Vulkan loader vkCreateInstance returned %d", result);
  shim_log(result_message);
  free(extensions);
  return result;
}

typedef struct {
  VkBaseInStructureCompat *node;
  const void *original_next;
} VkPNextRestoreCompat;

static int is_synthetic_present_feature(uint32_t s_type) {
  return (synthetic_present_id_available &&
          s_type ==
              VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR_COMPAT) ||
         (synthetic_present_wait_available &&
          s_type ==
              VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR_COMPAT);
}

static int is_synthetic_global_priority_structure(uint32_t s_type) {
  return synthetic_global_priority_available &&
         s_type == VK_STRUCTURE_TYPE_DEVICE_QUEUE_GLOBAL_PRIORITY_CREATE_INFO_EXT_COMPAT;
}

static int is_global_priority_structure(uint32_t s_type) {
  return s_type == 1000174000u;
}

static const void *strip_synthetic_pnext_structures(
    const void *chain, int (*is_synthetic)(uint32_t),
    VkPNextRestoreCompat *restore, size_t *restore_count) {
  const void *head = chain;
  const VkBaseInStructureCompat *previous = NULL;
  const VkBaseInStructureCompat *current = chain;
  size_t scanned = 0;
  while (current && scanned++ < 64) {
    const VkBaseInStructureCompat *next =
        (const VkBaseInStructureCompat *)current->pNext;
    if (is_synthetic(current->sType)) {
      if (!previous) {
        head = next;
      } else if (*restore_count < MAX_PNEXT_RESTORE) {
        VkBaseInStructureCompat *mutable_previous =
            (VkBaseInStructureCompat *)(uintptr_t)previous;
        restore[*restore_count].node = mutable_previous;
        restore[*restore_count].original_next = mutable_previous->pNext;
        mutable_previous->pNext = next;
        ++*restore_count;
      } else {
        previous = current;
      }
    } else {
      previous = current;
    }
    current = next;
  }
  return head;
}

static void restore_pnext(VkPNextRestoreCompat *restore,
                          size_t restore_count) {
  while (restore_count > 0) {
    --restore_count;
    restore[restore_count].node->pNext = restore[restore_count].original_next;
  }
}

int32_t vkCreateDevice(void *physical_device, const void *create_info,
                       const void *allocator, void **device) {
  const void *effective_create_info = create_info;
  VkDeviceCreateInfoCompat modified_info;
  VkDeviceQueueCreateInfoCompat *mapped_queue_infos = NULL;
  const char **filtered_extensions = NULL;
  VkExtensionPropertiesCompat *supported = NULL;
  VkPNextRestoreCompat pnext_restore[MAX_PNEXT_RESTORE];
  VkPNextRestoreCompat queue_pnext_restore[MAX_PNEXT_RESTORE];
  VkTimelineFeatureRestoreCompat timeline_feature_restore[MAX_PNEXT_RESTORE];
  size_t pnext_restore_count = 0;
  size_t queue_pnext_restore_count = 0;
  size_t timeline_feature_restore_count = 0;
  if (create_info && refresh_compute_queue_alias(physical_device)) {
    const VkDeviceCreateInfoCompat *info = create_info;
    const VkDeviceQueueCreateInfoCompat *queue_infos =
        info->pQueueCreateInfos;
    int requests_alias = 0;
    int requests_backing = 0;
    for (uint32_t i = 0; queue_infos &&
                         i < info->queueCreateInfoCount;
         ++i) {
      if (queue_infos[i].queueFamilyIndex == queue_alias_family)
        requests_alias = 1;
      if (queue_infos[i].queueFamilyIndex == queue_alias_backing_family)
        requests_backing = 1;
    }
    if (queue_infos && info->queueCreateInfoCount) {
      char message[128];
      snprintf(message, sizeof(message),
               "device queue requests=%u alias=%s backing=%s",
               info->queueCreateInfoCount, requests_alias ? "yes" : "no",
               requests_backing ? "yes" : "no");
      shim_log(message);
      uint32_t queue_info_capacity =
          info->queueCreateInfoCount + (requests_alias ? 0u : 1u);
      mapped_queue_infos =
          calloc(queue_info_capacity, sizeof(*mapped_queue_infos));
      if (mapped_queue_infos) {
        uint32_t mapped_count = 0;
        for (uint32_t i = 0; i < info->queueCreateInfoCount; ++i) {
          mapped_queue_infos[mapped_count] = queue_infos[i];
          if (synthetic_global_priority_available) {
            mapped_queue_infos[mapped_count].pNext =
                strip_synthetic_pnext_structures(
                    queue_infos[i].pNext,
                    is_synthetic_global_priority_structure,
                    queue_pnext_restore, &queue_pnext_restore_count);
          }
          ++mapped_count;
        }
        if (!requests_alias) {
          for (uint32_t i = 0; i < info->queueCreateInfoCount; ++i) {
            if (queue_infos[i].flags != 0 || queue_infos[i].queueCount == 0 ||
                !queue_infos[i].pQueuePriorities)
              continue;
            VkDeviceQueueCreateInfoCompat alias = mapped_queue_infos[i];
            alias.queueFamilyIndex = queue_alias_family;
            alias.queueCount = 1;
            alias.pQueuePriorities = queue_infos[i].pQueuePriorities;
            mapped_queue_infos[mapped_count++] = alias;
            requests_alias = 1;
            shim_log("added a synthetic compute queue request for SteamVR");
            break;
          }
        }
        if (requests_alias) {
          uint32_t retained_count = 0;
          for (uint32_t i = 0; i < mapped_count; ++i) {
            VkDeviceQueueCreateInfoCompat item = mapped_queue_infos[i];
            if (item.queueFamilyIndex == queue_alias_family) {
              if (requests_backing) continue;
              item.queueFamilyIndex = queue_alias_backing_family;
            }
            mapped_queue_infos[retained_count++] = item;
          }
          mapped_count = retained_count;
          modified_info = *info;
          modified_info.queueCreateInfoCount = mapped_count;
          modified_info.pQueueCreateInfos = mapped_queue_infos;
          effective_create_info = &modified_info;
          shim_log("mapped synthetic compute queue creation to the combined backing queue");
        }
      }
    }
  }
  if (create_info && active_vulkan_instance && real_get_instance_proc_addr) {
    const VkDeviceCreateInfoCompat *info = effective_create_info;
    PFN_vkEnumerateDeviceExtensionProperties enumerate =
        real_enumerate_device_extensions;
    if (!enumerate && real_get_instance_proc_addr) {
      enumerate = (PFN_vkEnumerateDeviceExtensionProperties)
          real_get_instance_proc_addr(
              active_vulkan_instance, "vkEnumerateDeviceExtensionProperties");
      if (enumerate == &vkEnumerateDeviceExtensionProperties) enumerate = NULL;
    }
    uint32_t supported_count = 0;
    int32_t enum_result =
        enumerate ? enumerate(physical_device, NULL, &supported_count, NULL)
                  : -3;
    if (enum_result == 0 && supported_count) {
      supported = calloc(supported_count, sizeof(*supported));
      if (supported)
        enum_result = enumerate(physical_device, NULL, &supported_count,
                                supported);
    }
    if (enum_result == 0 &&
        (supported || supported_count == 0) &&
        info->enabledExtensionCount && info->ppEnabledExtensionNames) {
      filtered_extensions =
          calloc(info->enabledExtensionCount, sizeof(*filtered_extensions));
      if (filtered_extensions) {
        uint32_t retained_count = 0;
        for (uint32_t i = 0; i < info->enabledExtensionCount; ++i) {
          const char *name = info->ppEnabledExtensionNames[i];
          int found = 0;
          for (uint32_t j = 0; j < supported_count; ++j)
            if (!strcmp(name, supported[j].extensionName)) found = 1;
          if (found && !device_extension_hidden(name)) {
            filtered_extensions[retained_count++] = name;
          } else {
            char message[320];
            snprintf(message, sizeof(message),
                     "omitting masked or unsupported Vulkan device extension %s",
                     name);
            shim_log(message);
          }
        }
        if (retained_count != info->enabledExtensionCount) {
          modified_info = *info;
          modified_info.enabledExtensionCount = retained_count;
          modified_info.ppEnabledExtensionNames = filtered_extensions;
          effective_create_info = &modified_info;
          shim_log("omitted unsupported Vulkan device extensions");
        }
      }
    }
  }
  free(supported);
  PFN_vkCreateDevice create_device = real_create_device;
  if (!create_device || create_device == &vkCreateDevice) {
    shim_log("real vkCreateDevice is unavailable");
    free(filtered_extensions);
    free(mapped_queue_infos);
    return -3;
  }
  if (effective_create_info) {
    const VkDeviceCreateInfoCompat *info = effective_create_info;
    const void *filtered_chain = strip_synthetic_pnext_structures(
        info->pNext, is_synthetic_present_feature,
        pnext_restore, &pnext_restore_count);
    if (filtered_chain != info->pNext) {
      if (effective_create_info == create_info) modified_info = *info;
      modified_info.pNext = filtered_chain;
      effective_create_info = &modified_info;
      shim_log("stripped synthetic present features before driver creation");
    }
    info = effective_create_info;
    mask_timeline_semaphore_features(
        (void *)(uintptr_t)info->pNext, timeline_feature_restore,
        &timeline_feature_restore_count);
  }
  int32_t result = create_device(physical_device, effective_create_info,
                                 allocator, device);
  if (result == -1000174001 && timeline_bridge_enabled() && effective_create_info) {
    const VkDeviceCreateInfoCompat *info = effective_create_info;
    VkDeviceQueueCreateInfoCompat *queues = calloc(info->queueCreateInfoCount, sizeof(*queues));
    VkPNextRestoreCompat priority_restore[MAX_PNEXT_RESTORE];
    size_t priority_restore_count = 0;
    int changed = 0;
    if (queues) {
      for (uint32_t i = 0; i < info->queueCreateInfoCount; i++) {
        queues[i] = ((const VkDeviceQueueCreateInfoCompat *)info->pQueueCreateInfos)[i];
        const VkBaseInStructureCompat *node = queues[i].pNext;
        while (node) {
          if (is_global_priority_structure(node->sType)) changed = 1;
          node = node->pNext;
        }
        queues[i].pNext = strip_synthetic_pnext_structures(queues[i].pNext,
            is_global_priority_structure, priority_restore, &priority_restore_count);
      }
      if (changed) {
        /* Keep the real compute queue when Windows refuses privileged priority. */
        VkDeviceCreateInfoCompat retry = *info;
        retry.pQueueCreateInfos = queues;
        shim_log("retrying real graphics/compute queues at normal priority");
        result = create_device(physical_device, &retry, allocator, device);
      }
      restore_pnext(priority_restore, priority_restore_count);
      free(queues);
    }
  }
  if (result == 0 && device && *device) active_vulkan_device = *device;
  restore_timeline_semaphore_features(timeline_feature_restore,
                                      timeline_feature_restore_count);
  restore_pnext(queue_pnext_restore, queue_pnext_restore_count);
  restore_pnext(pnext_restore, pnext_restore_count);
  char message[96];
  snprintf(message, sizeof(message), "Vulkan loader vkCreateDevice returned %d",
           result);
  shim_log(message);
  free(filtered_extensions);
  free(mapped_queue_infos);
  return result;
}

void *dlsym(void *handle, const char *name) {
  if (!real_dlsym) {
    real_dlsym = (PFN_dlsym)dlvsym(RTLD_NEXT, "dlsym", "GLIBC_2.34");
    if (!real_dlsym)
      real_dlsym = (PFN_dlsym)dlvsym(RTLD_NEXT, "dlsym", "GLIBC_2.2.5");
  }
  if (!real_dlsym) return NULL;

  void *proc = real_dlsym(handle, name);
  void *queue_api = wrap_queue_api(name, proc);
  if (queue_api) {
    if (!strcmp(name, "vkQueuePresentKHR") &&
        proc != (void *)(uintptr_t)&vkQueuePresentKHR)
      real_queue_present = (PFN_vkQueuePresentKHR)proc;
    return queue_api;
  }
  void *semaphore_fd_api = wrap_semaphore_fd_api(name, proc);
  if (semaphore_fd_api) return semaphore_fd_api;
  void *semaphore_create_api = wrap_semaphore_create_api(name, proc);
  if (semaphore_create_api) return semaphore_create_api;
  if (!strcmp(name, "vkEnumerateInstanceExtensionProperties")) {
    PFN_vkEnumerateInstanceExtensionProperties enumerate_extensions =
        (PFN_vkEnumerateInstanceExtensionProperties)proc;
    if (enumerate_extensions !=
        &vkEnumerateInstanceExtensionProperties)
      real_enumerate_instance_extensions = enumerate_extensions;
    else
      resolve_vulkan_loader();
    if (!real_enumerate_instance_extensions) return proc;
    return (void *)(uintptr_t)&vkEnumerateInstanceExtensionProperties;
  }
  if (!strcmp(name, "vkEnumerateDeviceExtensionProperties")) {
    PFN_vkEnumerateDeviceExtensionProperties enumerate_extensions =
        (PFN_vkEnumerateDeviceExtensionProperties)proc;
    if (enumerate_extensions != &vkEnumerateDeviceExtensionProperties)
      real_enumerate_device_extensions = enumerate_extensions;
    else
      resolve_vulkan_loader();
    if (!real_enumerate_device_extensions) return proc;
    return (void *)(uintptr_t)&vkEnumerateDeviceExtensionProperties;
  }
  if (!strcmp(name, "vkGetPhysicalDeviceFeatures2") ||
      !strcmp(name, "vkGetPhysicalDeviceFeatures2KHR")) {
    PFN_vkGetPhysicalDeviceFeatures2 get_features2 =
        (PFN_vkGetPhysicalDeviceFeatures2)proc;
    if (get_features2 != &vkGetPhysicalDeviceFeatures2 &&
        get_features2 != &vkGetPhysicalDeviceFeatures2KHR)
      real_get_physical_device_features2 = get_features2;
    else
      resolve_vulkan_loader();
    return (void *)(uintptr_t)(!strcmp(name, "vkGetPhysicalDeviceFeatures2KHR")
                                   ? &vkGetPhysicalDeviceFeatures2KHR
                                   : &vkGetPhysicalDeviceFeatures2);
  }
  if (!strcmp(name, "vkGetPhysicalDeviceQueueFamilyProperties")) {
    shim_log("dlsym intercepted vkGetPhysicalDeviceQueueFamilyProperties");
    real_get_queue_family_properties =
        (PFN_vkGetPhysicalDeviceQueueFamilyProperties)proc;
    if (real_get_queue_family_properties ==
        &vkGetPhysicalDeviceQueueFamilyProperties)
      real_get_queue_family_properties = NULL;
    return resolve_queue_family_properties(active_vulkan_instance)
               ? (void *)(uintptr_t)&vkGetPhysicalDeviceQueueFamilyProperties
               : NULL;
  }
  if (!strcmp(name, "vkGetPhysicalDeviceQueueFamilyProperties2") ||
      !strcmp(name, "vkGetPhysicalDeviceQueueFamilyProperties2KHR")) {
    shim_log("dlsym intercepted queue-family-properties2");
    real_get_queue_family_properties2 =
        (PFN_vkGetPhysicalDeviceQueueFamilyProperties2)proc;
    if (real_get_queue_family_properties2 ==
            &vkGetPhysicalDeviceQueueFamilyProperties2 ||
        real_get_queue_family_properties2 ==
            &vkGetPhysicalDeviceQueueFamilyProperties2KHR)
      real_get_queue_family_properties2 = NULL;
    if (resolve_queue_family_properties2(
            active_vulkan_instance, name))
      return (void *)(uintptr_t)(
          !strcmp(name, "vkGetPhysicalDeviceQueueFamilyProperties2KHR")
              ? &vkGetPhysicalDeviceQueueFamilyProperties2KHR
              : &vkGetPhysicalDeviceQueueFamilyProperties2);
    return proc;
  }
  if (!strcmp(name, "vkGetInstanceProcAddr")) {
    PFN_vkGetInstanceProcAddr gipa = (PFN_vkGetInstanceProcAddr)proc;
    real_get_instance_proc_addr =
        gipa == &vkGetInstanceProcAddr ? NULL : gipa;
    resolve_vulkan_loader();
    shim_log("dlsym intercepted vkGetInstanceProcAddr");
    return (void *)(uintptr_t)&vkGetInstanceProcAddr;
  }
  if (!strcmp(name, "vkGetDeviceProcAddr")) {
    real_get_device_proc_addr = (PFN_vkGetDeviceProcAddr)proc;
    if (real_get_device_proc_addr == &vkGetDeviceProcAddr)
      real_get_device_proc_addr = NULL;
    (void)resolve_real_vk_get_device_proc_addr(active_vulkan_instance);
    return (void *)(uintptr_t)&vkGetDeviceProcAddr;
  }
  if (!strcmp(name, "vkGetDeviceQueue")) {
    real_get_device_queue = (PFN_vkGetDeviceQueue)proc;
    if (real_get_device_queue == &vkGetDeviceQueue)
      real_get_device_queue = NULL;
    return resolve_real_vk_get_device_queue(NULL)
               ? (void *)(uintptr_t)&vkGetDeviceQueue
               : NULL;
  }
  if (!strcmp(name, "vkGetDeviceQueue2")) {
    real_get_device_queue2 = (PFN_vkGetDeviceQueue2)proc;
    if (real_get_device_queue2 == &vkGetDeviceQueue2)
      real_get_device_queue2 = NULL;
    return resolve_real_vk_get_device_queue2(NULL)
               ? (void *)(uintptr_t)&vkGetDeviceQueue2
               : NULL;
  }
  if (!strcmp(name, "vkCreateCommandPool")) {
    real_create_command_pool = (PFN_vkCreateCommandPool)proc;
    if (real_create_command_pool == &vkCreateCommandPool)
      real_create_command_pool = NULL;
    return resolve_real_vk_create_command_pool(NULL)
               ? (void *)(uintptr_t)&vkCreateCommandPool
               : NULL;
  }
  if (!strcmp(name, "vkQueuePresentKHR") && synthetic_present_id_available) {
    real_queue_present = (PFN_vkQueuePresentKHR)proc;
    if (real_queue_present == &vkQueuePresentKHR) real_queue_present = NULL;
    return resolve_real_vk_queue_present(NULL)
               ? (void *)(uintptr_t)&vkQueuePresentKHR
               : NULL;
  }
  if (!strcmp(name, "vkWaitForPresentKHR"))
    return (void *)(uintptr_t)&vkWaitForPresentKHR;
  if (!strcmp(name, "vkCreateInstance")) {
    PFN_vkCreateInstance create_instance = (PFN_vkCreateInstance)proc;
    if (create_instance != &vkCreateInstance)
      real_create_instance = create_instance;
    else
      resolve_vulkan_loader();
    return (void *)(uintptr_t)&vkCreateInstance;
  }
  if (!strcmp(name, "vkCreateDevice")) {
    real_create_device = (PFN_vkCreateDevice)proc;
    if (real_create_device == &vkCreateDevice) real_create_device = NULL;
    if (!real_create_device) (void)resolve_real_vk_create_device(NULL);
    if (!real_create_device) return proc == &vkCreateDevice ? NULL : proc;
    return (void *)(uintptr_t)&vkCreateDevice;
  }
  void *display = display_proc(name);
  if (display) {
    char message[160];
    snprintf(message, sizeof(message), "emulated %s", name);
    shim_log(message);
  }
  return display ? display : proc;
}

void *vkGetInstanceProcAddr(void *instance, const char *name) {
  if (name && strstr(name, "Display")) {
    char message[160];
    snprintf(message, sizeof(message), "gipa %s", name);
    shim_log(message);
  }
  if (name && !strcmp(name, "vkEnumerateInstanceExtensionProperties")) {
    if (!real_enumerate_instance_extensions &&
        real_get_instance_proc_addr) {
      PFN_vkEnumerateInstanceExtensionProperties enumerate_extensions =
          (PFN_vkEnumerateInstanceExtensionProperties)
              real_get_instance_proc_addr(instance, name);
      if (enumerate_extensions !=
          &vkEnumerateInstanceExtensionProperties)
        real_enumerate_instance_extensions = enumerate_extensions;
    }
    if (!real_enumerate_instance_extensions) return NULL;
    return (void *)(uintptr_t)&vkEnumerateInstanceExtensionProperties;
  }
  if (name && !strcmp(name, "vkEnumerateDeviceExtensionProperties")) {
    shim_log("intercepted vkGetInstanceProcAddr for device extension enumeration");
    if (!real_enumerate_device_extensions && real_get_instance_proc_addr) {
      PFN_vkEnumerateDeviceExtensionProperties enumerate_extensions =
          (PFN_vkEnumerateDeviceExtensionProperties)
              real_get_instance_proc_addr(instance, name);
      if (enumerate_extensions != &vkEnumerateDeviceExtensionProperties)
        real_enumerate_device_extensions = enumerate_extensions;
    }
    if (!real_enumerate_device_extensions) return NULL;
    return (void *)(uintptr_t)&vkEnumerateDeviceExtensionProperties;
  }
  if (name && (!strcmp(name, "vkGetPhysicalDeviceFeatures2") ||
               !strcmp(name, "vkGetPhysicalDeviceFeatures2KHR"))) {
    if (!real_get_physical_device_features2 && real_get_instance_proc_addr) {
      PFN_vkGetPhysicalDeviceFeatures2 get_features2 =
          (PFN_vkGetPhysicalDeviceFeatures2)
              real_get_instance_proc_addr(instance, name);
      if (!get_features2)
        get_features2 = (PFN_vkGetPhysicalDeviceFeatures2)
            real_get_instance_proc_addr(
                instance, !strcmp(name, "vkGetPhysicalDeviceFeatures2")
                              ? "vkGetPhysicalDeviceFeatures2KHR"
                              : "vkGetPhysicalDeviceFeatures2");
      if (get_features2 &&
          get_features2 != &vkGetPhysicalDeviceFeatures2 &&
          get_features2 != &vkGetPhysicalDeviceFeatures2KHR)
        real_get_physical_device_features2 = get_features2;
    }
    if (!real_get_physical_device_features2) return NULL;
    return (void *)(uintptr_t)(!strcmp(name, "vkGetPhysicalDeviceFeatures2KHR")
                                   ? &vkGetPhysicalDeviceFeatures2KHR
                                   : &vkGetPhysicalDeviceFeatures2);
  }
  if (name &&
      !strcmp(name, "vkGetPhysicalDeviceQueueFamilyProperties")) {
    shim_log("gipa vkGetPhysicalDeviceQueueFamilyProperties");
    if (!resolve_queue_family_properties(instance))
      return NULL;
    return (void *)(uintptr_t)&vkGetPhysicalDeviceQueueFamilyProperties;
  }
  if (name &&
      (!strcmp(name, "vkGetPhysicalDeviceQueueFamilyProperties2") ||
       !strcmp(name, "vkGetPhysicalDeviceQueueFamilyProperties2KHR"))) {
    shim_log("gipa queue-family-properties2");
    if (!resolve_queue_family_properties2(instance, name)) return NULL;
    return (void *)(uintptr_t)(
        !strcmp(name, "vkGetPhysicalDeviceQueueFamilyProperties2KHR")
            ? &vkGetPhysicalDeviceQueueFamilyProperties2KHR
            : &vkGetPhysicalDeviceQueueFamilyProperties2);
  }
  if (name && !strcmp(name, "vkCreateInstance")) {
    if (!real_create_instance && real_get_instance_proc_addr)
      real_create_instance = (PFN_vkCreateInstance)real_get_instance_proc_addr(
          NULL, name);
    return (void *)(uintptr_t)&vkCreateInstance;
  }
  if (name && !strcmp(name, "vkCreateDevice")) {
    (void)resolve_real_vk_create_device(instance);
    shim_log(real_create_device ? "gipa vkCreateDevice proc=yes"
                                : "gipa vkCreateDevice proc=no");
    if (!real_create_device) return NULL;
    return (void *)(uintptr_t)&vkCreateDevice;
  }
  if (name && !strcmp(name, "vkGetDeviceProcAddr")) {
    (void)resolve_real_vk_get_device_proc_addr(instance);
    return (void *)(uintptr_t)&vkGetDeviceProcAddr;
  }
  if (name && !strcmp(name, "vkQueuePresentKHR") &&
      (synthetic_present_id_available || queue_alias_available)) {
    return resolve_real_vk_queue_present(NULL)
               ? (void *)(uintptr_t)&vkQueuePresentKHR
               : NULL;
  }
  if (name && !strcmp(name, "vkWaitForPresentKHR"))
    return (void *)(uintptr_t)&vkWaitForPresentKHR;
  void *proc = display_proc(name);
  if (proc) {
    char message[160];
    snprintf(message, sizeof(message), "emulated %s", name);
    shim_log(message);
    return proc;
  }
  if (!real_get_instance_proc_addr) return NULL;
  proc = real_get_instance_proc_addr(instance, name);
  void *queue_api = wrap_queue_api(name, proc);
  if (queue_api) return queue_api;
  void *semaphore_fd_api = wrap_semaphore_fd_api(name, proc);
  if (semaphore_fd_api) return semaphore_fd_api;
  void *semaphore_create_api = wrap_semaphore_create_api(name, proc);
  return semaphore_create_api ? semaphore_create_api : proc;
}

void *vkGetDeviceProcAddr(void *device, const char *name) {
  if (name && !strcmp(name, "vkWaitForPresentKHR")) {
    if (synthetic_present_wait_available || getenv("STEARLIGHT_VULKAN_TRACE"))
      return (void *)(uintptr_t)&vkWaitForPresentKHR;
    if (resolve_real_vk_get_device_proc_addr(active_vulkan_instance)) {
      void *proc = real_get_device_proc_addr(device, name);
      if (proc && proc != (void *)(uintptr_t)&vkWaitForPresentKHR) return proc;
    }
    return NULL;
  }
  if (name && !strcmp(name, "vkGetDeviceQueue")) {
    if (!resolve_real_vk_get_device_queue(device))
      return NULL;
    return (void *)(uintptr_t)&vkGetDeviceQueue;
  }
  if (name && !strcmp(name, "vkGetDeviceQueue2")) {
    if (!resolve_real_vk_get_device_queue2(device))
      return NULL;
    return (void *)(uintptr_t)&vkGetDeviceQueue2;
  }
  if (name && !strcmp(name, "vkCreateCommandPool")) {
    if (!resolve_real_vk_create_command_pool(device))
      return NULL;
    return (void *)(uintptr_t)&vkCreateCommandPool;
  }
  if (name && !strcmp(name, "vkQueuePresentKHR") &&
      (synthetic_present_id_available || queue_alias_available)) {
    if (resolve_real_vk_queue_present(device))
      return (void *)(uintptr_t)&vkQueuePresentKHR;
    return NULL;
  }
  void *proc = display_proc(name);
  if (proc) return proc;
  if (!resolve_real_vk_get_device_proc_addr(active_vulkan_instance))
    return NULL;
  proc = real_get_device_proc_addr(device, name);
  void *queue_api = wrap_queue_api(name, proc);
  if (queue_api) return queue_api;
  void *semaphore_fd_api = wrap_semaphore_fd_api(name, proc);
  if (semaphore_fd_api) return semaphore_fd_api;
  void *semaphore_create_api = wrap_semaphore_create_api(name, proc);
  return semaphore_create_api ? semaphore_create_api : proc;
}

static void remove_shim_from_child_preloads(void) {
  const char *preload = getenv("LD_PRELOAD");
  if (!preload || !*preload) return;
  const char *shim = getenv("STEARLIGHT_VULKAN_WSI_SHIM");
  if (!shim || !*shim)
    shim = "/opt/steamvr/lib/linuxarm64/libstearlight-vulkan-wsi-shim.so";
  char *tokens = strdup(preload);
  char *remaining = calloc(strlen(preload) + 1, 1);
  if (!tokens || !remaining) {
    free(tokens);
    free(remaining);
    return;
  }
  char *save = NULL;
  for (char *token = strtok_r(tokens, ": ", &save); token;
       token = strtok_r(NULL, ": ", &save)) {
    if (!strcmp(token, shim)) continue;
    if (*remaining) strcat(remaining, ":");
    strcat(remaining, token);
  }
  if (*remaining)
    setenv("LD_PRELOAD", remaining, 1);
  else
    unsetenv("LD_PRELOAD");
  free(tokens);
  free(remaining);
}

__attribute__((constructor)) static void stearlight_wsi_shim_init(void) {
  // The library stays loaded here. Child launchers must not inherit this
  // glibc preload: Alpine's musl timeout/xset cannot resolve dlvsym, and CEF
  // must use its own Vulkan entry points.
  remove_shim_from_child_preloads();
  if (program_invocation_short_name &&
      (!strcmp(program_invocation_short_name, "vrcompositor") ||
       !strcmp(program_invocation_short_name, "vrcompositor.bin")))
    prctl(PR_SET_NAME, "vrcompositor", 0, 0, 0);
  real_dlsym = (PFN_dlsym)dlvsym(RTLD_NEXT, "dlsym", "GLIBC_2.34");
  if (!real_dlsym)
    real_dlsym = (PFN_dlsym)dlvsym(RTLD_NEXT, "dlsym", "GLIBC_2.2.5");
  if (real_dlsym) {
    real_get_instance_proc_addr =
        (PFN_vkGetInstanceProcAddr)real_dlsym(RTLD_NEXT,
                                              "vkGetInstanceProcAddr");
    real_get_device_proc_addr =
        (PFN_vkGetDeviceProcAddr)real_dlsym(RTLD_NEXT,
                                            "vkGetDeviceProcAddr");
  }
  char message[160];
  snprintf(message, sizeof(message), "loaded dlsym=%s gipa=%s gdpa=%s",
           real_dlsym ? "yes" : "no",
           real_get_instance_proc_addr ? "yes" : "no",
           real_get_device_proc_addr ? "yes" : "no");
  shim_log(message);
}
