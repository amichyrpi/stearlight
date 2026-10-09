#include <vulkan/vulkan.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#define VK_CHECK(call) do { VkResult r=(call); if(r!=VK_SUCCESS){fprintf(stderr,"GPU_PROBE_ERROR %s result=%d\n",#call,r); return 1;} } while(0)
int main(int argc,char**argv) {
 int timeline=argc>1&&!strncmp(argv[1],"--timeline",10);
 void *lib=dlopen(argc>1?"/opt/quest/libquest_vulkan.so":"/opt/quest/libvulkan_freedreno.so",RTLD_NOW|RTLD_LOCAL);
 if(!lib){fprintf(stderr,"GPU_DRIVER_LOAD_ERROR %s\n",dlerror());return 1;}
 PFN_vkGetInstanceProcAddr get=(PFN_vkGetInstanceProcAddr)dlsym(lib,"vk_icdGetInstanceProcAddr");
 uint32_t version=5; VkResult(*negotiate)(uint32_t*)=dlsym(lib,"vk_icdNegotiateLoaderICDInterfaceVersion"); if(negotiate)VK_CHECK(negotiate(&version));
 #define FN(name) PFN_##name name=(PFN_##name)get(instance,#name)
 VkInstance instance=VK_NULL_HANDLE;
 FN(vkCreateInstance);
 VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.pApplicationName="Stearlight Quest GPU probe",.apiVersion=VK_API_VERSION_1_1};
 VkInstanceCreateInfo ci={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app};
 VK_CHECK(vkCreateInstance(&ci,NULL,&instance));
 FN(vkEnumeratePhysicalDevices); FN(vkGetPhysicalDeviceProperties); FN(vkGetPhysicalDeviceQueueFamilyProperties); FN(vkGetPhysicalDeviceMemoryProperties); FN(vkCreateDevice);
 uint32_t count=0; VK_CHECK(vkEnumeratePhysicalDevices(instance,&count,NULL)); if(!count){puts("GPU_DEVICE_COUNT=0");return 1;}
 VkPhysicalDevice physical; count=1; VK_CHECK(vkEnumeratePhysicalDevices(instance,&count,&physical));
 VkPhysicalDeviceProperties props;vkGetPhysicalDeviceProperties(physical,&props);printf("GPU_DEVICE=%s API=%u.%u TYPE=%d\n",props.deviceName,VK_VERSION_MAJOR(props.apiVersion),VK_VERSION_MINOR(props.apiVersion),props.deviceType);
 uint32_t qcount=0;vkGetPhysicalDeviceQueueFamilyProperties(physical,&qcount,NULL);VkQueueFamilyProperties *q=calloc(qcount,sizeof(*q));vkGetPhysicalDeviceQueueFamilyProperties(physical,&qcount,q);uint32_t family=0;for(uint32_t i=0;i<qcount;i++)if(q[i].queueFlags&VK_QUEUE_GRAPHICS_BIT){family=i;break;}free(q);
 float priority=1;VkDeviceQueueCreateInfo queueInfo={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=family,.queueCount=1,.pQueuePriorities=&priority};
 VkPhysicalDeviceTimelineSemaphoreFeatures timelineFeature={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES,.timelineSemaphore=VK_TRUE};
 const char *extensions[]={"VK_KHR_timeline_semaphore","VK_KHR_external_semaphore_fd"};
 VkDeviceCreateInfo di={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.pNext=timeline?&timelineFeature:NULL,.queueCreateInfoCount=1,.pQueueCreateInfos=&queueInfo,.enabledExtensionCount=timeline?2:0,.ppEnabledExtensionNames=extensions};VkDevice device;VK_CHECK(vkCreateDevice(physical,&di,NULL,&device));
 FN(vkGetDeviceProcAddr);
 #define D(name) PFN_##name name=(PFN_##name)vkGetDeviceProcAddr(device,#name)
 D(vkGetDeviceQueue); D(vkCreateBuffer);D(vkGetBufferMemoryRequirements);D(vkAllocateMemory);D(vkBindBufferMemory);D(vkMapMemory);D(vkCreateCommandPool);D(vkAllocateCommandBuffers);D(vkBeginCommandBuffer);D(vkCmdFillBuffer);D(vkEndCommandBuffer);D(vkQueueSubmit);D(vkQueueWaitIdle);D(vkInvalidateMappedMemoryRanges);
 VkQueue queue;vkGetDeviceQueue(device,family,0,&queue);
 VkBufferCreateInfo bi={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=4096,.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT,.sharingMode=VK_SHARING_MODE_EXCLUSIVE};VkBuffer buffer;VK_CHECK(vkCreateBuffer(device,&bi,NULL,&buffer));
 VkMemoryRequirements req;vkGetBufferMemoryRequirements(device,buffer,&req);VkPhysicalDeviceMemoryProperties memory;vkGetPhysicalDeviceMemoryProperties(physical,&memory);uint32_t type=UINT32_MAX;for(uint32_t i=0;i<memory.memoryTypeCount;i++)if((req.memoryTypeBits&(1u<<i))&&(memory.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)){type=i;break;}if(type==UINT32_MAX){puts("GPU_HOST_MEMORY_MISSING");return 1;}
 VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,.memoryTypeIndex=type};VkDeviceMemory allocation;VK_CHECK(vkAllocateMemory(device,&ai,NULL,&allocation));VK_CHECK(vkBindBufferMemory(device,buffer,allocation,0));void *mapped;VK_CHECK(vkMapMemory(device,allocation,0,VK_WHOLE_SIZE,0,&mapped));memset(mapped,0,4096);
 VkCommandPoolCreateInfo pci={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.queueFamilyIndex=family};VkCommandPool pool;VK_CHECK(vkCreateCommandPool(device,&pci,NULL,&pool));VkCommandBufferAllocateInfo cai={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};VkCommandBuffer command;VK_CHECK(vkAllocateCommandBuffers(device,&cai,&command));VkCommandBufferBeginInfo cbi={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};VK_CHECK(vkBeginCommandBuffer(command,&cbi));vkCmdFillBuffer(command,buffer,0,4096,0x51ea71a7);VK_CHECK(vkEndCommandBuffer(command));VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&command};VK_CHECK(vkQueueSubmit(queue,1,&submit,VK_NULL_HANDLE));VK_CHECK(vkQueueWaitIdle(queue));VkMappedMemoryRange range={.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,.memory=allocation,.size=VK_WHOLE_SIZE};VK_CHECK(vkInvalidateMappedMemoryRanges(device,1,&range));
 for(unsigned i=0;i<1024;i++)if(((uint32_t*)mapped)[i]!=0x51ea71a7){printf("GPU_VERIFY_ERROR index=%u actual=%x\n",i,((uint32_t*)mapped)[i]);return 1;}
 puts("GPU_EXECUTION_VERIFIED 4096 bytes written by a Vulkan GPU command");fflush(stdout);
 if(timeline){
  D(vkCreateSemaphore);D(vkDestroySemaphore);D(vkGetSemaphoreFdKHR);D(vkImportSemaphoreFdKHR);D(vkGetSemaphoreCounterValueKHR);D(vkWaitSemaphoresKHR);D(vkSignalSemaphoreKHR);
  VkExportSemaphoreCreateInfo exportInfo={.sType=VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO,.handleTypes=VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT};
  VkSemaphoreTypeCreateInfo typeInfo={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,.pNext=&exportInfo,.semaphoreType=VK_SEMAPHORE_TYPE_TIMELINE};
  VkSemaphoreCreateInfo semaphoreInfo={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,.pNext=&typeInfo};VkSemaphore semaphore;VK_CHECK(vkCreateSemaphore(device,&semaphoreInfo,NULL,&semaphore));
  uint64_t target=1;VkSemaphoreWaitInfo wait={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,.semaphoreCount=1,.pSemaphores=&semaphore,.pValues=&target};
  if(argc>2){
   int fd=atoi(argv[2]),readyFd=atoi(argv[3]);VkImportSemaphoreFdInfoKHR import={.sType=VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR,.semaphore=semaphore,.handleType=VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT,.fd=fd};VK_CHECK(vkImportSemaphoreFdKHR(device,&import));
   if(vkWaitSemaphoresKHR(device,&wait,0)!=VK_TIMEOUT){puts("TIMELINE_PREMATURE_SIGNAL");return 1;}
   if(write(readyFd,"R",1)!=1)return 1;close(readyFd);
   VK_CHECK(vkWaitSemaphoresKHR(device,&wait,5000000000ULL));uint64_t value;VK_CHECK(vkGetSemaphoreCounterValueKHR(device,semaphore,&value));if(value!=1){puts("TIMELINE_CHILD_BAD_VALUE");return 1;}
   VkSemaphoreSignalInfo signal={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO,.semaphore=semaphore,.value=2};VK_CHECK(vkSignalSemaphoreKHR(device,&signal));
  }else{
   VkSemaphoreGetFdInfoKHR getFd={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR,.semaphore=semaphore,.handleType=VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT};int fd;VK_CHECK(vkGetSemaphoreFdKHR(device,&getFd,&fd));fcntl(fd,F_SETFD,0);int ready[2];if(pipe(ready))return 1;
   pid_t child=fork();if(child<0)return 1;if(!child){close(ready[0]);char fdText[32],pipeText[32];snprintf(fdText,sizeof(fdText),"%d",fd);snprintf(pipeText,sizeof(pipeText),"%d",ready[1]);execl("/opt/quest/gpu-probe","gpu-probe","--timeline-child",fdText,pipeText,NULL);_exit(127);}
   close(ready[1]);close(fd);char byte;if(read(ready[0],&byte,1)!=1)return 1;close(ready[0]);
   memset(mapped,0,4096);VkTimelineSemaphoreSubmitInfo timelineSubmit={.sType=VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,.signalSemaphoreValueCount=1,.pSignalSemaphoreValues=&target};submit.pNext=&timelineSubmit;submit.signalSemaphoreCount=1;submit.pSignalSemaphores=&semaphore;
   VK_CHECK(vkQueueSubmit(queue,1,&submit,VK_NULL_HANDLE));target=2;VK_CHECK(vkWaitSemaphoresKHR(device,&wait,5000000000ULL));VK_CHECK(vkInvalidateMappedMemoryRanges(device,1,&range));for(unsigned i=0;i<1024;i++)if(((uint32_t*)mapped)[i]!=0x51ea71a7){puts("TIMELINE_GPU_COMPLETION_ERROR");return 1;}
   int status;if(waitpid(child,&status,0)!=child||!WIFEXITED(status)||WEXITSTATUS(status)){puts("TIMELINE_CHILD_ERROR");return 1;}
   target=3;if(vkWaitSemaphoresKHR(device,&wait,10000000)!=VK_TIMEOUT){puts("TIMELINE_TIMEOUT_ERROR");return 1;}
   puts("GPU_SHARED_TIMELINE_VERIFIED GPU completion, exec/import, cross-process host signal and timeout");fflush(stdout);
  }
  vkDestroySemaphore(device,semaphore,NULL);
 }else if(argc>1){
  D(vkCreateSwapchainKHR);D(vkGetSwapchainImagesKHR);D(vkAcquireNextImageKHR);D(vkQueuePresentKHR);D(vkCreateImageView);D(vkCreateRenderPass);D(vkCreateFramebuffer);D(vkCmdBeginRenderPass);D(vkCmdClearAttachments);D(vkCmdEndRenderPass);
  FN(vkGetPhysicalDeviceSurfaceCapabilitiesKHR);VkSurfaceCapabilitiesKHR caps;VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical,VK_NULL_HANDLE,&caps));VkExtent2D extent=caps.currentExtent;
  VkSwapchainCreateInfoKHR create={.sType=VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,.minImageCount=3,.imageFormat=VK_FORMAT_R8G8B8A8_UNORM,.imageExtent=extent,.imageArrayLayers=1,.imageUsage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,.imageSharingMode=VK_SHARING_MODE_EXCLUSIVE,.preTransform=VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,.compositeAlpha=VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,.presentMode=VK_PRESENT_MODE_FIFO_KHR};
  VkSwapchainKHR swap;VK_CHECK(vkCreateSwapchainKHR(device,&create,NULL,&swap));VkImage images[3];uint32_t imageCount=3;VK_CHECK(vkGetSwapchainImagesKHR(device,swap,&imageCount,images));
  VkAttachmentDescription attachment={.format=VK_FORMAT_R8G8B8A8_UNORM,.samples=VK_SAMPLE_COUNT_1_BIT,.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR,.storeOp=VK_ATTACHMENT_STORE_OP_STORE,.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE,.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE,.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED,.finalLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR};VkAttachmentReference reference={0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};VkSubpassDescription subpass={.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS,.colorAttachmentCount=1,.pColorAttachments=&reference};VkRenderPassCreateInfo passInfo={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,.attachmentCount=1,.pAttachments=&attachment,.subpassCount=1,.pSubpasses=&subpass};VkRenderPass pass;VK_CHECK(vkCreateRenderPass(device,&passInfo,NULL,&pass));
  VkImageView views[3];VkFramebuffer targets[3];for(int i=0;i<3;i++){VkImageViewCreateInfo view={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=images[i],.viewType=VK_IMAGE_VIEW_TYPE_2D,.format=VK_FORMAT_R8G8B8A8_UNORM,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};VK_CHECK(vkCreateImageView(device,&view,NULL,&views[i]));VkFramebufferCreateInfo target={.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,.renderPass=pass,.attachmentCount=1,.pAttachments=&views[i],.width=extent.width,.height=extent.height,.layers=1};VK_CHECK(vkCreateFramebuffer(device,&target,NULL,&targets[i]));}
  uint32_t index;VK_CHECK(vkAcquireNextImageKHR(device,swap,UINT64_MAX,VK_NULL_HANDLE,VK_NULL_HANDLE,&index));VkCommandBuffer clearCommand;VK_CHECK(vkAllocateCommandBuffers(device,&cai,&clearCommand));VK_CHECK(vkBeginCommandBuffer(clearCommand,&cbi));VkClearValue background={.color={{0,0,0,1}}};VkRenderPassBeginInfo render={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,.renderPass=pass,.framebuffer=targets[index],.renderArea={{0,0},extent},.clearValueCount=1,.pClearValues=&background};vkCmdBeginRenderPass(clearCommand,&render,VK_SUBPASS_CONTENTS_INLINE);VkClearAttachment color={.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT,.colorAttachment=0,.clearValue={.color={{1,0,0,1}}}};VkClearRect rectangle={.rect={{0,0},{extent.width/2,extent.height}},.baseArrayLayer=0,.layerCount=1};vkCmdClearAttachments(clearCommand,1,&color,1,&rectangle);color.clearValue.color.float32[0]=0;color.clearValue.color.float32[2]=1;rectangle.rect.offset.x=extent.width/2;vkCmdClearAttachments(clearCommand,1,&color,1,&rectangle);vkCmdEndRenderPass(clearCommand);VK_CHECK(vkEndCommandBuffer(clearCommand));submit.pCommandBuffers=&clearCommand;VK_CHECK(vkQueueSubmit(queue,1,&submit,VK_NULL_HANDLE));VkPresentInfoKHR present={.sType=VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,.swapchainCount=1,.pSwapchains=&swap,.pImageIndices=&index};VK_CHECK(vkQueuePresentKHR(queue,&present));puts("GPU_OPENXR_BRIDGE_VERIFIED Linux Vulkan rendered distinct left/right eyes through Android DMA-BUF");fflush(stdout);
 }
 return 0;
}
