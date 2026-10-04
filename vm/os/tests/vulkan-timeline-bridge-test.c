#define _GNU_SOURCE
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <dlfcn.h>
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

/* Two independent guest processes and GPU devices. A closed gate must keep
 * the consumer waiting; GPU completion must then travel in both directions. */
#define CHECK(call) do { VkResult r = (call); if (r != VK_SUCCESS) { \
  fprintf(stderr, "%s failed: %d\n", #call, r); exit(1); } } while (0)
#define REQUIRE(test) do { if (!(test)) { perror(#test); exit(1); } } while (0)
#define DEVICE_PROC(name) PFN_vk##name name = (PFN_vk##name)gdpa(device, "vk" #name)

typedef struct {
  VkInstance instance;
  VkDevice device;
  VkQueue queue;
  PFN_vkGetInstanceProcAddr gipa;
  PFN_vkGetDeviceProcAddr gdpa;
} Context;

static Context initialize(void) {
  void *loader = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
  REQUIRE(loader);
  PFN_vkGetInstanceProcAddr gipa = (PFN_vkGetInstanceProcAddr)dlsym(loader, "vkGetInstanceProcAddr");
  REQUIRE(gipa);
  PFN_vkCreateInstance create = (PFN_vkCreateInstance)gipa(VK_NULL_HANDLE, "vkCreateInstance");
  VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_1};
  VkInstanceCreateInfo info = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app};
  VkInstance instance;
  CHECK(create(&info, NULL, &instance));
  PFN_vkEnumeratePhysicalDevices enumerate = (PFN_vkEnumeratePhysicalDevices)gipa(instance, "vkEnumeratePhysicalDevices");
  uint32_t count = 1;
  VkPhysicalDevice physical;
  CHECK(enumerate(instance, &count, &physical));
  PFN_vkGetPhysicalDeviceFeatures2 features = (PFN_vkGetPhysicalDeviceFeatures2)gipa(instance, "vkGetPhysicalDeviceFeatures2");
  VkPhysicalDeviceTimelineSemaphoreFeatures timeline = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES};
  VkPhysicalDeviceFeatures2 enabled = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &timeline};
  features(physical, &enabled);
  REQUIRE(timeline.timelineSemaphore);
  const float priority = 1;
  VkDeviceQueueCreateInfo queue = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
    .queueFamilyIndex = 0, .queueCount = 1, .pQueuePriorities = &priority};
  const char *extensions[] = {"VK_KHR_external_semaphore_fd", "VK_KHR_timeline_semaphore"};
  VkDeviceCreateInfo device_info = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
    .pNext = &timeline, .queueCreateInfoCount = 1, .pQueueCreateInfos = &queue,
    .enabledExtensionCount = 2, .ppEnabledExtensionNames = extensions};
  PFN_vkCreateDevice create_device = (PFN_vkCreateDevice)gipa(instance, "vkCreateDevice");
  VkDevice device;
  CHECK(create_device(physical, &device_info, NULL, &device));
  PFN_vkGetDeviceProcAddr gdpa = (PFN_vkGetDeviceProcAddr)gipa(instance, "vkGetDeviceProcAddr");
  DEVICE_PROC(GetDeviceQueue);
  VkQueue native_queue;
  GetDeviceQueue(device, 0, 0, &native_queue);
  return (Context){instance, device, native_queue, gipa, gdpa};
}

static VkSemaphore semaphore(Context *ctx, int exported) {
  VkDevice device = ctx->device;
  PFN_vkGetDeviceProcAddr gdpa = ctx->gdpa;
  DEVICE_PROC(CreateSemaphore);
  VkExportSemaphoreCreateInfo export_info = {.sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO,
    .handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT};
  VkSemaphoreTypeCreateInfo type = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
    .pNext = exported ? &export_info : NULL, .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE};
  VkSemaphoreCreateInfo info = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = &type};
  VkSemaphore result;
  CHECK(CreateSemaphore(device, &info, NULL, &result));
  return result;
}

static void submit(Context *ctx, VkSemaphore wait, uint64_t wait_value,
                   VkSemaphore signal, uint64_t signal_value) {
  VkDevice device = ctx->device;
  PFN_vkGetDeviceProcAddr gdpa = ctx->gdpa;
  DEVICE_PROC(QueueSubmit);
  VkTimelineSemaphoreSubmitInfo values = {.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
    .waitSemaphoreValueCount = 1, .pWaitSemaphoreValues = &wait_value,
    .signalSemaphoreValueCount = 1, .pSignalSemaphoreValues = &signal_value};
  const VkPipelineStageFlags stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
  VkSubmitInfo info = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .pNext = &values,
    .waitSemaphoreCount = 1, .pWaitSemaphores = &wait, .pWaitDstStageMask = &stage,
    .signalSemaphoreCount = 1, .pSignalSemaphores = &signal};
  CHECK(QueueSubmit(ctx->queue, 1, &info, VK_NULL_HANDLE));
}

static VkResult wait_value(Context *ctx, VkSemaphore semaphore, uint64_t value, uint64_t timeout) {
  PFN_vkWaitSemaphores wait = (PFN_vkWaitSemaphores)ctx->gdpa(ctx->device, "vkWaitSemaphores");
  if (!wait) wait = (PFN_vkWaitSemaphores)ctx->gdpa(ctx->device, "vkWaitSemaphoresKHR");
  REQUIRE(wait);
  VkSemaphoreWaitInfo info = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
    .semaphoreCount = 1, .pSemaphores = &semaphore, .pValues = &value};
  return wait(ctx->device, &info, timeout);
}

static void message(int socket, char expected) {
  struct pollfd ready = {socket, POLLIN, 0};
  REQUIRE(poll(&ready, 1, 15000) > 0);
  char received;
  REQUIRE(read(socket, &received, 1) == 1 && received == expected);
}

static void close_context(Context *ctx, VkSemaphore semaphore, VkSemaphore gate) {
  VkDevice device = ctx->device;
  PFN_vkGetDeviceProcAddr gdpa = ctx->gdpa;
  DEVICE_PROC(QueueWaitIdle); DEVICE_PROC(DestroySemaphore); DEVICE_PROC(DestroyDevice);
  CHECK(QueueWaitIdle(ctx->queue));
  fprintf(stderr, "process %ld: queue idle; destroying semaphores\n", (long)getpid());
  DestroySemaphore(device, semaphore, NULL);
  if (gate) DestroySemaphore(device, gate, NULL);
  fprintf(stderr, "process %ld: destroying device\n", (long)getpid());
  DestroyDevice(device, NULL);
  fprintf(stderr, "process %ld: destroying instance\n", (long)getpid());
  PFN_vkDestroyInstance destroy = (PFN_vkDestroyInstance)ctx->gipa(ctx->instance, "vkDestroyInstance");
  destroy(ctx->instance, NULL);
}

int main(void) {
  setbuf(stdout, NULL);
  int sockets[2];
  REQUIRE(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
  pid_t peer = fork();
  REQUIRE(peer >= 0);
  if (!peer) {
    close(sockets[0]);
    Context ctx = initialize();
    VkSemaphore imported = semaphore(&ctx, 0);
    char byte;
    struct iovec vector = {&byte, 1};
    union { struct cmsghdr alignment; char bytes[CMSG_SPACE(sizeof(int))]; } control;
    struct msghdr packet = {.msg_iov = &vector, .msg_iovlen = 1,
      .msg_control = control.bytes, .msg_controllen = sizeof(control.bytes)};
    REQUIRE(recvmsg(sockets[1], &packet, 0) == 1);
    struct cmsghdr *header = CMSG_FIRSTHDR(&packet);
    REQUIRE(header && header->cmsg_level == SOL_SOCKET && header->cmsg_type == SCM_RIGHTS);
    int fd = *(int *)CMSG_DATA(header);
    PFN_vkImportSemaphoreFdKHR import_fd = (PFN_vkImportSemaphoreFdKHR)ctx.gdpa(ctx.device, "vkImportSemaphoreFdKHR");
    REQUIRE(import_fd);
    VkImportSemaphoreFdInfoKHR info = {.sType = VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR,
      .semaphore = imported, .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT, .fd = fd};
    CHECK(import_fd(ctx.device, &info));
    REQUIRE(write(sockets[1], "i", 1) == 1);
    message(sockets[1], 'q');
    REQUIRE(wait_value(&ctx, imported, 5, UINT64_C(100000000)) == VK_TIMEOUT);
    submit(&ctx, imported, 5, imported, 9);
    REQUIRE(write(sockets[1], "b", 1) == 1);
    CHECK(wait_value(&ctx, imported, 9, UINT64_C(10000000000)));
    REQUIRE(write(sockets[1], "d", 1) == 1);
    close_context(&ctx, imported, VK_NULL_HANDLE);
    close(sockets[1]);
    return 0;
  }
  close(sockets[1]);
  Context ctx = initialize();
  VkSemaphore exported = semaphore(&ctx, 1), gate = semaphore(&ctx, 0);
  PFN_vkGetSemaphoreFdKHR get_fd = (PFN_vkGetSemaphoreFdKHR)ctx.gdpa(ctx.device, "vkGetSemaphoreFdKHR");
  REQUIRE(get_fd);
  VkSemaphoreGetFdInfoKHR info = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR,
    .semaphore = exported, .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT};
  int fd;
  CHECK(get_fd(ctx.device, &info, &fd));
  char byte = 'f';
  struct iovec vector = {&byte, 1};
  union { struct cmsghdr alignment; char bytes[CMSG_SPACE(sizeof(int))]; } control = {0};
  struct msghdr packet = {.msg_iov = &vector, .msg_iovlen = 1,
    .msg_control = control.bytes, .msg_controllen = sizeof(control.bytes)};
  struct cmsghdr *header = CMSG_FIRSTHDR(&packet);
  header->cmsg_level = SOL_SOCKET; header->cmsg_type = SCM_RIGHTS;
  header->cmsg_len = CMSG_LEN(sizeof(int)); *(int *)CMSG_DATA(header) = fd;
  REQUIRE(sendmsg(sockets[0], &packet, 0) == 1);
  close(fd);
  message(sockets[0], 'i');
  /* Let both imported/exported payloads enter idle polling before work
   * arrives. Recovery must still wait for actual producer GPU completion. */
  sleep(1);
  submit(&ctx, gate, 2, exported, 5);
  REQUIRE(write(sockets[0], "q", 1) == 1);
  message(sockets[0], 'b');
  puts("PASS: remote GPU wait remains blocked before producer completion");
  PFN_vkSignalSemaphore signal = (PFN_vkSignalSemaphore)ctx.gdpa(ctx.device, "vkSignalSemaphore");
  if (!signal) signal = (PFN_vkSignalSemaphore)ctx.gdpa(ctx.device, "vkSignalSemaphoreKHR");
  REQUIRE(signal);
  VkSemaphoreSignalInfo signal_info = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO,
    .semaphore = gate, .value = 2};
  CHECK(signal(ctx.device, &signal_info));
  message(sockets[0], 'd');
  CHECK(wait_value(&ctx, exported, 9, UINT64_C(10000000000)));
  int status;
  REQUIRE(waitpid(peer, &status, 0) == peer);
  if (!WIFEXITED(status) || WEXITSTATUS(status)) {
    fprintf(stderr, "consumer exited with status 0x%x\n", status);
    exit(1);
  }
  close_context(&ctx, exported, gate);
  close(sockets[0]);
  puts("PASS: timeline GPU completion crosses processes and returns value 9");
  return 0;
}
