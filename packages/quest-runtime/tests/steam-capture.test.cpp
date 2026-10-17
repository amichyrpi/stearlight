// On-device integration test: GPU capture must work without gpu.sock/OpenXR.
#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_XCB_KHR
#include <vulkan/vulkan.h>
#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>
#include <vector>
extern char* program_invocation_short_name;
#define CHECK(call) do{VkResult r=(call);if(r!=VK_SUCCESS){std::fprintf(stderr,"%s: %d\n",#call,r);return 1;}}while(0)
int main(){
 program_invocation_short_name=const_cast<char*>("steam");
 void* library=dlopen("/opt/quest/libquest_vulkan.so",RTLD_NOW|RTLD_LOCAL);
 if(!library){std::fprintf(stderr,"%s\n",dlerror());return 1;}
 auto get=reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(library,"vk_icdGetInstanceProcAddr"));
 VkInstance instance=VK_NULL_HANDLE;
 #define I(name) auto name=reinterpret_cast<PFN_##name>(get(instance,#name))
 I(vkCreateInstance);VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.apiVersion=VK_API_VERSION_1_1;
 const char* instanceExtensions[]={"VK_KHR_surface","VK_KHR_xcb_surface"};
 VkInstanceCreateInfo create{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};create.pApplicationInfo=&app;create.enabledExtensionCount=2;create.ppEnabledExtensionNames=instanceExtensions;
 CHECK(vkCreateInstance(&create,nullptr,&instance));
 I(vkEnumeratePhysicalDevices);I(vkGetPhysicalDeviceQueueFamilyProperties);I(vkGetPhysicalDeviceMemoryProperties);I(vkCreateDevice);I(vkCreateXcbSurfaceKHR);I(vkGetPhysicalDeviceSurfaceCapabilitiesKHR);I(vkGetDeviceProcAddr);I(vkDestroySurfaceKHR);I(vkDestroyInstance);
 uint32_t count=0;CHECK(vkEnumeratePhysicalDevices(instance,&count,nullptr));if(!count)return 2;
 std::vector<VkPhysicalDevice> physicals(count);CHECK(vkEnumeratePhysicalDevices(instance,&count,physicals.data()));auto physical=physicals[0];
 vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,nullptr);std::vector<VkQueueFamilyProperties> families(count);vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,families.data());
 uint32_t family=0;while(family<count&&!(families[family].queueFlags&VK_QUEUE_GRAPHICS_BIT))family++;if(family==count)return 3;
 float priority=1;VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};queueInfo.queueFamilyIndex=family;queueInfo.queueCount=1;queueInfo.pQueuePriorities=&priority;
 const char* extension="VK_KHR_swapchain";VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};deviceInfo.queueCreateInfoCount=1;deviceInfo.pQueueCreateInfos=&queueInfo;deviceInfo.enabledExtensionCount=1;deviceInfo.ppEnabledExtensionNames=&extension;
 VkDevice device;CHECK(vkCreateDevice(physical,&deviceInfo,nullptr,&device));
 #define D(name) auto name=reinterpret_cast<PFN_##name>(vkGetDeviceProcAddr(device,#name))
 D(vkGetDeviceQueue);D(vkCreateSwapchainKHR);D(vkGetSwapchainImagesKHR);D(vkAcquireNextImageKHR);D(vkQueuePresentKHR);D(vkWaitForPresentKHR);D(vkCreateSemaphore);D(vkCreateBuffer);D(vkGetBufferMemoryRequirements);D(vkAllocateMemory);D(vkBindBufferMemory);D(vkMapMemory);D(vkInvalidateMappedMemoryRanges);D(vkCreateCommandPool);D(vkAllocateCommandBuffers);D(vkBeginCommandBuffer);D(vkCmdPipelineBarrier);D(vkCmdClearColorImage);D(vkCmdCopyImageToBuffer);D(vkEndCommandBuffer);D(vkQueueSubmit);D(vkDeviceWaitIdle);D(vkDestroySwapchainKHR);D(vkDestroySemaphore);D(vkDestroyBuffer);D(vkFreeMemory);D(vkDestroyCommandPool);D(vkDestroyDevice);
 VkQueue queue;vkGetDeviceQueue(device,family,0,&queue);
 VkXcbSurfaceCreateInfoKHR surfaceInfo{VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR};VkSurfaceKHR surface;CHECK(vkCreateXcbSurfaceKHR(instance,&surfaceInfo,nullptr,&surface));
 VkSurfaceCapabilitiesKHR capabilities;CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical,surface,&capabilities));if(capabilities.currentExtent.width!=UINT32_MAX)return 4;
 VkSwapchainCreateInfoKHR swapInfo{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};swapInfo.surface=surface;swapInfo.minImageCount=3;swapInfo.imageFormat=VK_FORMAT_R8G8B8A8_UNORM;swapInfo.imageColorSpace=VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;swapInfo.imageExtent={96,64};swapInfo.imageArrayLayers=1;swapInfo.imageUsage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT;swapInfo.preTransform=VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;swapInfo.compositeAlpha=VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;swapInfo.presentMode=VK_PRESENT_MODE_FIFO_KHR;
 VkSwapchainKHR swap;CHECK(vkCreateSwapchainKHR(device,&swapInfo,nullptr,&swap));count=0;CHECK(vkGetSwapchainImagesKHR(device,swap,&count,nullptr));if(count!=3)return 5;
 std::vector<VkImage> images(count);CHECK(vkGetSwapchainImagesKHR(device,swap,&count,images.data()));
 VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};VkSemaphore acquired,rendered;CHECK(vkCreateSemaphore(device,&semaphoreInfo,nullptr,&acquired));CHECK(vkCreateSemaphore(device,&semaphoreInfo,nullptr,&rendered));
 uint32_t index;CHECK(vkAcquireNextImageKHR(device,swap,UINT64_MAX,acquired,VK_NULL_HANDLE,&index));if(index>=images.size())return 6;
 constexpr uint32_t bytes=96*64*4;
 VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};bufferInfo.size=bytes;bufferInfo.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;VkBuffer buffer;CHECK(vkCreateBuffer(device,&bufferInfo,nullptr,&buffer));
 VkMemoryRequirements requirements;vkGetBufferMemoryRequirements(device,buffer,&requirements);VkPhysicalDeviceMemoryProperties memory;vkGetPhysicalDeviceMemoryProperties(physical,&memory);
 uint32_t type=0;while(type<memory.memoryTypeCount&&(!(requirements.memoryTypeBits&(1u<<type))||!(memory.memoryTypes[type].propertyFlags&VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)))type++;if(type==memory.memoryTypeCount)return 7;
 VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};allocation.allocationSize=requirements.size;allocation.memoryTypeIndex=type;VkDeviceMemory hostMemory;CHECK(vkAllocateMemory(device,&allocation,nullptr,&hostMemory));CHECK(vkBindBufferMemory(device,buffer,hostMemory,0));
 VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};poolInfo.queueFamilyIndex=family;VkCommandPool pool;CHECK(vkCreateCommandPool(device,&poolInfo,nullptr,&pool));
 VkCommandBufferAllocateInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};commandInfo.commandPool=pool;commandInfo.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;commandInfo.commandBufferCount=1;VkCommandBuffer command;CHECK(vkAllocateCommandBuffers(device,&commandInfo,&command));
 VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};CHECK(vkBeginCommandBuffer(command,&begin));
 VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};barrier.image=images[index];barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
 vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
 VkClearColorValue color{{1,0,0,1}};vkCmdClearColorImage(command,images[index],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&color,1,&barrier.subresourceRange);
 barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
 vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
 VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={96,64,1};vkCmdCopyImageToBuffer(command,images[index],VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer,1,&copy);
 barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;barrier.newLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;barrier.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;barrier.dstAccessMask=0;
 vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,0,0,nullptr,0,nullptr,1,&barrier);CHECK(vkEndCommandBuffer(command));
 VkPipelineStageFlags stage=VK_PIPELINE_STAGE_TRANSFER_BIT;VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.waitSemaphoreCount=1;submit.pWaitSemaphores=&acquired;submit.pWaitDstStageMask=&stage;submit.commandBufferCount=1;submit.pCommandBuffers=&command;submit.signalSemaphoreCount=1;submit.pSignalSemaphores=&rendered;CHECK(vkQueueSubmit(queue,1,&submit,VK_NULL_HANDLE));
 uint64_t id=1;VkPresentIdKHR presentId{VK_STRUCTURE_TYPE_PRESENT_ID_KHR};presentId.swapchainCount=1;presentId.pPresentIds=&id;
 VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};present.pNext=&presentId;present.waitSemaphoreCount=1;present.pWaitSemaphores=&rendered;present.swapchainCount=1;present.pSwapchains=&swap;present.pImageIndices=&index;
 CHECK(vkQueuePresentKHR(queue,&present));CHECK(vkWaitForPresentKHR(device,swap,id,0));
 void* pixels;CHECK(vkMapMemory(device,hostMemory,0,VK_WHOLE_SIZE,0,&pixels));VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};range.memory=hostMemory;range.size=VK_WHOLE_SIZE;CHECK(vkInvalidateMappedMemoryRanges(device,1,&range));
 for(uint32_t i=0;i<bytes/4;i++)if(static_cast<uint32_t*>(pixels)[i]!=0xff0000ffu){std::fprintf(stderr,"Incorrect captured pixel %u\n",i);return 8;}
 CHECK(vkDeviceWaitIdle(device));vkDestroySwapchainKHR(device,swap,nullptr);vkDestroySemaphore(device,acquired,nullptr);vkDestroySemaphore(device,rendered,nullptr);vkDestroyBuffer(device,buffer,nullptr);vkFreeMemory(device,hostMemory,nullptr);vkDestroyCommandPool(device,pool,nullptr);vkDestroyDevice(device,nullptr);vkDestroySurfaceKHR(instance,surface,nullptr);vkDestroyInstance(instance,nullptr);
 std::puts("QUEST_CAPTURE_OFFSCREEN_VERIFIED 96x64: GPU pixels, semaphore waits and present completion");
}
