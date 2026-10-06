#define _GNU_SOURCE
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <dlfcn.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

/* Vulkan-only DMA_BUF image sharing. No GL import or capability-only pass. */
#define CHECK(call) do { VkResult r = (call); if (r != VK_SUCCESS) { \
  fprintf(stderr, "%s failed: %d\n", #call, r); exit(1); } } while (0)
#define REQUIRE(test) do { if (!(test)) { fprintf(stderr, "failed: %s\n", #test); exit(1); } } while (0)
#define PROC(ctx, name) ((PFN_vk##name)(ctx)->gdpa((ctx)->device, "vk" #name))
#define WIDTH 64
#define HEIGHT 64
typedef struct {
  VkInstance instance;
  VkPhysicalDevice physical;
  VkDevice device;
  VkQueue queue;
  PFN_vkGetInstanceProcAddr gipa;
  PFN_vkGetDeviceProcAddr gdpa;
  VkPhysicalDeviceMemoryProperties memory;
} Context;
typedef struct { VkDeviceSize size; VkSubresourceLayout plane; uint32_t memory_type; } Payload;
typedef struct { VkImage image; VkDeviceMemory memory; } Image;
static VkExternalMemoryHandleTypeFlagBits handle_type = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

static int optimal_tiling;

static Context initialize(void) {
  void *loader = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
  REQUIRE(loader);
  PFN_vkGetInstanceProcAddr gipa = (PFN_vkGetInstanceProcAddr)dlsym(loader, "vkGetInstanceProcAddr");
  REQUIRE(gipa);
  PFN_vkCreateInstance create = (PFN_vkCreateInstance)gipa(NULL, "vkCreateInstance");
  VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_2};
  VkInstanceCreateInfo ici = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app};
  Context ctx = {.gipa = gipa};
  CHECK(create(&ici, NULL, &ctx.instance));
  PFN_vkEnumeratePhysicalDevices enumerate = (PFN_vkEnumeratePhysicalDevices)gipa(ctx.instance, "vkEnumeratePhysicalDevices");
  uint32_t count = 1;
  CHECK(enumerate(ctx.instance, &count, &ctx.physical));
  float priority = 1;
  VkDeviceQueueCreateInfo q = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
    .queueFamilyIndex = 0, .queueCount = 1, .pQueuePriorities = &priority};
  const char *extensions[] = {"VK_KHR_external_memory_fd", "VK_EXT_external_memory_dma_buf", "VK_EXT_image_drm_format_modifier"};
  VkDeviceCreateInfo d = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = 1,
    .pQueueCreateInfos = &q, .enabledExtensionCount = 3, .ppEnabledExtensionNames = extensions};
  CHECK(((PFN_vkCreateDevice)gipa(ctx.instance, "vkCreateDevice"))(ctx.physical, &d, NULL, &ctx.device));
  ctx.gdpa = (PFN_vkGetDeviceProcAddr)gipa(ctx.instance, "vkGetDeviceProcAddr");
  PROC(&ctx, GetDeviceQueue)(ctx.device, 0, 0, &ctx.queue);
  ((PFN_vkGetPhysicalDeviceMemoryProperties)gipa(ctx.instance, "vkGetPhysicalDeviceMemoryProperties"))(ctx.physical, &ctx.memory);
  return ctx;
}

static uint32_t memory_type(Context *ctx, uint32_t bits, VkMemoryPropertyFlags flags) {
  for (uint32_t i = 0; i < ctx->memory.memoryTypeCount; i++)
    if ((bits & (1u << i)) && (ctx->memory.memoryTypes[i].propertyFlags & flags) == flags) return i;
  REQUIRE(0 && "no compatible memory type");
  return 0;
}

static Image image(Context *ctx, int fd, Payload *payload) {
  const uint64_t linear = 0;
  VkImageDrmFormatModifierListCreateInfoEXT list = {.sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_LIST_CREATE_INFO_EXT,
    .drmFormatModifierCount = 1, .pDrmFormatModifiers = &linear};
  VkImageDrmFormatModifierExplicitCreateInfoEXT explicit = {.sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT,
    .drmFormatModifier = 0, .drmFormatModifierPlaneCount = 1, .pPlaneLayouts = &payload->plane};
  VkExternalMemoryImageCreateInfo external = {.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
    .pNext = handle_type == VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT
      ? (fd < 0 ? (void *)&list : (void *)&explicit) : NULL, .handleTypes = handle_type};
  VkImageCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .pNext = &external,
    .imageType = VK_IMAGE_TYPE_2D, .format = VK_FORMAT_B8G8R8A8_UNORM, .extent = {WIDTH, HEIGHT, 1},
    .mipLevels = 1, .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT,
    .tiling = handle_type == VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT
      ? VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT
      : (optimal_tiling ? VK_IMAGE_TILING_OPTIMAL : VK_IMAGE_TILING_LINEAR), .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
      VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
    .sharingMode = VK_SHARING_MODE_EXCLUSIVE, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
  Image result;
  CHECK(PROC(ctx, CreateImage)(ctx->device, &ci, NULL, &result.image));
  VkMemoryRequirements req;
  PROC(ctx, GetImageMemoryRequirements)(ctx->device, result.image, &req);
  uint32_t bits = req.memoryTypeBits;
  VkMemoryFdPropertiesKHR properties = {.sType = VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR};
  if (fd >= 0 && handle_type == VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT) {
    CHECK(PROC(ctx, GetMemoryFdPropertiesKHR)(ctx->device, handle_type, fd, &properties));
    bits &= properties.memoryTypeBits;
  } else if (fd >= 0) {
    REQUIRE(payload->memory_type < 32);
    bits &= 1u << payload->memory_type;
  }
  VkImportMemoryFdInfoKHR import = {.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR,
    .handleType = handle_type, .fd = fd};
  VkExportMemoryAllocateInfo export = {.sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO,
    .handleTypes = handle_type};
  VkMemoryDedicatedAllocateInfo dedicated = {.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
    .pNext = fd < 0 ? (void *)&export : (void *)&import, .image = result.image};
  VkMemoryAllocateInfo allocation = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .pNext = &dedicated,
    .allocationSize = fd < 0 ? req.size : payload->size, .memoryTypeIndex = memory_type(ctx, bits, 0)};
  CHECK(PROC(ctx, AllocateMemory)(ctx->device, &allocation, NULL, &result.memory));
  CHECK(PROC(ctx, BindImageMemory)(ctx->device, result.image, result.memory, 0));
  if (fd < 0) {
    payload->size = allocation.allocationSize;
    payload->memory_type = allocation.memoryTypeIndex;
    VkImageSubresource sub = {.aspectMask = handle_type == VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT
      ? VK_IMAGE_ASPECT_MEMORY_PLANE_0_BIT_EXT : VK_IMAGE_ASPECT_COLOR_BIT};
    if (!optimal_tiling)
      PROC(ctx, GetImageSubresourceLayout)(ctx->device, result.image, &sub, &payload->plane);
  }
  return result;
}

static VkCommandBuffer commands(Context *ctx, VkCommandPool *pool) {
  VkCommandPoolCreateInfo p = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = 0};
  CHECK(PROC(ctx, CreateCommandPool)(ctx->device, &p, NULL, pool));
  VkCommandBufferAllocateInfo a = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    .commandPool = *pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
  VkCommandBuffer cmd;
  CHECK(PROC(ctx, AllocateCommandBuffers)(ctx->device, &a, &cmd));
  VkCommandBufferBeginInfo b = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  CHECK(PROC(ctx, BeginCommandBuffer)(cmd, &b));
  return cmd;
}

static void barrier(Context *ctx, VkCommandBuffer cmd, VkImage image,
                    VkImageLayout old, VkImageLayout next, uint32_t from, uint32_t to,
                    VkAccessFlags src, VkAccessFlags dst) {
  VkImageMemoryBarrier b = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
    .srcAccessMask = src, .dstAccessMask = dst, .oldLayout = old, .newLayout = next,
    .srcQueueFamilyIndex = from, .dstQueueFamilyIndex = to, .image = image,
    .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
  PROC(ctx, CmdPipelineBarrier)(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
    VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0, NULL, 1, &b);
}

static void complete(Context *ctx, VkCommandBuffer cmd) {
  CHECK(PROC(ctx, EndCommandBuffer)(cmd));
  VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd};
  CHECK(PROC(ctx, QueueSubmit)(ctx->queue, 1, &submit, VK_NULL_HANDLE));
  CHECK(PROC(ctx, QueueWaitIdle)(ctx->queue));
}

static void verify(Context *ctx, Image img) {
  VkBufferCreateInfo b = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
    .size = WIDTH * HEIGHT * 4, .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT};
  VkBuffer buffer;
  CHECK(PROC(ctx, CreateBuffer)(ctx->device, &b, NULL, &buffer));
  VkMemoryRequirements req;
  PROC(ctx, GetBufferMemoryRequirements)(ctx->device, buffer, &req);
  VkMemoryAllocateInfo a = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = req.size,
    .memoryTypeIndex = memory_type(ctx, req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};
  VkDeviceMemory memory;
  CHECK(PROC(ctx, AllocateMemory)(ctx->device, &a, NULL, &memory));
  CHECK(PROC(ctx, BindBufferMemory)(ctx->device, buffer, memory, 0));
  VkCommandPool pool;
  VkCommandBuffer cmd = commands(ctx, &pool);
  barrier(ctx, cmd, img.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
    VK_QUEUE_FAMILY_EXTERNAL, 0, 0, VK_ACCESS_TRANSFER_READ_BIT);
  VkBufferImageCopy copy = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .imageExtent = {WIDTH, HEIGHT, 1}};
  PROC(ctx, CmdCopyImageToBuffer)(cmd, img.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &copy);
  VkBufferMemoryBarrier host = {.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
    .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
    .buffer = buffer, .size = VK_WHOLE_SIZE};
  PROC(ctx, CmdPipelineBarrier)(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
    0, 0, NULL, 1, &host, 0, NULL);
  complete(ctx, cmd);
  void *mapped;
  CHECK(PROC(ctx, MapMemory)(ctx->device, memory, 0, VK_WHOLE_SIZE, 0, &mapped));
  const uint32_t *pixels = mapped;
  for (unsigned i = 0; i < WIDTH * HEIGHT; i++) {
    if (pixels[i] != UINT32_C(0xffff00ff)) {
      fprintf(stderr, "pixel %u: expected ffff00ff, got %08x\n", i, pixels[i]); exit(1);
    }
  }
  PROC(ctx, UnmapMemory)(ctx->device, memory);
  PROC(ctx, DestroyCommandPool)(ctx->device, pool, NULL);
  PROC(ctx, DestroyBuffer)(ctx->device, buffer, NULL);
  PROC(ctx, FreeMemory)(ctx->device, memory, NULL);
}

static void close_context(Context *ctx, Image img) {
  PROC(ctx, DestroyImage)(ctx->device, img.image, NULL);
  PROC(ctx, FreeMemory)(ctx->device, img.memory, NULL);
  PROC(ctx, DestroyDevice)(ctx->device, NULL);
  ((PFN_vkDestroyInstance)ctx->gipa(ctx->instance, "vkDestroyInstance"))(ctx->instance, NULL);
}

int main(int argc, char **argv) {
  setbuf(stdout, NULL);
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--opaque")) handle_type = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
    else if (!strcmp(argv[i], "--optimal")) optimal_tiling = 1;
    else REQUIRE(0 && "usage: external-image-test [--opaque [--optimal]]");
  }
  REQUIRE(!optimal_tiling || handle_type == VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT);
  int sockets[2];
  REQUIRE(socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sockets) == 0);
  pid_t peer = fork();
  REQUIRE(peer >= 0);
  if (!peer) {
    close(sockets[0]);
    Payload payload;
    struct iovec vector = {&payload, sizeof(payload)};
    union { struct cmsghdr alignment; char bytes[CMSG_SPACE(sizeof(int))]; } control;
    struct msghdr packet = {.msg_iov = &vector, .msg_iovlen = 1,
      .msg_control = control.bytes, .msg_controllen = sizeof(control.bytes)};
    REQUIRE(recvmsg(sockets[1], &packet, 0) == sizeof(payload));
    struct cmsghdr *header = CMSG_FIRSTHDR(&packet);
    REQUIRE(header && header->cmsg_level == SOL_SOCKET && header->cmsg_type == SCM_RIGHTS);
    int fd = *(int *)CMSG_DATA(header);
    Context ctx = initialize();
    Image imported = image(&ctx, fd, &payload);
    verify(&ctx, imported);
    close_context(&ctx, imported);
    REQUIRE(write(sockets[1], "p", 1) == 1);
    return 0;
  }
  close(sockets[1]);
  Context ctx = initialize();
  Payload payload = {0};
  Image exported = image(&ctx, -1, &payload);
  VkCommandPool pool;
  VkCommandBuffer cmd = commands(&ctx, &pool);
  barrier(&ctx, cmd, exported.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, 0, VK_ACCESS_TRANSFER_WRITE_BIT);
  const VkClearColorValue magenta = {.float32 = {1, 0, 1, 1}};
  const VkImageSubresourceRange range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  PROC(&ctx, CmdClearColorImage)(cmd, exported.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &magenta, 1, &range);
  barrier(&ctx, cmd, exported.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
    0, VK_QUEUE_FAMILY_EXTERNAL, VK_ACCESS_TRANSFER_WRITE_BIT, 0);
  complete(&ctx, cmd);
  VkMemoryGetFdInfoKHR info = {.sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR,
    .memory = exported.memory, .handleType = handle_type};
  int fd;
  CHECK(PROC(&ctx, GetMemoryFdKHR)(ctx.device, &info, &fd));
  struct iovec vector = {&payload, sizeof(payload)};
  union { struct cmsghdr alignment; char bytes[CMSG_SPACE(sizeof(int))]; } control = {0};
  struct msghdr packet = {.msg_iov = &vector, .msg_iovlen = 1,
    .msg_control = control.bytes, .msg_controllen = sizeof(control.bytes)};
  struct cmsghdr *header = CMSG_FIRSTHDR(&packet);
  header->cmsg_level = SOL_SOCKET; header->cmsg_type = SCM_RIGHTS;
  header->cmsg_len = CMSG_LEN(sizeof(int)); *(int *)CMSG_DATA(header) = fd;
  REQUIRE(sendmsg(sockets[0], &packet, 0) == sizeof(payload));
  close(fd);
  struct pollfd ready = {sockets[0], POLLIN, 0};
  REQUIRE(poll(&ready, 1, 20000) > 0);
  char answer;
  REQUIRE(read(sockets[0], &answer, 1) == 1 && answer == 'p');
  int status;
  REQUIRE(waitpid(peer, &status, 0) == peer && WIFEXITED(status) && !WEXITSTATUS(status));
  PROC(&ctx, DestroyCommandPool)(ctx.device, pool, NULL);
  close_context(&ctx, exported);
  printf("PASS: exported %s %s image retains all 4096 pixels across guest processes\n",
    handle_type == VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT ? "DMA_BUF" : "OPAQUE_FD", optimal_tiling ? "OPTIMAL" : "LINEAR");
  return 0;
}
