// Vulkan ICD adapter: Turnip owns rendering; Android owns the headset display.
#define VK_USE_PLATFORM_XCB_KHR
#define VK_USE_PLATFORM_XLIB_KHR
#include <vulkan/vulkan.h>
#include <dlfcn.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <mutex>
#include <vector>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <condition_variable>
#include <chrono>
#include <atomic>
#include <unordered_map>
#include <time.h>
#include <cmath>
#include "share-mirror.h"
#include "roomview_shaders.h"
#include "pcvr_color_shader.h"
#include "gpu_bridge.h"
#include "gpu_frames.h"
#include <poll.h>
#include <climits>
#include "quest_bridge.h"
#include "display-pulse.h"
#include "video_image_layout.h"

namespace {
PFN_vkGetInstanceProcAddr realGet=nullptr;PFN_vkGetDeviceProcAddr realDeviceGet=nullptr;
uint32_t activeQueueFamily=0;
VkInstance activeInstance=VK_NULL_HANDLE;VkPhysicalDevice activePhysical=VK_NULL_HANDLE;
bool isCompositor(){return program_invocation_short_name&&(!strcmp(program_invocation_short_name,"vrcompositor")||!strcmp(program_invocation_short_name,"vrcompositor.bin"));}
bool isSteamCapture(){return program_invocation_short_name&&!strcmp(program_invocation_short_name,"steam");}
VkExtent2D displayExtent(){
 QuestTracking state{};int fd=open("/run/stearlight/quest/tracking",O_RDONLY|O_CLOEXEC);
 if(fd<0)return {3664,1920};
 ssize_t bytes=pread(fd,&state,sizeof(state),0);close(fd);
 if(bytes!=sizeof(state)||state.magic!=QUEST_BRIDGE_MAGIC||state.version!=QUEST_BRIDGE_VERSION||!state.width||!state.height)return {3664,1920};
 return {state.width*2,state.height};
}
struct RoomShader {VkShaderModule alternate=VK_NULL_HANDLE;bool background=false,stars=false;};
struct RoomPipeline {VkPipeline alternate=VK_NULL_HANDLE;bool background=false,stars=false;};
struct RoomCommand {bool background=false,stars=false;};
std::mutex roomLock;
std::unordered_map<VkShaderModule,RoomShader> roomShaders;
std::unordered_map<VkPipeline,RoomPipeline> roomPipelines;
std::unordered_map<VkCommandBuffer,RoomCommand> roomBackgroundCommands;
std::atomic<bool> roomViewMode{false},roomViewReady{false};
std::atomic<uint64_t> displayPeriodNs{1000000000ull/72};
void init(){if(realGet)return;if(program_invocation_short_name&&(!strcmp(program_invocation_short_name,"vrcompositor")||!strcmp(program_invocation_short_name,"vrcompositor.bin")))prctl(PR_SET_NAME,"vrcompositor",0,0,0);void* lib=dlopen("/opt/quest/libvulkan_freedreno.so",RTLD_NOW|RTLD_LOCAL);if(!lib){fprintf(stderr,"Quest Turnip: %s\n",dlerror());abort();}realGet=(PFN_vkGetInstanceProcAddr)dlsym(lib,"vk_icdGetInstanceProcAddr");auto negotiate=(VkResult(*)(uint32_t*))dlsym(lib,"vk_icdNegotiateLoaderICDInterfaceVersion");uint32_t version=5;if(negotiate)negotiate(&version);}
template<class T>T instanceFunction(const char* name){init();return reinterpret_cast<T>(realGet(activeInstance,name));}
template<class T>T deviceFunction(VkDevice device,const char* name){if(!realDeviceGet)realDeviceGet=instanceFunction<PFN_vkGetDeviceProcAddr>("vkGetDeviceProcAddr");return reinterpret_cast<T>(realDeviceGet(device,name));}
#define I(name) instanceFunction<PFN_##name>(#name)
#define D(name) deviceFunction<PFN_##name>(device,#name)
// Core command entry points are stable in the pinned Turnip ICD. Resolve each
// hot call site once instead of repeating the driver's name lookup per draw.
#define C(name) ([](){static const auto function=I(name);return function;})()
bool wsiExtension(const char* name){return !strcmp(name,"VK_KHR_surface")||!strcmp(name,"VK_KHR_xcb_surface")||!strcmp(name,"VK_KHR_xlib_surface")||!strcmp(name,"VK_KHR_display")||!strcmp(name,"VK_EXT_direct_mode_display")||!strcmp(name,"VK_EXT_acquire_xlib_display")||!strcmp(name,"VK_EXT_display_surface_counter")||!strcmp(name,"VK_KHR_swapchain")||!strcmp(name,"VK_KHR_present_id")||!strcmp(name,"VK_KHR_present_wait")||!strcmp(name,"VK_EXT_display_control");}
VkResult appendExtensions(std::vector<VkExtensionProperties>& values,const char* const* names,size_t size,uint32_t* count,VkExtensionProperties* output){for(size_t i=0;i<size;i++){bool found=false;for(const auto& value:values)if(!strcmp(value.extensionName,names[i]))found=true;if(!found){VkExtensionProperties value{};strncpy(value.extensionName,names[i],VK_MAX_EXTENSION_NAME_SIZE-1);value.specVersion=1;values.push_back(value);}}if(!output){*count=values.size();return VK_SUCCESS;}uint32_t n=std::min(*count,(uint32_t)values.size());memcpy(output,values.data(),n*sizeof(*output));*count=n;return n<values.size()?VK_INCOMPLETE:VK_SUCCESS;}
struct Surface {uint64_t magic=QUEST_GPU_MAGIC;};
#include "mirror-readback.h"
struct Swapchain {
 bool offscreen=false;
 QuestShareMirror* mirror=nullptr;void* mirrorPixels[QUEST_GPU_BUFFERS]{};bool mirrorMapFailed=false;
 uint64_t mirrorChecked=0,mirrorPublished=0,mirrorNext=0;uint32_t mirrorEyeWidth=0;std::vector<uint32_t> mirrorX,mirrorRow;
 uint64_t mirrorReport=0,mirrorCopyNs=0;uint32_t mirrorCaptures=0;
 MirrorReadback mirrorReadback;
 VkDevice device=VK_NULL_HANDLE;int socket=-1;QuestGpuHello hello{};VkFormat imageFormat=VK_FORMAT_UNDEFINED;VkImage images[QUEST_GPU_BUFFERS]{};VkDeviceMemory memory[QUEST_GPU_BUFFERS]{};uint32_t next=0;uint64_t frame=0,presented=0;std::mutex lock;std::condition_variable presentedCondition;
 uint32_t queueFamily=0;VkCommandPool pool=VK_NULL_HANDLE;VkCommandBuffer acquireCommands[QUEST_GPU_BUFFERS]{},releaseCommands[QUEST_GPU_BUFFERS]{};bool released[QUEST_GPU_BUFFERS]{};
 VkFence releaseFence=VK_NULL_HANDLE;
 quest::FramePool framePool;uint64_t presentIds[QUEST_GPU_BUFFERS]{};
 bool eventWaiting=false;
 VkResult waitReadable(uint64_t timeout){
  int milliseconds=timeout==UINT64_MAX?-1:static_cast<int>(std::min<uint64_t>(INT_MAX,timeout/1000000+(timeout%1000000!=0)));
  pollfd event{socket,POLLIN,0};int ready;do ready=::poll(&event,1,milliseconds);while(ready<0&&errno==EINTR);
  if(!ready)return VK_TIMEOUT;
  if(ready<0||event.revents&(POLLERR|POLLHUP|POLLNVAL))return VK_ERROR_SURFACE_LOST_KHR;
  return VK_SUCCESS;
 }
 VkResult receiveEvent(std::unique_lock<std::mutex>& guard,uint64_t timeout){
  // One socket reader wakes all waiters after updating shared state. Multiple
  // pollers could miss an event consumed by another thread and wait forever.
  if(eventWaiting){
   if(!timeout)return VK_TIMEOUT;
   // Recheck the caller's condition on every wake, even if a different reader
   // has already started waiting for a later event.
   if(timeout==UINT64_MAX)presentedCondition.wait(guard);
   else if(presentedCondition.wait_for(guard,std::chrono::nanoseconds(std::min<uint64_t>(timeout,INT64_MAX)))==std::cv_status::timeout)return VK_TIMEOUT;
   return VK_SUCCESS;
  }
  eventWaiting=true;
  guard.unlock();
  VkResult result=waitReadable(timeout);
  guard.lock();
  eventWaiting=false;presentedCondition.notify_all();
  if(result!=VK_SUCCESS)return result;
  QuestGpuRelease release{};ssize_t size=recv(socket,&release,sizeof(release),MSG_DONTWAIT|MSG_TRUNC);
  if(size<0&&(errno==EAGAIN||errno==EINTR))return VK_TIMEOUT;
  if(size!=sizeof(release)||release.slot>=QUEST_GPU_BUFFERS)return VK_ERROR_SURFACE_LOST_KHR;
  if(release.magic==QUEST_GPU_ACCEPTED_MAGIC){
   if(framePool.matches(release.slot,release.frame_id))presented=std::max(presented,presentIds[release.slot]);
  }else if(release.magic!=QUEST_GPU_MAGIC||!framePool.release(release.slot,release.frame_id))return VK_ERROR_SURFACE_LOST_KHR;
  return VK_SUCCESS;
 }
 const QuestTracking* tracking=nullptr;QuestGpuPresent inputPoses[QUEST_GPU_BUFFERS]{};
 std::chrono::steady_clock::time_point acquired[QUEST_GPU_BUFFERS]{},reportSince=std::chrono::steady_clock::now();
 double cpuMs=0,gpuMs=0,displayMs=0;uint32_t reportFrames=0;
 void sampleInputPose(uint32_t slot){
  inputPoses[slot]={};
  if(!tracking){
   int fd=open("/run/stearlight/quest/tracking",O_RDONLY|O_CLOEXEC);if(fd<0)return;
   struct stat data{};if(!fstat(fd,&data)&&data.st_size==sizeof(QuestTracking)){
    void* mapped=mmap(nullptr,sizeof(QuestTracking),PROT_READ,MAP_SHARED,fd,0);
    if(mapped!=MAP_FAILED)tracking=static_cast<const QuestTracking*>(mapped);
   }close(fd);if(!tracking)return;
  }
  QuestTracking state{};
  for(int attempt=0;attempt<8;attempt++){
   uint32_t sequence=__atomic_load_n(&tracking->sequence,__ATOMIC_ACQUIRE);if(sequence&1)continue;
   memcpy(&state,tracking,sizeof(state));
   if(sequence!=__atomic_load_n(&tracking->sequence,__ATOMIC_ACQUIRE))continue;
   if(state.magic!=QUEST_BRIDGE_MAGIC||state.version!=QUEST_BRIDGE_VERSION||
      (state.eyes[0].flags&3)!=3||(state.eyes[1].flags&3)!=3)return;
   auto& input=inputPoses[slot];input.pose_sequence=sequence;input.predicted_monotonic_ns=state.predicted_monotonic_ns;
   for(int eye=0;eye<2;eye++){
    memcpy(input.views[eye].position,state.eyes[eye].position,sizeof(input.views[eye].position));
    memcpy(input.views[eye].orientation,state.eyes[eye].orientation,sizeof(input.views[eye].orientation));
    memcpy(input.views[eye].fov,state.fov[eye],sizeof(input.views[eye].fov));
   }return;
  }
 }
 VkResult ownershipCommands(){
  VkDevice device=this->device;
  VkCommandPoolCreateInfo create{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};create.queueFamilyIndex=queueFamily;
  VkResult result=D(vkCreateCommandPool)(device,&create,nullptr,&pool);if(result!=VK_SUCCESS)return result;
  for(int slot=0;slot<QUEST_GPU_BUFFERS;slot++)for(int acquire=0;acquire<2;acquire++){
   VkCommandBuffer* command=acquire?&acquireCommands[slot]:&releaseCommands[slot];
   VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};allocate.commandPool=pool;allocate.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;allocate.commandBufferCount=1;
   result=D(vkAllocateCommandBuffers)(device,&allocate,command);if(result!=VK_SUCCESS)return result;
   VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};result=D(vkBeginCommandBuffer)(*command,&begin);if(result!=VK_SUCCESS)return result;
   VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};barrier.image=images[slot];barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
   barrier.oldLayout=acquire?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;barrier.newLayout=acquire?VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:VK_IMAGE_LAYOUT_GENERAL;
   barrier.srcQueueFamilyIndex=acquire?VK_QUEUE_FAMILY_FOREIGN_EXT:queueFamily;barrier.dstQueueFamilyIndex=acquire?queueFamily:VK_QUEUE_FAMILY_FOREIGN_EXT;
   barrier.srcAccessMask=acquire?0:VK_ACCESS_MEMORY_WRITE_BIT;barrier.dstAccessMask=acquire?(VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT):0;
   D(vkCmdPipelineBarrier)(*command,acquire?VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT:VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,acquire?VK_PIPELINE_STAGE_ALL_COMMANDS_BIT:VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,0,0,nullptr,0,nullptr,1,&barrier);
   result=D(vkEndCommandBuffer)(*command);if(result!=VK_SUCCESS)return result;
  }
  return VK_SUCCESS;
 }
 bool mirrorDue(){
  uint64_t now=quest_share_now();
  if(!mirror){if(now-mirrorChecked<250000000)return false;mirrorChecked=now;mirror=quest_share_map(0);if(!mirror)return false;}
  uint64_t requested=__atomic_load_n(&mirror->requested_ns,__ATOMIC_ACQUIRE);
  return requested&&now>=requested&&now-requested<=2000000000ull&&now>=mirrorNext;
 }
 VkRect2D mirrorCrop(uint32_t slot){
  const auto& buffer=hello.buffers[slot];uint32_t eyeWidth=buffer.width/2,eyeHeight=buffer.height;
  // The dashboard crop includes its bottom dock; immersive games stay centered.
  uint32_t width=std::min(eyeWidth,(uint32_t)((uint64_t)eyeHeight*QUEST_SHARE_WIDTH/QUEST_SHARE_HEIGHT));
  uint32_t height=std::min(eyeHeight,(uint32_t)((uint64_t)width*QUEST_SHARE_HEIGHT/QUEST_SHARE_WIDTH));
  uint32_t left=(eyeWidth-width)/2,top=(eyeHeight-height)/2;
  if(__atomic_load_n(&mirror->reserved,__ATOMIC_ACQUIRE)&QUEST_SHARE_DASHBOARD)top=std::min(eyeHeight-height,top+eyeHeight*12/100);
  return {{(int32_t)left,(int32_t)top},{width,height}};
 }
 bool recordMirror(uint32_t slot){
  if(!mirrorDue())return false;
  auto crop=mirrorCrop(slot);if(!crop.extent.width||!crop.extent.height)return false;
  return mirrorReadback.initialize(device,queueFamily,imageFormat)&&
   mirrorReadback.record(images[slot],crop.offset.x,crop.offset.y,crop.extent.width,crop.extent.height);
 }
 void publishMirror(uint32_t slot,bool copied){
  if(!mirrorDue()||(!copied&&mirrorMapFailed))return;
  uint64_t now=quest_share_now();
  const uint32_t* cached=copied?mirrorReadback.read():nullptr;
  if(copied&&!cached)return;
  if(!copied){
  if(!mirrorPixels[slot]){
   VkResult result=D(vkMapMemory)(device,memory[slot],0,VK_WHOLE_SIZE,0,&mirrorPixels[slot]);
   if(result!=VK_SUCCESS){mirrorPixels[slot]=nullptr;mirrorMapFailed=true;fprintf(stderr,"Quest sharing: GPU memory cannot be mapped (%d)\n",result);return;}
  }
  VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};range.memory=memory[slot];range.size=VK_WHOLE_SIZE;
  if(D(vkInvalidateMappedMemoryRanges)(device,1,&range)!=VK_SUCCESS)return;
  }
  const auto& buffer=hello.buffers[slot];uint32_t eyeWidth=buffer.width/2,eyeHeight=buffer.height;
  if(!eyeWidth||!eyeHeight)return;
  auto crop=mirrorCrop(slot);uint32_t cropWidth=crop.extent.width,cropHeight=crop.extent.height,sourceLeft=crop.offset.x,sourceTop=crop.offset.y;
  if(mirrorEyeWidth!=eyeWidth){mirrorX.resize(QUEST_SHARE_WIDTH);mirrorRow.resize(eyeWidth);for(unsigned x=0;x<QUEST_SHARE_WIDTH;x++)mirrorX[x]=sourceLeft+(uint64_t)x*cropWidth/QUEST_SHARE_WIDTH;mirrorEyeWidth=eyeWidth;}
  static const auto toSrgb=[]{std::vector<unsigned char> lut(256);for(int i=0;i<256;i++){double c=i/255.;lut[i]=(unsigned char)std::lround(255*(c<=0.0031308?12.92*c:1.055*std::pow(c,1./2.4)-0.055));}return lut;}();
  bool bgra=imageFormat==VK_FORMAT_B8G8R8A8_UNORM||imageFormat==VK_FORMAT_B8G8R8A8_SRGB;
  bool encoded=imageFormat==VK_FORMAT_B8G8R8A8_SRGB||imageFormat==VK_FORMAT_R8G8B8A8_SRGB;
  uint32_t sequence=__atomic_load_n(&mirror->sequence,__ATOMIC_RELAXED)&~1u;
  __atomic_store_n(&mirror->sequence,sequence+1,__ATOMIC_SEQ_CST);
  __atomic_thread_fence(__ATOMIC_SEQ_CST);
  if(cached){
   auto* destination=reinterpret_cast<uint32_t*>(mirror->pixels);
   for(size_t pixel=0;pixel<(size_t)QUEST_SHARE_WIDTH*QUEST_SHARE_HEIGHT;pixel++)destination[pixel]=cached[pixel]|0xff000000u;
  }else{
  const auto* source=static_cast<const unsigned char*>(mirrorPixels[slot]);
  for(uint32_t y=0;y<QUEST_SHARE_HEIGHT;y++){
   const auto* gpuRow=source+(size_t)(sourceTop+(uint64_t)y*cropHeight/QUEST_SHARE_HEIGHT)*buffer.stride*4;
   // Imported display memory is expensive for individual CPU reads. Read a
   // contiguous row once, then crop/convert from ordinary cached memory.
   memcpy(mirrorRow.data(),gpuRow,(size_t)eyeWidth*4);
   const auto* row=reinterpret_cast<const unsigned char*>(mirrorRow.data());
   auto* destination=mirror->pixels+(size_t)y*QUEST_SHARE_WIDTH*4;
   if(encoded&&bgra){
    auto* words=reinterpret_cast<uint32_t*>(destination);const auto* pixels=reinterpret_cast<const uint32_t*>(row);
    for(unsigned x=0;x<QUEST_SHARE_WIDTH;x++)words[x]=pixels[mirrorX[x]]|0xff000000u;
    continue;
   }
   for(uint32_t x=0;x<QUEST_SHARE_WIDTH;x++){
    const auto* pixel=row+(size_t)mirrorX[x]*4;
    unsigned char r=pixel[bgra?2:0],g=pixel[1],b=pixel[bgra?0:2];
    destination[0]=encoded?b:toSrgb[b];destination[1]=encoded?g:toSrgb[g];destination[2]=encoded?r:toSrgb[r];destination[3]=255;destination+=4;
   }
  }
  }
  __atomic_store_n(&mirror->frame,frame+1,__ATOMIC_RELAXED);__atomic_store_n(&mirror->sequence,sequence+2,__ATOMIC_RELEASE);
  syscall(SYS_futex,&mirror->sequence,FUTEX_WAKE,INT32_MAX,nullptr,nullptr,0);
  if(!mirrorPublished)fprintf(stderr,"QUEST_SHARE_MIRROR_READY %ux%u format=%d\n",QUEST_SHARE_WIDTH,QUEST_SHARE_HEIGHT,imageFormat);
  mirrorNext=(!mirrorNext||now-mirrorNext>100000000)?now+33333333:mirrorNext+33333333;
  mirrorPublished=now;
  mirrorCopyNs+=quest_share_now()-now;++mirrorCaptures;
  if(!mirrorReport)mirrorReport=now;
  if(now-mirrorReport>=5000000000ull){fprintf(stderr,"Quest sharing mirror: %.2f ms/capture, %.1f fps\n",mirrorCopyNs/1e6/mirrorCaptures,mirrorCaptures*1e9/(now-mirrorReport));mirrorReport=now;mirrorCopyNs=0;mirrorCaptures=0;}
 }
 ~Swapchain(){if(mirror)munmap(mirror,sizeof(*mirror));if(tracking)munmap(const_cast<QuestTracking*>(tracking),sizeof(QuestTracking));if(releaseFence)D(vkDestroyFence)(device,releaseFence,nullptr);if(pool)D(vkDestroyCommandPool)(device,pool,nullptr);if(socket>=0)close(socket);for(int i=0;i<QUEST_GPU_BUFFERS;i++){if(mirrorPixels[i])D(vkUnmapMemory)(device,memory[i]);if(images[i])D(vkDestroyImage)(device,images[i],nullptr);if(memory[i])D(vkFreeMemory)(device,memory[i],nullptr);}}
 VkResult initializeCapture(VkDevice value,const VkSwapchainCreateInfoKHR* info){
  device=value;queueFamily=activeQueueFamily;imageFormat=info->imageFormat;offscreen=true;
  VkPhysicalDeviceMemoryProperties properties{};I(vkGetPhysicalDeviceMemoryProperties)(activePhysical,&properties);
  for(int slot=0;slot<QUEST_GPU_BUFFERS;slot++){
   VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};image.imageType=VK_IMAGE_TYPE_2D;image.format=info->imageFormat;
   image.extent={info->imageExtent.width,info->imageExtent.height,1};image.mipLevels=1;image.arrayLayers=info->imageArrayLayers;
   image.samples=VK_SAMPLE_COUNT_1_BIT;image.tiling=VK_IMAGE_TILING_OPTIMAL;image.usage=info->imageUsage;
   image.sharingMode=info->imageSharingMode;image.queueFamilyIndexCount=info->queueFamilyIndexCount;image.pQueueFamilyIndices=info->pQueueFamilyIndices;
   if(info->flags&VK_SWAPCHAIN_CREATE_MUTABLE_FORMAT_BIT_KHR){
    image.flags=VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT|VK_IMAGE_CREATE_EXTENDED_USAGE_BIT;
    for(auto node=(const VkBaseInStructure*)info->pNext;node;node=node->pNext)if(node->sType==VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO){image.pNext=node;break;}
   }
   VkResult result=D(vkCreateImage)(device,&image,nullptr,&images[slot]);if(result!=VK_SUCCESS)return result;
   VkMemoryRequirements requirements{};D(vkGetImageMemoryRequirements)(device,images[slot],&requirements);
   uint32_t type=UINT32_MAX;
   for(uint32_t i=0;i<properties.memoryTypeCount;i++)if(requirements.memoryTypeBits&(1u<<i)){
    if(type==UINT32_MAX)type=i;if(properties.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT){type=i;break;}
   }
   if(type==UINT32_MAX)return VK_ERROR_OUT_OF_DEVICE_MEMORY;
   VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};allocate.allocationSize=requirements.size;allocate.memoryTypeIndex=type;
   result=D(vkAllocateMemory)(device,&allocate,nullptr,&memory[slot]);if(result!=VK_SUCCESS)return result;
   result=D(vkBindImageMemory)(device,images[slot],memory[slot],0);if(result!=VK_SUCCESS)return result;
  }
  fprintf(stderr,"QUEST_STEAM_CAPTURE_SWAPCHAIN_READY %ux%u format=%d\n",info->imageExtent.width,info->imageExtent.height,info->imageFormat);
  return VK_SUCCESS;
 }
 VkResult initialize(VkDevice value,VkFormat format){device=value;queueFamily=activeQueueFamily;imageFormat=format;socket=::socket(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0);sockaddr_un address{};address.sun_family=AF_UNIX;strcpy(address.sun_path,"/run/stearlight/quest/gpu.sock");int connected=-1;for(int attempt=0;attempt<200&&socket>=0;attempt++){connected=connect(socket,(sockaddr*)&address,sizeof(address));if(!connected)break;if(errno!=ECONNREFUSED&&errno!=ENOENT)break;usleep(50000);}if(socket<0||connected){fprintf(stderr,"Quest GPU socket connect: %s (%d)\n",strerror(errno),errno);return VK_ERROR_INITIALIZATION_FAILED;}timeval timeout{15,0};setsockopt(socket,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));int fds[QUEST_GPU_BUFFERS]={-1,-1,-1};char control[CMSG_SPACE(sizeof(fds))]{};iovec io{&hello,sizeof(hello)};msghdr message{};message.msg_iov=&io;message.msg_iovlen=1;message.msg_control=control;message.msg_controllen=sizeof(control);ssize_t received=recvmsg(socket,&message,MSG_CMSG_CLOEXEC);if(received!=sizeof(hello)||hello.magic!=QUEST_GPU_MAGIC||hello.version!=QUEST_GPU_VERSION||hello.count!=QUEST_GPU_BUFFERS){fprintf(stderr,"Quest GPU hello: bytes=%zd expected=%zu errno=%d magic=%x count=%u\n",received,sizeof(hello),errno,hello.magic,hello.count);return VK_ERROR_INITIALIZATION_FAILED;}cmsghdr* header=CMSG_FIRSTHDR(&message);if(!header||header->cmsg_level!=SOL_SOCKET||header->cmsg_type!=SCM_RIGHTS||header->cmsg_len!=CMSG_LEN(sizeof(fds)))return VK_ERROR_INITIALIZATION_FAILED;memcpy(fds,CMSG_DATA(header),sizeof(fds));if(!framePool.initialize(hello.available_mask))return VK_ERROR_INITIALIZATION_FAILED;
  for(int i=0;i<QUEST_GPU_BUFFERS;i++){
   const auto& buffer=hello.buffers[i];VkExternalMemoryImageCreateInfo external{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};external.handleTypes=VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};image.pNext=&external;image.imageType=VK_IMAGE_TYPE_2D;image.format=format;image.extent={buffer.stride,buffer.height,1};image.mipLevels=1;image.arrayLayers=1;image.samples=VK_SAMPLE_COUNT_1_BIT;image.tiling=VK_IMAGE_TILING_LINEAR;image.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;image.sharingMode=VK_SHARING_MODE_EXCLUSIVE;VkResult result=D(vkCreateImage)(device,&image,nullptr,&images[i]);if(result!=VK_SUCCESS)return result;VkImageSubresource sub{VK_IMAGE_ASPECT_COLOR_BIT,0,0};VkSubresourceLayout layout;D(vkGetImageSubresourceLayout)(device,images[i],&sub,&layout);VkMemoryRequirements requirement;D(vkGetImageMemoryRequirements)(device,images[i],&requirement);fprintf(stderr,"Quest DMA-BUF import image %d: row=%llu expected=%u bytes=%llu allocation=%llu\n",i,(unsigned long long)layout.rowPitch,buffer.stride*4,(unsigned long long)requirement.size,(unsigned long long)buffer.allocation_size);if(layout.rowPitch!=buffer.stride*4||layout.offset!=0||requirement.size>buffer.allocation_size)return VK_ERROR_FORMAT_NOT_SUPPORTED;
   VkMemoryFdPropertiesKHR properties{VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR};result=D(vkGetMemoryFdPropertiesKHR)(device,VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,fds[i],&properties);if(result!=VK_SUCCESS)return result;uint32_t bits=properties.memoryTypeBits&requirement.memoryTypeBits;if(!bits)return VK_ERROR_INVALID_EXTERNAL_HANDLE;VkImportMemoryFdInfoKHR import{VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR};import.handleType=VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;import.fd=fds[i];VkMemoryDedicatedAllocateInfo dedicated{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};dedicated.pNext=&import;dedicated.image=images[i];VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};allocate.pNext=&dedicated;allocate.allocationSize=buffer.allocation_size;allocate.memoryTypeIndex=__builtin_ctz(bits);result=D(vkAllocateMemory)(device,&allocate,nullptr,&memory[i]);if(result!=VK_SUCCESS)return result;fds[i]=-1;result=D(vkBindImageMemory)(device,images[i],memory[i],0);if(result!=VK_SUCCESS)return result;
  }fprintf(stderr,"QUEST_GPU_SWAPCHAIN_READY %ux%u\n",hello.buffers[0].width,hello.buffers[0].height);return ownershipCommands();
 }
};
template<class T,class H>T* object(H handle){return reinterpret_cast<T*>((uintptr_t)handle);}
}
extern "C" {
VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceExtensionProperties(const char* layer,uint32_t* count,VkExtensionProperties* output){init();auto real=(PFN_vkEnumerateInstanceExtensionProperties)realGet(VK_NULL_HANDLE,"vkEnumerateInstanceExtensionProperties");uint32_t n=0;VkResult result=real(layer,&n,nullptr);if(result!=VK_SUCCESS)return result;std::vector<VkExtensionProperties> values(n);result=real(layer,&n,values.data());if(result!=VK_SUCCESS)return result;const char* names[]={"VK_KHR_surface","VK_KHR_xcb_surface","VK_KHR_xlib_surface","VK_KHR_display","VK_EXT_direct_mode_display","VK_EXT_acquire_xlib_display","VK_EXT_display_surface_counter"};return appendExtensions(values,names,std::size(names),count,output);}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateInstance(const VkInstanceCreateInfo* info,const VkAllocationCallbacks* callbacks,VkInstance* instance){init();if(isCompositor())unlink("/run/stearlight/quest/passthrough.composition-ready");std::vector<const char*> names;for(uint32_t i=0;i<info->enabledExtensionCount;i++)if(!wsiExtension(info->ppEnabledExtensionNames[i]))names.push_back(info->ppEnabledExtensionNames[i]);VkInstanceCreateInfo copy=*info;copy.enabledExtensionCount=names.size();copy.ppEnabledExtensionNames=names.data();auto create=(PFN_vkCreateInstance)realGet(VK_NULL_HANDLE,"vkCreateInstance");VkResult result=create(&copy,callbacks,instance);if(result==VK_SUCCESS)activeInstance=*instance;return result;}
VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceExtensionProperties(VkPhysicalDevice physical,const char* layer,uint32_t* count,VkExtensionProperties* output){activePhysical=physical;uint32_t n=0;VkResult result=I(vkEnumerateDeviceExtensionProperties)(physical,layer,&n,nullptr);if(result!=VK_SUCCESS)return result;std::vector<VkExtensionProperties> values(n);result=I(vkEnumerateDeviceExtensionProperties)(physical,layer,&n,values.data());if(result!=VK_SUCCESS)return result;const char* names[]={"VK_KHR_swapchain","VK_KHR_present_id","VK_KHR_present_wait","VK_EXT_display_control"};return appendExtensions(values,names,std::size(names),count,output);}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDevice(VkPhysicalDevice physical,const VkDeviceCreateInfo* info,const VkAllocationCallbacks* callbacks,VkDevice* device){activePhysical=physical;if(info->queueCreateInfoCount)activeQueueFamily=info->pQueueCreateInfos[0].queueFamilyIndex;std::vector<const char*> names;for(uint32_t i=0;i<info->enabledExtensionCount;i++)if(!wsiExtension(info->ppEnabledExtensionNames[i]))names.push_back(info->ppEnabledExtensionNames[i]);for(const char* required:{"VK_EXT_external_memory_dma_buf","VK_KHR_external_memory_fd","VK_EXT_queue_family_foreign"}){bool found=false;for(const char* name:names)if(!strcmp(name,required)){found=true;break;}if(!found)names.push_back(required);}for(auto node=(const VkBaseInStructure*)info->pNext;node;node=node->pNext)fprintf(stderr,"Quest device feature structure %d\n",node->sType);VkDeviceCreateInfo copy=*info;copy.enabledExtensionCount=names.size();copy.ppEnabledExtensionNames=names.data();VkResult result=I(vkCreateDevice)(physical,&copy,callbacks,device);return result;}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateXcbSurfaceKHR(VkInstance,const VkXcbSurfaceCreateInfoKHR*,const VkAllocationCallbacks*,VkSurfaceKHR* surface){*surface=(VkSurfaceKHR)(uintptr_t)new Surface;return VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateXlibSurfaceKHR(VkInstance,const VkXlibSurfaceCreateInfoKHR*,const VkAllocationCallbacks*,VkSurfaceKHR* surface){*surface=(VkSurfaceKHR)(uintptr_t)new Surface;return VK_SUCCESS;}
VKAPI_ATTR void VKAPI_CALL vkDestroySurfaceKHR(VkInstance,VkSurfaceKHR surface,const VkAllocationCallbacks*){delete object<Surface>(surface);}
VKAPI_ATTR VkBool32 VKAPI_CALL vkGetPhysicalDeviceXcbPresentationSupportKHR(VkPhysicalDevice,uint32_t,xcb_connection_t*,xcb_visualid_t){return VK_TRUE;}
VKAPI_ATTR VkBool32 VKAPI_CALL vkGetPhysicalDeviceXlibPresentationSupportKHR(VkPhysicalDevice,uint32_t,Display*,VisualID){return VK_TRUE;}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice,uint32_t,VkSurfaceKHR,VkBool32* supported){*supported=VK_TRUE;return VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice,VkSurfaceKHR,VkSurfaceCapabilitiesKHR* caps){*caps={};caps->minImageCount=caps->maxImageCount=QUEST_GPU_BUFFERS;caps->currentExtent=caps->minImageExtent=caps->maxImageExtent=displayExtent();if(isSteamCapture()){caps->currentExtent={UINT32_MAX,UINT32_MAX};caps->minImageExtent={1,1};caps->maxImageExtent={8192,8192};}caps->maxImageArrayLayers=1;caps->supportedTransforms=caps->currentTransform=VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;caps->supportedCompositeAlpha=VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;caps->supportedUsageFlags=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;return VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice,VkSurfaceKHR,uint32_t* count,VkSurfaceFormatKHR* output){fprintf(stderr,"Quest surface formats capacity=%u output=%p\n",*count,(void*)output);const VkSurfaceFormatKHR formats[]={{VK_FORMAT_B8G8R8A8_UNORM,VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},{VK_FORMAT_B8G8R8A8_SRGB,VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},{VK_FORMAT_R8G8B8A8_UNORM,VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},{VK_FORMAT_R8G8B8A8_SRGB,VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}};if(!output){*count=4;return VK_SUCCESS;}uint32_t n=std::min(*count,4u);memcpy(output,formats,n*sizeof(*output));*count=n;return n<4?VK_INCOMPLETE:VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice,VkSurfaceKHR,uint32_t* count,VkPresentModeKHR* modes){if(modes&&*count){modes[0]=VK_PRESENT_MODE_FIFO_KHR;*count=1;}else *count=1;return VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceDisplayPropertiesKHR(VkPhysicalDevice,uint32_t* count,VkDisplayPropertiesKHR* output){if(!output){*count=1;return VK_SUCCESS;}if(!*count)return VK_INCOMPLETE;output[0]={};output[0].display=(VkDisplayKHR)1;output[0].displayName="Stearlight Quest OpenXR bridge";output[0].physicalResolution=displayExtent();output[0].supportedTransforms=VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;output[0].persistentContent=VK_TRUE;*count=1;return VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceDisplayPlanePropertiesKHR(VkPhysicalDevice,uint32_t* count,VkDisplayPlanePropertiesKHR* output){if(!output){*count=1;return VK_SUCCESS;}if(!*count)return VK_INCOMPLETE;output[0]={(VkDisplayKHR)1,0};*count=1;return VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL vkGetDisplayPlaneSupportedDisplaysKHR(VkPhysicalDevice,uint32_t,uint32_t* count,VkDisplayKHR* output){if(!output){*count=1;return VK_SUCCESS;}if(!*count)return VK_INCOMPLETE;output[0]=(VkDisplayKHR)1;*count=1;return VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL vkGetDisplayModePropertiesKHR(VkPhysicalDevice,VkDisplayKHR,uint32_t* count,VkDisplayModePropertiesKHR* output){if(!output){*count=1;return VK_SUCCESS;}if(!*count)return VK_INCOMPLETE;output[0]={(VkDisplayModeKHR)1,{displayExtent(),72000}};*count=1;return VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDisplayModeKHR(VkPhysicalDevice,VkDisplayKHR,const VkDisplayModeCreateInfoKHR*,const VkAllocationCallbacks*,VkDisplayModeKHR* mode){*mode=(VkDisplayModeKHR)1;return VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL vkGetDisplayPlaneCapabilitiesKHR(VkPhysicalDevice,VkDisplayModeKHR,uint32_t,VkDisplayPlaneCapabilitiesKHR* output){*output={};output->supportedAlpha=VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR;output->minSrcExtent=output->maxSrcExtent=output->minDstExtent=output->maxDstExtent=displayExtent();return VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDisplayPlaneSurfaceKHR(VkInstance,const VkDisplaySurfaceCreateInfoKHR*,const VkAllocationCallbacks*,VkSurfaceKHR* surface){*surface=(VkSurfaceKHR)(uintptr_t)new Surface;return VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL vkAcquireXlibDisplayEXT(VkPhysicalDevice,Display*,VkDisplayKHR){return VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL vkGetRandROutputDisplayEXT(VkPhysicalDevice,Display*,unsigned long,VkDisplayKHR* display){*display=(VkDisplayKHR)1;return VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL vkReleaseDisplayEXT(VkPhysicalDevice,VkDisplayKHR){return VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceCapabilities2EXT(VkPhysicalDevice physical,VkSurfaceKHR surface,VkSurfaceCapabilities2EXT* output){VkSurfaceCapabilitiesKHR caps{};VkResult result=vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical,surface,&caps);output->minImageCount=caps.minImageCount;output->maxImageCount=caps.maxImageCount;output->currentExtent=caps.currentExtent;output->minImageExtent=caps.minImageExtent;output->maxImageExtent=caps.maxImageExtent;output->maxImageArrayLayers=caps.maxImageArrayLayers;output->supportedTransforms=caps.supportedTransforms;output->currentTransform=caps.currentTransform;output->supportedCompositeAlpha=caps.supportedCompositeAlpha;output->supportedUsageFlags=caps.supportedUsageFlags;output->supportedSurfaceCounters=0;return result;}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateSwapchainKHR(VkDevice device,const VkSwapchainCreateInfoKHR* info,const VkAllocationCallbacks*,VkSwapchainKHR* output){
 bool capture=isSteamCapture();
 if(!capture&&!isCompositor()&&(!program_invocation_short_name||strcmp(program_invocation_short_name,"gpu-probe"))){fprintf(stderr,"Quest display swapchain is reserved for vrcompositor (%s)\n",program_invocation_short_name?program_invocation_short_name:"unknown");return VK_ERROR_INITIALIZATION_FAILED;}
 fprintf(stderr,"Quest swapchain requested format=%d extent=%ux%u usage=%x flags=%x\n",info->imageFormat,info->imageExtent.width,info->imageExtent.height,info->imageUsage,info->flags);
 auto swap=new Swapchain;VkResult result=capture?swap->initializeCapture(device,info):swap->initialize(device,info->imageFormat);
 if(result!=VK_SUCCESS){delete swap;return result;}*output=(VkSwapchainKHR)(uintptr_t)swap;return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkDestroySwapchainKHR(VkDevice,VkSwapchainKHR swap,const VkAllocationCallbacks*){delete object<Swapchain>(swap);}
VKAPI_ATTR VkResult VKAPI_CALL vkGetSwapchainImagesKHR(VkDevice,VkSwapchainKHR handle,uint32_t* count,VkImage* images){auto swap=object<Swapchain>(handle);if(!images){*count=QUEST_GPU_BUFFERS;return VK_SUCCESS;}uint32_t n=std::min(*count,(uint32_t)QUEST_GPU_BUFFERS);memcpy(images,swap->images,n*sizeof(VkImage));*count=n;return n<QUEST_GPU_BUFFERS?VK_INCOMPLETE:VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL vkAcquireNextImageKHR(VkDevice device,VkSwapchainKHR handle,uint64_t timeout,VkSemaphore semaphore,VkFence fence,uint32_t* index){
 roomViewMode=roomViewReady&&access("/run/stearlight/quest/passthrough.enabled",F_OK)==0;
 auto swap=object<Swapchain>(handle);std::unique_lock<std::mutex> guard(swap->lock);
 if(swap->offscreen)*index=swap->next++%QUEST_GPU_BUFFERS;
 else{
  auto start=std::chrono::steady_clock::now();
  for(;;){
   int slot=swap->framePool.acquire();if(slot>=0){*index=slot;break;}
   uint64_t elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
   uint64_t left=timeout==UINT64_MAX?timeout:elapsed>=timeout?0:timeout-elapsed;
   VkResult result=swap->receiveEvent(guard,left);
   if(result==VK_TIMEOUT){int slot=swap->framePool.acquire();if(slot>=0){*index=slot;break;}return timeout?VK_TIMEOUT:VK_NOT_READY;}
   if(result!=VK_SUCCESS)return result;
  }
 }
 swap->sampleInputPose(*index);
 swap->acquired[*index]=std::chrono::steady_clock::now();
 VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};if(semaphore){submit.signalSemaphoreCount=1;submit.pSignalSemaphores=&semaphore;}
 if(swap->released[*index]){submit.commandBufferCount=1;submit.pCommandBuffers=&swap->acquireCommands[*index];}
 VkQueue queue;D(vkGetDeviceQueue)(device,swap->queueFamily,0,&queue);VkResult result=D(vkQueueSubmit)(queue,1,&submit,fence);if(result==VK_SUCCESS)swap->released[*index]=false;return result;
}
VKAPI_ATTR VkResult VKAPI_CALL vkAcquireNextImage2KHR(VkDevice device,const VkAcquireNextImageInfoKHR* info,uint32_t* index){return vkAcquireNextImageKHR(device,info->swapchain,info->timeout,info->semaphore,info->fence,index);}
VKAPI_ATTR VkResult VKAPI_CALL vkQueuePresentKHR(VkQueue queue,const VkPresentInfoKHR* info){
 for(uint32_t i=0;i<info->swapchainCount;i++){
  auto swap=object<Swapchain>(info->pSwapchains[i]);VkDevice device=swap->device;
  std::lock_guard<std::mutex> guard(swap->lock);uint32_t slot=info->pImageIndices[i];
  if(swap->offscreen){
   // Steam's capture/conversion window owns ordinary GPU images. It must not
   // connect to the compositor's Android display socket or replace its eyes.
   std::vector<VkPipelineStageFlags> stages(info->waitSemaphoreCount,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
   VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.waitSemaphoreCount=info->waitSemaphoreCount;
   submit.pWaitSemaphores=info->pWaitSemaphores;submit.pWaitDstStageMask=stages.data();
   VkResult result=VK_SUCCESS;
   if(!swap->releaseFence){VkFenceCreateInfo create{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};result=D(vkCreateFence)(device,&create,nullptr,&swap->releaseFence);}
   else result=D(vkResetFences)(device,1,&swap->releaseFence);
   if(result==VK_SUCCESS)result=D(vkQueueSubmit)(queue,1,&submit,swap->releaseFence);
   if(result==VK_SUCCESS)result=D(vkWaitForFences)(device,1,&swap->releaseFence,VK_TRUE,UINT64_MAX);
   if(result==VK_SUCCESS){
    for(auto node=(const VkBaseInStructure*)info->pNext;node;node=node->pNext)if(node->sType==VK_STRUCTURE_TYPE_PRESENT_ID_KHR){auto ids=(const VkPresentIdKHR*)node;if(i<ids->swapchainCount)swap->presented=std::max(swap->presented,ids->pPresentIds[i]);}
    swap->presentedCondition.notify_all();
   }
   if(info->pResults)info->pResults[i]=result;if(result!=VK_SUCCESS)return result;continue;
  }
  auto start=std::chrono::steady_clock::now();
  swap->cpuMs+=std::chrono::duration<double,std::milli>(start-swap->acquired[slot]).count();
  std::vector<VkPipelineStageFlags> stages(info->waitSemaphoreCount,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
  VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.waitSemaphoreCount=info->waitSemaphoreCount;
  submit.pWaitSemaphores=info->pWaitSemaphores;submit.pWaitDstStageMask=stages.data();
  bool mirrorCopied=swap->recordMirror(slot);
  VkCommandBuffer commands[2]={mirrorCopied?swap->mirrorReadback.command:swap->releaseCommands[slot],swap->releaseCommands[slot]};
  submit.commandBufferCount=mirrorCopied?2:1;submit.pCommandBuffers=commands;
  VkResult result=VK_SUCCESS;
  if(!swap->releaseFence){VkFenceCreateInfo create{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};result=D(vkCreateFence)(device,&create,nullptr,&swap->releaseFence);}
  else result=D(vkResetFences)(device,1,&swap->releaseFence);
  if(result==VK_SUCCESS)result=D(vkQueueSubmit)(queue,1,&submit,swap->releaseFence);
  // Wait for this image's release command, not work submitted afterward by
  // another compositor thread. The Android/foreign ownership barrier remains.
  if(result==VK_SUCCESS)result=D(vkWaitForFences)(device,1,&swap->releaseFence,VK_TRUE,UINT64_MAX);
  auto rendered=std::chrono::steady_clock::now();
  swap->gpuMs+=std::chrono::duration<double,std::milli>(rendered-start).count();
  if(result==VK_SUCCESS){
   swap->publishMirror(slot,mirrorCopied);
   swap->released[slot]=true;QuestGpuPresent present=swap->inputPoses[slot];
   present.magic=QUEST_GPU_MAGIC;present.slot=slot;present.frame_id=++swap->frame;
   present.vk_format=static_cast<uint32_t>(swap->imageFormat);
   swap->framePool.submitted(slot,present.frame_id);swap->presentIds[slot]=0;
   for(auto node=(const VkBaseInStructure*)info->pNext;node;node=node->pNext)if(node->sType==VK_STRUCTURE_TYPE_PRESENT_ID_KHR){auto ids=(const VkPresentIdKHR*)node;if(i<ids->swapchainCount)swap->presentIds[slot]=ids->pPresentIds[i];}
   if(present.frame_id<=2)fprintf(stderr,"Quest present frame=%llu format=%u bytes=%zu\n",(unsigned long long)present.frame_id,present.vk_format,sizeof(present));
   if(send(swap->socket,&present,sizeof(present),MSG_NOSIGNAL)!=sizeof(present))result=VK_ERROR_SURFACE_LOST_KHR;
  }
  if(result==VK_SUCCESS){
   auto done=std::chrono::steady_clock::now();swap->displayMs+=std::chrono::duration<double,std::milli>(done-rendered).count();
   ++swap->reportFrames;double elapsed=std::chrono::duration<double>(done-swap->reportSince).count();
   if(elapsed>=10){fprintf(stderr,"Quest frame timing: %.1f fps; record %.2f ms, GPU wait %.2f ms, display wait %.2f ms\n",swap->reportFrames/elapsed,swap->cpuMs/swap->reportFrames,swap->gpuMs/swap->reportFrames,swap->displayMs/swap->reportFrames);swap->cpuMs=swap->gpuMs=swap->displayMs=0;swap->reportFrames=0;swap->reportSince=done;}
   // Native acceptance preserves the previous handoff-completion semantics.
   swap->presentedCondition.notify_all();
  }
  if(info->pResults)info->pResults[i]=result;if(result!=VK_SUCCESS)return result;
 }
 return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFeatures2(VkPhysicalDevice physical,VkPhysicalDeviceFeatures2* features){I(vkGetPhysicalDeviceFeatures2)(physical,features);for(auto node=(VkBaseOutStructure*)features->pNext;node;node=node->pNext){if(node->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR)((VkPhysicalDevicePresentIdFeaturesKHR*)node)->presentId=VK_TRUE;if(node->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR)((VkPhysicalDevicePresentWaitFeaturesKHR*)node)->presentWait=VK_TRUE;}}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFeatures2KHR(VkPhysicalDevice physical,VkPhysicalDeviceFeatures2* features){vkGetPhysicalDeviceFeatures2(physical,features);}
VKAPI_ATTR VkResult VKAPI_CALL vkWaitForPresentKHR(VkDevice,VkSwapchainKHR handle,uint64_t id,uint64_t timeout){
 auto swap=object<Swapchain>(handle);std::unique_lock<std::mutex> lock(swap->lock);
 auto ready=[&]{return swap->presented>=id;};if(ready())return VK_SUCCESS;
 if(swap->offscreen){if(!timeout)return VK_TIMEOUT;if(timeout==UINT64_MAX){swap->presentedCondition.wait(lock,ready);return VK_SUCCESS;}return swap->presentedCondition.wait_for(lock,std::chrono::nanoseconds(timeout),ready)?VK_SUCCESS:VK_TIMEOUT;}
 auto start=std::chrono::steady_clock::now();
 while(!ready()){
  uint64_t elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
  uint64_t left=timeout==UINT64_MAX?timeout:elapsed>=timeout?0:timeout-elapsed;
  VkResult result=swap->receiveEvent(lock,left);if(ready())return VK_SUCCESS;if(result!=VK_SUCCESS)return result;
 }
 return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateSemaphore(VkDevice device,const VkSemaphoreCreateInfo* info,const VkAllocationCallbacks* allocator,VkSemaphore* value){VkResult result=D(vkCreateSemaphore)(device,info,allocator,value);if(result<0){fprintf(stderr,"Quest Vulkan failure vkCreateSemaphore: %d\n",result);for(auto node=(const VkBaseInStructure*)info->pNext;node;node=node->pNext){fprintf(stderr,"Quest semaphore structure %d\n",node->sType);if(node->sType==VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO)fprintf(stderr,"Quest semaphore export handles=%x\n",((const VkExportSemaphoreCreateInfo*)node)->handleTypes);if(node->sType==VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO)fprintf(stderr,"Quest semaphore type=%d value=%llu\n",((const VkSemaphoreTypeCreateInfo*)node)->semaphoreType,(unsigned long long)((const VkSemaphoreTypeCreateInfo*)node)->initialValue);}}return result;}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateFence(VkDevice device,const VkFenceCreateInfo* info,const VkAllocationCallbacks* allocator,VkFence* value){VkResult result=D(vkCreateFence)(device,info,allocator,value);if(result<0)fprintf(stderr,"Quest Vulkan failure vkCreateFence: %d\n",result);return result;}
VKAPI_ATTR VkResult VKAPI_CALL vkRegisterDisplayEventEXT(VkDevice device,VkDisplayKHR,const VkDisplayEventInfoEXT*,const VkAllocationCallbacks* allocator,VkFence* fence){
 static std::once_flag logged;std::call_once(logged,[]{fprintf(stderr,"Quest OpenXR display-event pacing active\n");});
 // Turnip has no Android display event source. SteamVR otherwise spins its
 // VulkanVblankThread through the unsupported display-control entry point.
 // A genuine signaled Vulkan fence preserves normal Turnip wait/destruction.
 thread_local const QuestTracking* timing=nullptr;thread_local QuestDisplayPulse pulse;
 if(!timing){
  int fd=open("/run/stearlight/quest/tracking",O_RDONLY|O_CLOEXEC);
  if(fd>=0){struct stat data{};if(!fstat(fd,&data)&&data.st_size==sizeof(QuestTracking)){void* mapped=mmap(nullptr,sizeof(QuestTracking),PROT_READ,MAP_SHARED,fd,0);if(mapped!=MAP_FAILED)timing=static_cast<const QuestTracking*>(mapped);}close(fd);}
 }
 QuestTracking sample{};
 if(timing)for(int attempt=0;attempt<8;attempt++){uint32_t before=__atomic_load_n(&timing->sequence,__ATOMIC_ACQUIRE);if(before&1)continue;memcpy(&sample,timing,sizeof(sample));if(before==__atomic_load_n(&timing->sequence,__ATOMIC_ACQUIRE))break;sample={};}
 float rate=sample.refresh_hz;uint64_t period=1000000000ull/(rate>=60&&rate<=144?rate:72);
 displayPeriodNs=period;
 if(!pulse.wait(timing,period)){
  // No running OpenXR session: remain bounded and avoid a vblank busy loop.
  timespec pause{static_cast<time_t>(period/1000000000),static_cast<long>(period%1000000000)};
  while(nanosleep(&pause,&pause)&&errno==EINTR){}
 }
 VkFenceCreateInfo create{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};create.flags=VK_FENCE_CREATE_SIGNALED_BIT;
 return D(vkCreateFence)(device,&create,allocator,fence);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetSwapchainCounterEXT(VkDevice,VkSwapchainKHR,VkSurfaceCounterFlagBitsEXT,uint64_t* counter){
 timespec now{};clock_gettime(CLOCK_MONOTONIC,&now);
 *counter=(uint64_t(now.tv_sec)*1000000000+now.tv_nsec)/displayPeriodNs.load();return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkDisplayPowerControlEXT(VkDevice,VkDisplayKHR,const VkDisplayPowerInfoEXT*){return VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL vkRegisterDeviceEventEXT(VkDevice device,const VkDeviceEventInfoEXT*,const VkAllocationCallbacks* allocator,VkFence* fence){
 // The Android-owned display never hotplugs inside this Linux session.
 VkFenceCreateInfo create{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};return D(vkCreateFence)(device,&create,allocator,fence);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateImage(VkDevice device,const VkImageCreateInfo* info,const VkAllocationCallbacks* allocator,VkImage* value){
 quest::LinearVideoImage linear;
 // Correct both the producer and SteamVR's import of the same video FD.
 // The private decoder negotiates NV12, but the player retains its compressed
 // modifier. Other formats and valid plane layouts pass through unchanged.
 bool videoPlayer=program_invocation_short_name&&!strcmp(program_invocation_short_name,"vrlink")&&getenv("QUEST_VIDEO_STATEFUL");
 if((videoPlayer||isCompositor())&&linear.prepare(*info))info=&linear.image;
 VkResult result=D(vkCreateImage)(device,info,allocator,value);if(result<0)fprintf(stderr,"Quest Vulkan failure vkCreateImage: %d\n",result);return result;
}
VKAPI_ATTR VkResult VKAPI_CALL vkAllocateMemory(VkDevice device,const VkMemoryAllocateInfo* info,const VkAllocationCallbacks* allocator,VkDeviceMemory* value){VkResult result=D(vkAllocateMemory)(device,info,allocator,value);if(result<0)fprintf(stderr,"Quest Vulkan failure vkAllocateMemory: %d\n",result);return result;}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateGraphicsPipelines(VkDevice device,VkPipelineCache cache,uint32_t count,const VkGraphicsPipelineCreateInfo* info,const VkAllocationCallbacks* allocator,VkPipeline* values){
 auto create=D(vkCreateGraphicsPipelines);VkResult result=create(device,cache,count,info,allocator,values);
 if(result<0)fprintf(stderr,"Quest Vulkan failure vkCreateGraphicsPipelines: %d\n",result);
 if(!isCompositor()||result!=VK_SUCCESS)return result;
 std::lock_guard<std::mutex> guard(roomLock);
 for(uint32_t i=0;i<count;i++){
  RoomPipeline pipeline;std::vector<VkPipelineShaderStageCreateInfo> stages(info[i].pStages,info[i].pStages+info[i].stageCount);bool changed=false;
  for(auto& stage:stages)if(stage.stage==VK_SHADER_STAGE_FRAGMENT_BIT){
   auto found=roomShaders.find(stage.module);if(found==roomShaders.end())continue;
   pipeline.background=found->second.background;
   pipeline.stars=found->second.stars&&info[i].pDepthStencilState&&
    info[i].pDepthStencilState->depthTestEnable&&!info[i].pDepthStencilState->depthWriteEnable;
   if(found->second.alternate){stage.module=found->second.alternate;changed=true;}
  }
  if(changed){auto copy=info[i];copy.pStages=stages.data();if(copy.basePipelineIndex>=0){copy.basePipelineHandle=values[copy.basePipelineIndex];copy.basePipelineIndex=-1;}
   VkResult alternate=create(device,cache,1,&copy,allocator,&pipeline.alternate);
   if(alternate==VK_SUCCESS){if(!roomViewReady.exchange(true)){if(FILE* marker=fopen("/run/stearlight/quest/passthrough.composition-ready","w"))fclose(marker);fprintf(stderr,"QUEST_ROOMVIEW_COMPOSITION_READY\n");}}
   else{pipeline.alternate=VK_NULL_HANDLE;fprintf(stderr,"Quest room-view pipeline unavailable: %d\n",alternate);}
  }
  if(pipeline.stars)fprintf(stderr,"QUEST_ROOMVIEW_STARS_PIPELINE_READY\n");
  // Frame draws its system layer directly into the final attachment. Its
  // background pipelines are identified above; clearing alpha and omitting
  // those draws preserves the panels without the desktop PS_DISTORT shader.
  if(pipeline.background&&getenv("QUEST_FRAME_FINAL_PASS")&&!roomViewReady.exchange(true)){
   if(FILE* marker=fopen("/run/stearlight/quest/passthrough.composition-ready","w"))fclose(marker);
   fprintf(stderr,"QUEST_ROOMVIEW_FRAME_COMPOSITION_READY\n");
  }
  if(pipeline.background||pipeline.stars||pipeline.alternate)roomPipelines[values[i]]=pipeline;
 }
 return result;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateComputePipelines(VkDevice device,VkPipelineCache cache,uint32_t count,const VkComputePipelineCreateInfo* info,const VkAllocationCallbacks* allocator,VkPipeline* values){VkResult result=D(vkCreateComputePipelines)(device,cache,count,info,allocator,values);if(result<0)fprintf(stderr,"Quest Vulkan failure vkCreateComputePipelines: %d\n",result);return result;}
VKAPI_ATTR VkResult VKAPI_CALL vkGetSemaphoreFdKHR(VkDevice device,const VkSemaphoreGetFdInfoKHR* info,int* fd){VkResult result=D(vkGetSemaphoreFdKHR)(device,info,fd);if(result<0)fprintf(stderr,"Quest Vulkan failure vkGetSemaphoreFdKHR: %d\n",result);return result;}
VKAPI_ATTR VkResult VKAPI_CALL vkImportSemaphoreFdKHR(VkDevice device,const VkImportSemaphoreFdInfoKHR* info){VkResult result=D(vkImportSemaphoreFdKHR)(device,info);if(result<0)fprintf(stderr,"Quest Vulkan failure vkImportSemaphoreFdKHR: %d\n",result);return result;}
VKAPI_ATTR VkResult VKAPI_CALL vkGetMemoryFdKHR(VkDevice device,const VkMemoryGetFdInfoKHR* info,int* fd){VkResult result=D(vkGetMemoryFdKHR)(device,info,fd);if(result<0)fprintf(stderr,"Quest Vulkan failure vkGetMemoryFdKHR: %d\n",result);return result;}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateSampler(VkDevice device,const VkSamplerCreateInfo* info,const VkAllocationCallbacks* allocator,VkSampler* value){VkResult result=D(vkCreateSampler)(device,info,allocator,value);if(result<0)fprintf(stderr,"Quest Vulkan failure vkCreateSampler: %d\n",result);return result;}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device,const char* name);
VKAPI_ATTR VkResult VKAPI_CALL vkCreateShaderModule(VkDevice device,const VkShaderModuleCreateInfo* info,const VkAllocationCallbacks* callbacks,VkShaderModule* output){
 auto create=D(vkCreateShaderModule);
 auto linear=isCompositor()?quest::pcvrColorShader(info->pCode,info->codeSize/4):std::vector<uint32_t>{};
 auto corrected=*info;
 if(!linear.empty()){corrected.pCode=linear.data();corrected.codeSize=linear.size()*4;}
 VkResult result=create(device,&corrected,callbacks,output);
 if(result==VK_SUCCESS&&!linear.empty())fprintf(stderr,"QUEST_PCVR_LINEAR_COLOR_READY\n");
 if(!isCompositor()||result!=VK_SUCCESS)return result;
 auto code=quest::roomViewShader(info->pCode,info->codeSize/4);RoomShader shader;
 shader.background=quest::roomViewBackgroundShader(info->pCode,info->codeSize/4);
 shader.stars=quest::roomViewStarShader(info->pCode,info->codeSize/4);
 if(!code.empty()){auto copy=*info;copy.pCode=code.data();copy.codeSize=code.size()*4;
  if(create(device,&copy,callbacks,&shader.alternate)!=VK_SUCCESS)shader.alternate=VK_NULL_HANDLE;
 }
 if(shader.alternate||shader.background||shader.stars){std::lock_guard<std::mutex> guard(roomLock);roomShaders[*output]=shader;}
 return result;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyShaderModule(VkDevice device,VkShaderModule shader,const VkAllocationCallbacks* callbacks){
 if(isCompositor()){std::lock_guard<std::mutex> guard(roomLock);auto found=roomShaders.find(shader);if(found!=roomShaders.end()){if(found->second.alternate)D(vkDestroyShaderModule)(device,found->second.alternate,callbacks);roomShaders.erase(found);}}
 D(vkDestroyShaderModule)(device,shader,callbacks);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyPipeline(VkDevice device,VkPipeline pipeline,const VkAllocationCallbacks* callbacks){
 if(isCompositor()){std::lock_guard<std::mutex> guard(roomLock);auto found=roomPipelines.find(pipeline);if(found!=roomPipelines.end()){if(found->second.alternate)D(vkDestroyPipeline)(device,found->second.alternate,callbacks);roomPipelines.erase(found);}}
 D(vkDestroyPipeline)(device,pipeline,callbacks);
}
VKAPI_ATTR void VKAPI_CALL vkCmdBindPipeline(VkCommandBuffer command,VkPipelineBindPoint point,VkPipeline pipeline){
 if(isCompositor()&&point==VK_PIPELINE_BIND_POINT_GRAPHICS){std::lock_guard<std::mutex> guard(roomLock);auto found=roomPipelines.find(pipeline);
  roomBackgroundCommands[command]=found==roomPipelines.end()?RoomCommand{}:
   RoomCommand{found->second.background,found->second.stars};
  if(roomViewMode&&found!=roomPipelines.end()&&found->second.alternate)pipeline=found->second.alternate;
 }
 C(vkCmdBindPipeline)(command,point,pipeline);
}
VKAPI_ATTR void VKAPI_CALL vkCmdBeginRenderPass(VkCommandBuffer command,const VkRenderPassBeginInfo* info,VkSubpassContents contents){
 // SteamVR splits each eye's scene into passes which reuse color and depth
 // attachments. Make those writes visible before Turnip begins the next pass;
 // stale attachment data otherwise leaves holes and trails in the right eye.
 if(isCompositor()){
  VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  barrier.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;
  barrier.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
  C(vkCmdPipelineBarrier)(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
   VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,1,&barrier,0,nullptr,0,nullptr);
 }
 auto copy=*info;std::vector<VkClearValue> clears;
 if(roomViewMode&&info->clearValueCount){clears.assign(info->pClearValues,info->pClearValues+info->clearValueCount);
  // The fourth word is alpha for color clears, padding for depth/stencil clears.
  for(auto& value:clears)value.color.float32[3]=0;copy.pClearValues=clears.data();
 }
 C(vkCmdBeginRenderPass)(command,&copy,contents);
}
bool hideRoomBackground(VkCommandBuffer command,bool starDraw=false){if(!roomViewMode)return false;std::lock_guard<std::mutex> guard(roomLock);auto found=roomBackgroundCommands.find(command);return found!=roomBackgroundCommands.end()&&(found->second.background||(starDraw&&found->second.stars));}
VKAPI_ATTR void VKAPI_CALL vkCmdDraw(VkCommandBuffer command,uint32_t vertices,uint32_t instances,uint32_t firstVertex,uint32_t firstInstance){if(!hideRoomBackground(command))C(vkCmdDraw)(command,vertices,instances,firstVertex,firstInstance);}
VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndexed(VkCommandBuffer command,uint32_t indices,uint32_t instances,uint32_t firstIndex,int32_t offset,uint32_t firstInstance){if(!hideRoomBackground(command,quest::roomViewStarDraw(indices,instances,firstIndex,offset,firstInstance)))C(vkCmdDrawIndexed)(command,indices,instances,firstIndex,offset,firstInstance);}
VKAPI_ATTR void VKAPI_CALL vkFreeCommandBuffers(VkDevice device,VkCommandPool pool,uint32_t count,const VkCommandBuffer* commands){
 if(isCompositor()){std::lock_guard<std::mutex> guard(roomLock);for(uint32_t i=0;i<count;i++)roomBackgroundCommands.erase(commands[i]);}
 D(vkFreeCommandBuffers)(device,pool,count,commands);
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance,const char* name){init();
#define MAP(fn) if(!strcmp(name,#fn))return reinterpret_cast<PFN_vkVoidFunction>(&fn)
 MAP(vkRegisterDisplayEventEXT);MAP(vkGetSwapchainCounterEXT);MAP(vkDisplayPowerControlEXT);MAP(vkRegisterDeviceEventEXT);
 MAP(vkCreateShaderModule);MAP(vkDestroyShaderModule);MAP(vkDestroyPipeline);MAP(vkCmdBindPipeline);MAP(vkCmdBeginRenderPass);MAP(vkCmdDraw);MAP(vkCmdDrawIndexed);MAP(vkFreeCommandBuffers);
 MAP(vkGetInstanceProcAddr);MAP(vkGetPhysicalDeviceFeatures2);MAP(vkGetPhysicalDeviceFeatures2KHR);MAP(vkGetDeviceProcAddr);MAP(vkEnumerateInstanceExtensionProperties);MAP(vkCreateInstance);MAP(vkEnumerateDeviceExtensionProperties);MAP(vkCreateDevice);MAP(vkCreateXcbSurfaceKHR);MAP(vkCreateXlibSurfaceKHR);MAP(vkDestroySurfaceKHR);MAP(vkGetPhysicalDeviceXcbPresentationSupportKHR);MAP(vkGetPhysicalDeviceXlibPresentationSupportKHR);MAP(vkGetPhysicalDeviceSurfaceSupportKHR);MAP(vkGetPhysicalDeviceSurfaceCapabilitiesKHR);MAP(vkGetPhysicalDeviceSurfaceFormatsKHR);MAP(vkGetPhysicalDeviceSurfacePresentModesKHR);MAP(vkCreateSwapchainKHR);MAP(vkDestroySwapchainKHR);MAP(vkGetSwapchainImagesKHR);MAP(vkAcquireNextImageKHR);MAP(vkAcquireNextImage2KHR);MAP(vkQueuePresentKHR);MAP(vkWaitForPresentKHR);MAP(vkCreateSemaphore);MAP(vkCreateFence);MAP(vkCreateImage);MAP(vkAllocateMemory);MAP(vkCreateGraphicsPipelines);MAP(vkCreateComputePipelines);MAP(vkGetSemaphoreFdKHR);MAP(vkImportSemaphoreFdKHR);MAP(vkGetMemoryFdKHR);MAP(vkCreateSampler);
 MAP(vkGetPhysicalDeviceDisplayPropertiesKHR);MAP(vkGetPhysicalDeviceDisplayPlanePropertiesKHR);MAP(vkGetDisplayPlaneSupportedDisplaysKHR);MAP(vkGetDisplayModePropertiesKHR);MAP(vkCreateDisplayModeKHR);MAP(vkGetDisplayPlaneCapabilitiesKHR);MAP(vkCreateDisplayPlaneSurfaceKHR);MAP(vkAcquireXlibDisplayEXT);MAP(vkGetRandROutputDisplayEXT);MAP(vkReleaseDisplayEXT);MAP(vkGetPhysicalDeviceSurfaceCapabilities2EXT);
 return realGet(instance,name);
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device,const char* name){
 MAP(vkRegisterDisplayEventEXT);MAP(vkGetSwapchainCounterEXT);MAP(vkDisplayPowerControlEXT);MAP(vkRegisterDeviceEventEXT);
 MAP(vkCreateShaderModule);MAP(vkDestroyShaderModule);MAP(vkDestroyPipeline);MAP(vkCmdBindPipeline);MAP(vkCmdBeginRenderPass);MAP(vkCmdDraw);MAP(vkCmdDrawIndexed);MAP(vkFreeCommandBuffers);
 MAP(vkGetDeviceProcAddr);MAP(vkCreateSwapchainKHR);MAP(vkDestroySwapchainKHR);MAP(vkGetSwapchainImagesKHR);MAP(vkAcquireNextImageKHR);MAP(vkAcquireNextImage2KHR);MAP(vkQueuePresentKHR);MAP(vkWaitForPresentKHR);MAP(vkCreateSemaphore);MAP(vkCreateFence);MAP(vkCreateImage);MAP(vkAllocateMemory);MAP(vkCreateGraphicsPipelines);MAP(vkCreateComputePipelines);MAP(vkGetSemaphoreFdKHR);MAP(vkImportSemaphoreFdKHR);MAP(vkGetMemoryFdKHR);MAP(vkCreateSampler);
 return deviceFunction<PFN_vkVoidFunction>(device,name);
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance instance,const char* name){return vkGetInstanceProcAddr(instance,name);}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetPhysicalDeviceProcAddr(VkInstance instance,const char* name){return vkGetInstanceProcAddr(instance,name);}
VKAPI_ATTR VkResult VKAPI_CALL vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t* version){*version=std::min(*version,5u);return VK_SUCCESS;}
}
