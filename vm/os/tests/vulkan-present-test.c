#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define CHECK(call) do { VkResult r = (call); if (r != VK_SUCCESS) { \
  fprintf(stderr, "%s failed: %d\n", #call, r); exit(1); } } while (0)
#define REQUIRE(test) do { if (!(test)) { fprintf(stderr, "failed: %s\n", #test); exit(1); } } while (0)
#define INSTANCE(name) ((PFN_vk##name)gipa(instance, "vk" #name))
#define DEVICE(name) ((PFN_vk##name)gdpa(device, "vk" #name))

/* Avoid an X11 development dependency in the cross SDK. */
typedef struct {
  VkStructureType sType;
  const void *pNext;
  VkFlags flags;
  void *connection;
  uint32_t window;
} XcbSurfaceInfo;
typedef VkResult (*CreateXcbSurface)(VkInstance, const XcbSurfaceInfo *,
                                   const VkAllocationCallbacks *, VkSurfaceKHR *);

int main(void) {
  setbuf(stdout, NULL);
  void *xlib = dlopen("libX11.so.6", RTLD_NOW | RTLD_LOCAL);
  void *xcb = dlopen("libX11-xcb.so.1", RTLD_NOW | RTLD_LOCAL);
  REQUIRE(xlib && xcb);
  void *(*open_display)(const char *) = dlsym(xlib, "XOpenDisplay");
  unsigned long (*root_window)(void *) = dlsym(xlib, "XDefaultRootWindow");
  unsigned long (*create_window)(void *, unsigned long, int, int, unsigned,
    unsigned, unsigned, unsigned long, unsigned long) = dlsym(xlib, "XCreateSimpleWindow");
  int (*map_window)(void *, unsigned long) = dlsym(xlib, "XMapWindow");
  int (*store_name)(void *, unsigned long, const char *) = dlsym(xlib, "XStoreName");
  int (*sync_display)(void *, int) = dlsym(xlib, "XSync");
  void *(*get_connection)(void *) = dlsym(xcb, "XGetXCBConnection");
  void *(*get_image)(void *, unsigned long, int, int, unsigned, unsigned,
    unsigned long, int) = dlsym(xlib, "XGetImage");
  unsigned long (*get_pixel)(void *, int, int) = dlsym(xlib, "XGetPixel");
  int (*destroy_image)(void *) = dlsym(xlib, "XDestroyImage");
  REQUIRE(open_display && root_window && create_window && map_window && store_name && sync_display && get_connection);
  REQUIRE(get_image && get_pixel && destroy_image);
  void *display = open_display(NULL);
  REQUIRE(display);
  unsigned long window = create_window(display, root_window(display), 0, 0, 640, 360, 0, 0, 0);
  store_name(display, window, "Stearlight Vulkan presentation test");
  map_window(display, window);
  sync_display(display, 0);

  void *loader = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
  REQUIRE(loader);
  PFN_vkGetInstanceProcAddr gipa = (PFN_vkGetInstanceProcAddr)dlsym(loader, "vkGetInstanceProcAddr");
  REQUIRE(gipa);
  VkInstance instance;
  const char *instance_extensions[] = {"VK_KHR_surface", "VK_KHR_xcb_surface"};
  VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_2};
  VkInstanceCreateInfo ici = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app,
    .enabledExtensionCount = 2, .ppEnabledExtensionNames = instance_extensions};
  CHECK(((PFN_vkCreateInstance)gipa(NULL, "vkCreateInstance"))(&ici, NULL, &instance));
  VkPhysicalDevice physical;
  uint32_t count = 1;
  CHECK(INSTANCE(EnumeratePhysicalDevices)(instance, &count, &physical));
  VkSurfaceKHR surface;
  XcbSurfaceInfo si = {.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR,
    .connection = get_connection(display), .window = (uint32_t)window};
  CHECK(((CreateXcbSurface)gipa(instance, "vkCreateXcbSurfaceKHR"))(instance, &si, NULL, &surface));
  float priority = 1;
  VkDeviceQueueCreateInfo qi = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
    .queueFamilyIndex = 0, .queueCount = 1, .pQueuePriorities = &priority};
  const char *extensions[] = {"VK_KHR_swapchain", "VK_KHR_present_id", "VK_KHR_present_wait"};
  VkPhysicalDevicePresentWaitFeaturesKHR wait_feature = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR, .presentWait = VK_TRUE};
  VkPhysicalDevicePresentIdFeaturesKHR id_feature = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR,
    .pNext = &wait_feature, .presentId = VK_TRUE};
  VkDeviceCreateInfo di = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .pNext = &id_feature,
    .queueCreateInfoCount = 1, .pQueueCreateInfos = &qi, .enabledExtensionCount = 3, .ppEnabledExtensionNames = extensions};
  VkDevice device;
  CHECK(INSTANCE(CreateDevice)(physical, &di, NULL, &device));
  PFN_vkGetDeviceProcAddr gdpa = (PFN_vkGetDeviceProcAddr)gipa(instance, "vkGetDeviceProcAddr");
  VkQueue queue;
  DEVICE(GetDeviceQueue)(device, 0, 0, &queue);
  VkSurfaceCapabilitiesKHR caps;
  CHECK(INSTANCE(GetPhysicalDeviceSurfaceCapabilitiesKHR)(physical, surface, &caps));
  VkSwapchainCreateInfoKHR sci = {.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
    .surface = surface, .minImageCount = caps.minImageCount, .imageFormat = VK_FORMAT_B8G8R8A8_UNORM,
    .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, .imageExtent = {640, 360}, .imageArrayLayers = 1,
    .imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT, .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
    .preTransform = caps.currentTransform, .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
    .presentMode = VK_PRESENT_MODE_FIFO_KHR, .clipped = VK_TRUE};
  VkSwapchainKHR chain;
  CHECK(DEVICE(CreateSwapchainKHR)(device, &sci, NULL, &chain));
  count = 0;
  CHECK(DEVICE(GetSwapchainImagesKHR)(device, chain, &count, NULL));
  VkImage *images = calloc(count, sizeof(*images));
  REQUIRE(images);
  CHECK(DEVICE(GetSwapchainImagesKHR)(device, chain, &count, images));
  VkCommandPool pool;
  VkCommandPoolCreateInfo pci = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = 0};
  CHECK(DEVICE(CreateCommandPool)(device, &pci, NULL, &pool));
  VkCommandBuffer command;
  VkCommandBufferAllocateInfo cai = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    .commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
  CHECK(DEVICE(AllocateCommandBuffers)(device, &cai, &command));
  VkSemaphore acquired, complete;
  VkSemaphoreCreateInfo sem = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  CHECK(DEVICE(CreateSemaphore)(device, &sem, NULL, &acquired));
  CHECK(DEVICE(CreateSemaphore)(device, &sem, NULL, &complete));
  uint32_t index;
  CHECK(DEVICE(AcquireNextImageKHR)(device, chain, UINT64_MAX, acquired, VK_NULL_HANDLE, &index));
  VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  CHECK(DEVICE(BeginCommandBuffer)(command, &begin));
  VkImageMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
    .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .image = images[index],
    .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
  DEVICE(CmdPipelineBarrier)(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
    0, 0, NULL, 0, NULL, 1, &barrier);
  VkClearColorValue color = {.float32 = {0.1f, 0.8f, 0.2f, 1.0f}};
  DEVICE(CmdClearColorImage)(command, images[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color, 1, &barrier.subresourceRange);
  barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  barrier.dstAccessMask = 0;
  DEVICE(CmdPipelineBarrier)(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
    0, 0, NULL, 0, NULL, 1, &barrier);
  CHECK(DEVICE(EndCommandBuffer)(command));
  VkPipelineStageFlags stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
  VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .waitSemaphoreCount = 1,
    .pWaitSemaphores = &acquired, .pWaitDstStageMask = &stage, .commandBufferCount = 1,
    .pCommandBuffers = &command, .signalSemaphoreCount = 1, .pSignalSemaphores = &complete};
  CHECK(DEVICE(QueueSubmit)(queue, 1, &submit, VK_NULL_HANDLE));
  VkResult pending = DEVICE(WaitForPresentKHR)(device, chain, 1, 0);
  REQUIRE(pending == VK_TIMEOUT);
  uint64_t id = 1;
  VkPresentIdKHR pid = {.sType = VK_STRUCTURE_TYPE_PRESENT_ID_KHR, .swapchainCount = 1, .pPresentIds = &id};
  VkPresentInfoKHR present = {.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, .pNext = &pid,
    .waitSemaphoreCount = 1, .pWaitSemaphores = &complete, .swapchainCount = 1,
    .pSwapchains = &chain, .pImageIndices = &index};
  CHECK(DEVICE(QueuePresentKHR)(queue, &present));
  VkResult waited = DEVICE(WaitForPresentKHR)(device, chain, 1, 2000000000ULL);
  printf("present-wait result=%d window=0x%lx\n", waited, window);
  CHECK(DEVICE(DeviceWaitIdle)(device));
  puts("real GPU idle after present");
  sync_display(display, 0);
  void *readback = get_image(display, window, 320, 180, 1, 1, ~0UL, 2);
  REQUIRE(readback);
  unsigned long pixel = get_pixel(readback, 0, 0);
  destroy_image(readback);
  printf("X11 center pixel=0x%06lx\n", pixel);
  REQUIRE(((pixel >> 8) & 255) > 180 && ((pixel >> 16) & 255) < 50 && (pixel & 255) < 80);
  sleep(15);
  DEVICE(DestroySemaphore)(device, acquired, NULL);
  DEVICE(DestroySemaphore)(device, complete, NULL);
  DEVICE(DestroyCommandPool)(device, pool, NULL);
  DEVICE(DestroySwapchainKHR)(device, chain, NULL);
  DEVICE(DestroyDevice)(device, NULL);
  INSTANCE(DestroySurfaceKHR)(instance, surface, NULL);
  INSTANCE(DestroyInstance)(instance, NULL);
  free(images);
  return waited == VK_SUCCESS ? 0 : 1;
}
