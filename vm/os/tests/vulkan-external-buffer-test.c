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

/* Reproduce Frame's 400-byte host-visible external uniform buffer.
 * Verify GPU writes in both directions across independent guest processes. */
#define CHECK(call) do { VkResult r = (call); if (r != VK_SUCCESS) { \
  fprintf(stderr, "%s failed: %d\n", #call, r); exit(1); } } while (0)
#define REQUIRE(test) do { if (!(test)) { fprintf(stderr, "failed: %s\n", #test); exit(1); } } while (0)
#define PROC(ctx, name) ((PFN_vk##name)(ctx)->gdpa((ctx)->device, "vk" #name))
typedef struct {
  VkInstance instance;
  VkPhysicalDevice physical;
  VkDevice device;
  VkQueue queue;
  PFN_vkGetInstanceProcAddr gipa;
  PFN_vkGetDeviceProcAddr gdpa;
  VkPhysicalDeviceMemoryProperties memory;
} Context;
typedef struct { VkDeviceSize size;  uint32_t memory_type; } Payload;
typedef struct { VkBuffer buffer; VkDeviceMemory memory; } SharedBuffer;
static VkExternalMemoryHandleTypeFlagBits handle_type = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

static int dedicated_allocation;
static VkMemoryPropertyFlags host_flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

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
  const char *extensions[] = {"VK_KHR_external_memory_fd", "VK_EXT_external_memory_dma_buf"};
  VkDeviceCreateInfo d = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = 1,
    .pQueueCreateInfos = &q, .enabledExtensionCount = 2, .ppEnabledExtensionNames = extensions};
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

static void complete(Context *ctx, VkCommandBuffer cmd) {
  CHECK(PROC(ctx, EndCommandBuffer)(cmd));
  VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd};
  CHECK(PROC(ctx, QueueSubmit)(ctx->queue, 1, &submit, VK_NULL_HANDLE));
  CHECK(PROC(ctx, QueueWaitIdle)(ctx->queue));
}


static SharedBuffer buffer(Context *ctx, int fd, Payload *payload) {
 VkExternalMemoryBufferCreateInfo external={.sType=VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO,.handleTypes=handle_type};
 VkBufferCreateInfo ci={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.pNext=&external,.size=400,.usage=0x13};
 SharedBuffer result;CHECK(PROC(ctx,CreateBuffer)(ctx->device,&ci,NULL,&result.buffer));
 VkMemoryRequirements req;PROC(ctx,GetBufferMemoryRequirements)(ctx->device,result.buffer,&req);
 uint32_t bits=req.memoryTypeBits;
 if(fd>=0) {
  if(handle_type==VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT) {
   VkMemoryFdPropertiesKHR p={.sType=VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR};
   CHECK(PROC(ctx,GetMemoryFdPropertiesKHR)(ctx->device,handle_type,fd,&p));bits &= p.memoryTypeBits;
  } else bits &= 1u<<payload->memory_type;
 }
 VkImportMemoryFdInfoKHR import={.sType=VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR,.handleType=handle_type,.fd=fd};
 VkExportMemoryAllocateInfo export={.sType=VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO,.handleTypes=handle_type};
 
 VkMemoryDedicatedAllocateInfo dedicated={.sType=VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,.pNext=fd<0?(void*)&export:(void*)&import,.buffer=result.buffer};
 VkMemoryAllocateInfo a={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.pNext=dedicated_allocation?(void*)&dedicated:(fd<0?(void*)&export:(void*)&import),.allocationSize=fd<0?req.size:payload->size,.memoryTypeIndex=memory_type(ctx,bits,host_flags)};
 CHECK(PROC(ctx,AllocateMemory)(ctx->device,&a,NULL,&result.memory));
 CHECK(PROC(ctx,BindBufferMemory)(ctx->device,result.buffer,result.memory,0));
 if(fd<0){payload->size=a.allocationSize;payload->memory_type=a.memoryTypeIndex;}
 return result;
}
static void finish(Context *ctx, SharedBuffer image) {
 PROC(ctx,DestroyBuffer)(ctx->device,image.buffer,NULL);PROC(ctx,FreeMemory)(ctx->device,image.memory,NULL);
 PROC(ctx,DestroyDevice)(ctx->device,NULL);
 ((PFN_vkDestroyInstance)ctx->gipa(ctx->instance,"vkDestroyInstance"))(ctx->instance,NULL);
}
int main(int argc,char **argv) {
 setbuf(stdout,NULL);
 for(int i=1;i<argc;i++) {
  if(!strcmp(argv[i],"--opaque"))handle_type=VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
  else if(!strcmp(argv[i],"--dedicated"))dedicated_allocation=1;
  else if(!strcmp(argv[i],"--cached"))host_flags|=VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
  else REQUIRE(0 && "usage: external-buffer-test [--opaque] [--dedicated] [--cached]");
 }
 int sockets[2];REQUIRE(socketpair(AF_UNIX,SOCK_SEQPACKET,0,sockets)==0);
 pid_t peer=fork();REQUIRE(peer>=0);
 if(!peer){
  close(sockets[0]);Payload payload;
  struct iovec vector={&payload,sizeof payload};
  union{struct cmsghdr alignment;char bytes[CMSG_SPACE(sizeof(int))];}control;
  struct msghdr packet={.msg_iov=&vector,.msg_iovlen=1,.msg_control=control.bytes,.msg_controllen=sizeof control.bytes};
  REQUIRE(recvmsg(sockets[1],&packet,0)==sizeof payload);
  struct cmsghdr *h=CMSG_FIRSTHDR(&packet);REQUIRE(h&&h->cmsg_type==SCM_RIGHTS);
  int fd=*(int*)CMSG_DATA(h);Context ctx=initialize();SharedBuffer imported=buffer(&ctx,fd,&payload);
  void *mapped;CHECK(PROC(&ctx,MapMemory)(ctx.device,imported.memory,0,400,0,&mapped));
  for(unsigned i=0;i<100;i++)REQUIRE(((uint32_t*)mapped)[i]==0x12345678);
  VkCommandPool pool;VkCommandBuffer cmd=commands(&ctx,&pool);
  PROC(&ctx,CmdFillBuffer)(cmd,imported.buffer,0,400,0x87654321);complete(&ctx,cmd);
  PROC(&ctx,UnmapMemory)(ctx.device,imported.memory);
  PROC(&ctx,DestroyCommandPool)(ctx.device,pool,NULL);finish(&ctx,imported);
  REQUIRE(write(sockets[1],"p",1)==1);return 0;
 }
 close(sockets[1]);Context ctx=initialize();Payload payload={0};SharedBuffer exported=buffer(&ctx,-1,&payload);
 VkCommandPool pool;VkCommandBuffer cmd=commands(&ctx,&pool);
 PROC(&ctx,CmdFillBuffer)(cmd,exported.buffer,0,400,0x12345678);complete(&ctx,cmd);
 VkMemoryGetFdInfoKHR info={.sType=VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR,.memory=exported.memory,.handleType=handle_type};
 int fd;CHECK(PROC(&ctx,GetMemoryFdKHR)(ctx.device,&info,&fd));
 struct iovec vector={&payload,sizeof payload};
 union{struct cmsghdr alignment;char bytes[CMSG_SPACE(sizeof(int))];}control={0};
 struct msghdr packet={.msg_iov=&vector,.msg_iovlen=1,.msg_control=control.bytes,.msg_controllen=sizeof control.bytes};
 struct cmsghdr *h=CMSG_FIRSTHDR(&packet);h->cmsg_level=SOL_SOCKET;h->cmsg_type=SCM_RIGHTS;h->cmsg_len=CMSG_LEN(sizeof(int));*(int*)CMSG_DATA(h)=fd;
 REQUIRE(sendmsg(sockets[0],&packet,0)==sizeof payload);close(fd);
 struct pollfd ready={sockets[0],POLLIN,0};REQUIRE(poll(&ready,1,30000)>0);
 char answer;REQUIRE(read(sockets[0],&answer,1)==1&&answer=='p');
 int status;REQUIRE(waitpid(peer,&status,0)==peer&&WIFEXITED(status)&&!WEXITSTATUS(status));
 void *mapped;CHECK(PROC(&ctx,MapMemory)(ctx.device,exported.memory,0,400,0,&mapped));
 for(unsigned i=0;i<100;i++)REQUIRE(((uint32_t*)mapped)[i]==0x87654321);
 PROC(&ctx,UnmapMemory)(ctx.device,exported.memory);PROC(&ctx,DestroyCommandPool)(ctx.device,pool,NULL);finish(&ctx,exported);
 printf("PASS: %s 400-byte host-visible shared buffer, dedicated=%d cached=%d, preserves GPU writes in both guest processes\n",handle_type==VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT?"DMA_BUF":"OPAQUE_FD",dedicated_allocation,!!(host_flags&VK_MEMORY_PROPERTY_HOST_CACHED_BIT));return 0;
}
