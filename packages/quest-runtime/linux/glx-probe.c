#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <vulkan/vulkan.h>
#define CHECK(call)                                                            \
  do {                                                                         \
    VkResult result = (call);                                                  \
    if (result) {                                                              \
      printf("ERROR %s %d\n", #call, result);                                  \
      return 10;                                                               \
    }                                                                          \
  } while (0)
// Reproduce SteamVR's opaque-fd GL/Vulkan sharing contract on a real GPU.
// Keep FRAMEBUFFER_SRGB disabled: linear views must retain the imported image.
int main(void) {
  void *x = dlopen("/usr/lib/aarch64-linux-gnu/libX11.so.6",
                   RTLD_NOW | RTLD_GLOBAL),
       *g = dlopen("/usr/lib/aarch64-linux-gnu/libGL.so.1",
                   RTLD_NOW | RTLD_GLOBAL);
  if (!x || !g) {
    puts(dlerror());
    return 1;
  }
  void *(*open)(const char *) = dlsym(x, "XOpenDisplay");
  void *d = open(":0");
  if (!d)
    return 2;
  void **(*choose)(void *, int, const int *, int *) =
      dlsym(g, "glXChooseFBConfig");
  int attr[] = {0x8010, 4, 0x8011, 1, 0}, count = 0;
  void **configs = choose(d, 0, attr, &count);
  if (!count)
    return 3;
  void *(*create)(void *, void *, int, void *, int) =
      dlsym(g, "glXCreateNewContext");
  void *c = create(d, configs[0], 0x8014, 0, 1);
  if (!c)
    return 4;
  unsigned long (*buffer)(void *, void *, const int *) =
      dlsym(g, "glXCreatePbuffer");
  int a[] = {0x8041, 1, 0x8040, 1, 0};
  unsigned long b = buffer(d, configs[0], a);
  int (*make)(void *, unsigned long, unsigned long, void *) =
      dlsym(g, "glXMakeContextCurrent");
  if (!make(d, b, b, c))
    return 5;
  void *(*proc)(const unsigned char *) = dlsym(g, "glXGetProcAddress");
  const unsigned char *(*string)(unsigned) = proc((void *)"glGetString");
  const char *renderer = (const char *)string(0x1F01);
  printf("GLX_RENDERER=%s\n", renderer);
  void (*integer)(unsigned, int *) = proc((void *)"glGetIntegerv");
  const unsigned char *(*stringi)(unsigned, unsigned) =
      proc((void *)"glGetStringi");
  integer(0x821D, &count);
  int semaphore = 0, memory = 0;
  for (int i = 0; i < count; i++) {
    const char *e = (const char *)stringi(0x1F03, i);
    if (!e)
      continue;
    if (!strcmp(e, "GL_EXT_semaphore_fd"))
      semaphore = 1;
    if (!strcmp(e, "GL_EXT_memory_object_fd"))
      memory = 1;
  }
  void (*color)(float, float, float, float) = proc((void *)"glClearColor");
  void (*clear)(unsigned) = proc((void *)"glClear");
  void (*read)(int, int, int, int, unsigned, unsigned, void *) =
      proc((void *)"glReadPixels");
  unsigned char pixel[4] = {0};
  color(1, 0, 0, 1);
  clear(0x4000);
  read(0, 0, 1, 1, 0x1908, 0x1401, pixel);
  if (!renderer || !strstr(renderer, "Adreno") || !semaphore || !memory ||
      pixel[0] != 255 || pixel[1] || pixel[2])
    return 6;

  void *vk = dlopen("libvulkan.so.1", RTLD_NOW);
  if (!vk) {
    puts(dlerror());
    return 7;
  }
  PFN_vkGetInstanceProcAddr get = dlsym(vk, "vkGetInstanceProcAddr");
  if (!get)
    return 7;
  VkInstance instance = 0;
#define FN(name) PFN_##name name = (PFN_##name)get(instance, #name)
  FN(vkCreateInstance);
  VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                           .apiVersion = VK_API_VERSION_1_1};
  VkInstanceCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                             .pApplicationInfo = &app};
  CHECK(vkCreateInstance(&ci, 0, &instance));
  FN(vkEnumeratePhysicalDevices);
  FN(vkGetPhysicalDeviceMemoryProperties);
  FN(vkCreateDevice);
  FN(vkGetDeviceProcAddr);
  VkPhysicalDevice physical;
  uint32_t n = 1;
  CHECK(vkEnumeratePhysicalDevices(instance, &n, &physical));
  float priority = 1;
  VkDeviceQueueCreateInfo qi = {.sType =
                                    VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                .queueFamilyIndex = 0,
                                .queueCount = 1,
                                .pQueuePriorities = &priority};
  const char *extensions[] = {"VK_KHR_external_memory_fd"};
  VkDeviceCreateInfo di = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                           .queueCreateInfoCount = 1,
                           .pQueueCreateInfos = &qi,
                           .enabledExtensionCount = 1,
                           .ppEnabledExtensionNames = extensions};
  VkDevice device;
  CHECK(vkCreateDevice(physical, &di, 0, &device));
#define D(name) PFN_##name name = (PFN_##name)vkGetDeviceProcAddr(device, #name)
  D(vkCreateImage);
  D(vkGetImageMemoryRequirements);
  D(vkAllocateMemory);
  D(vkBindImageMemory);
  D(vkGetMemoryFdKHR);
  D(vkCreateBuffer);
  D(vkGetBufferMemoryRequirements);
  D(vkBindBufferMemory);
  D(vkMapMemory);
  D(vkCreateCommandPool);
  D(vkAllocateCommandBuffers);
  D(vkBeginCommandBuffer);
  D(vkCmdPipelineBarrier);
  D(vkCmdCopyImageToBuffer);
  D(vkEndCommandBuffer);
  D(vkGetDeviceQueue);
  D(vkQueueSubmit);
  D(vkQueueWaitIdle);
  D(vkInvalidateMappedMemoryRanges);
  VkExternalMemoryImageCreateInfo external = {
      .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
      .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT};
  VkImageCreateInfo ii = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                          .pNext = &external,
                          .imageType = VK_IMAGE_TYPE_2D,
                          .format = VK_FORMAT_R8G8B8A8_SRGB,
                          .extent = {64, 64, 1},
                          .mipLevels = 1,
                          .arrayLayers = 1,
                          .samples = VK_SAMPLE_COUNT_1_BIT,
                          .tiling = VK_IMAGE_TILING_OPTIMAL,
                          .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                   VK_IMAGE_USAGE_SAMPLED_BIT |
                                   VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                   VK_IMAGE_USAGE_TRANSFER_DST_BIT};
  VkImage im;
  CHECK(vkCreateImage(device, &ii, 0, &im));
  VkMemoryRequirements req;
  vkGetImageMemoryRequirements(device, im, &req);
  VkPhysicalDeviceMemoryProperties props;
  vkGetPhysicalDeviceMemoryProperties(physical, &props);
  uint32_t type = 0;
  while (!(req.memoryTypeBits & (1 << type)))
    type++;
  VkExportMemoryAllocateInfo ex = {
      .sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO,
      .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT};
  VkMemoryAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                             .pNext = &ex,
                             .allocationSize = req.size,
                             .memoryTypeIndex = type};
  VkDeviceMemory mem;
  CHECK(vkAllocateMemory(device, &ai, 0, &mem));
  CHECK(vkBindImageMemory(device, im, mem, 0));
  VkMemoryGetFdInfoKHR fdinfo = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR,
      .memory = mem,
      .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT};
  int fd;
  CHECK(vkGetMemoryFdKHR(device, &fdinfo, &fd));
#define GL(ret, name, args) ret(*name) args = proc((void *)#name)
  GL(void, glCreateMemoryObjectsEXT, (int, unsigned *));
  GL(void, glImportMemoryFdEXT, (unsigned, unsigned long, unsigned, int));
  GL(void, glGenTextures, (int, unsigned *));
  GL(void, glBindTexture, (unsigned, unsigned));
  GL(void, glTexStorageMem2DEXT,
     (unsigned, int, unsigned, int, int, unsigned, unsigned long));
  GL(void, glGenFramebuffers, (int, unsigned *));
  GL(void, glBindFramebuffer, (unsigned, unsigned));
  GL(void, glFramebufferTexture2D,
     (unsigned, unsigned, unsigned, unsigned, int));
  GL(unsigned, glCheckFramebufferStatus, (unsigned));
  GL(unsigned, glGetError, (void));
  GL(void, glFinish, (void));
  unsigned mo, tex, fbo;
  glCreateMemoryObjectsEXT(1, &mo);
  glImportMemoryFdEXT(mo, req.size, 0x9586, fd);
  printf("GL_IMPORT_ERROR=%x size=%lu\n", glGetError(), req.size);
  glGenTextures(1, &tex);
  glBindTexture(0x0DE1, tex);
  glTexStorageMem2DEXT(0x0DE1, 1, 0x8C43, 64, 64, mo, 0);
  printf("GL_STORAGE_ERROR=%x\n", glGetError());
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(0x8D40, fbo);
  glFramebufferTexture2D(0x8D40, 0x8CE0, 0x0DE1, tex, 0);
  printf("GL_FBO=%x\n", glCheckFramebufferStatus(0x8D40));
  color(1, 0, 0, 1);
  clear(0x4000);
  glFinish();
  read(0, 0, 1, 1, 0x1908, 0x1401, pixel);
  printf("GL_IMPORTED_PIXEL=%u,%u,%u,%u error=%x\n", pixel[0], pixel[1],
         pixel[2], pixel[3], glGetError());
  VkBufferCreateInfo bi = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                           .size = 64 * 64 * 4,
                           .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT};
  VkBuffer out;
  CHECK(vkCreateBuffer(device, &bi, 0, &out));
  vkGetBufferMemoryRequirements(device, out, &req);
  type = 0;
  while (!(req.memoryTypeBits & (1 << type)) ||
         !(props.memoryTypes[type].propertyFlags &
           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
    type++;
  ai.pNext = 0;
  ai.allocationSize = req.size;
  ai.memoryTypeIndex = type;
  VkDeviceMemory bm;
  CHECK(vkAllocateMemory(device, &ai, 0, &bm));
  CHECK(vkBindBufferMemory(device, out, bm, 0));
  void *mapped;
  CHECK(vkMapMemory(device, bm, 0, VK_WHOLE_SIZE, 0, &mapped));
  memset(mapped, 0, 64 * 64 * 4);
  VkCommandPoolCreateInfo pi = {.sType =
                                    VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                .queueFamilyIndex = 0};
  VkCommandPool pool;
  CHECK(vkCreateCommandPool(device, &pi, 0, &pool));
  VkCommandBufferAllocateInfo ca = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = pool,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1};
  VkCommandBuffer cmd;
  CHECK(vkAllocateCommandBuffers(device, &ca, &cmd));
  VkCommandBufferBeginInfo cb = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  CHECK(vkBeginCommandBuffer(cmd, &cb));
  VkImageMemoryBarrier barrier = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
      .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
      .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
      .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL,
      .dstQueueFamilyIndex = 0,
      .image = im,
      .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, 0, 0, 0, 1,
                       &barrier);
  VkBufferImageCopy copy = {
      .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
      .imageExtent = {64, 64, 1}};
  vkCmdCopyImageToBuffer(cmd, im, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, out, 1,
                         &copy);
  CHECK(vkEndCommandBuffer(cmd));
  VkQueue queue;
  vkGetDeviceQueue(device, 0, 0, &queue);
  VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                         .commandBufferCount = 1,
                         .pCommandBuffers = &cmd};
  CHECK(vkQueueSubmit(queue, 1, &submit, 0));
  CHECK(vkQueueWaitIdle(queue));
  VkMappedMemoryRange range = {.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
                               .memory = bm,
                               .size = VK_WHOLE_SIZE};
  CHECK(vkInvalidateMappedMemoryRanges(device, 1, &range));
  unsigned char *p = mapped;
  printf("VULKAN_SHARED_PIXEL=%u,%u,%u,%u\n", p[0], p[1], p[2], p[3]);
  for (unsigned k = 0; k < 64 * 64; k++)
    if (p[k * 4] != 255 || p[k * 4 + 1] || p[k * 4 + 2] ||
        p[k * 4 + 3] != 255) {
      printf("SHARED_PIXEL_MISMATCH index=%u\n", k);
      return 11;
    }
  puts("GL_TO_VULKAN_PIXELS_VERIFIED");
  return 0;
}
