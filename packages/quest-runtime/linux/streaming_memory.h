#pragma once
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <vector>

namespace quest {
// Allocate exportable, CPU-coherent memory through the active Vulkan ICD.
// The exported FD retains its payload after the producer objects are freed.
// No KMS device or separate allocator process is needed.
class StreamingMemory {
 void* library=nullptr;PFN_vkGetInstanceProcAddr get=nullptr;PFN_vkGetDeviceProcAddr deviceGet=nullptr;
 VkInstance instance=VK_NULL_HANDLE;VkDevice device=VK_NULL_HANDLE;
 VkPhysicalDeviceMemoryProperties properties{};bool attempted=false;
 template<class T>T i(const char* name){return reinterpret_cast<T>(get(instance,name));}
 template<class T>T d(const char* name){return reinterpret_cast<T>(deviceGet(device,name));}
 bool initialize(){
  if(attempted)return device!=VK_NULL_HANDLE;
  attempted=true;library=dlopen("libvulkan.so.1",RTLD_NOW|RTLD_LOCAL);if(!library)return false;
  get=reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(library,"vkGetInstanceProcAddr"));if(!get)return false;
  VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};application.apiVersion=VK_API_VERSION_1_1;
  VkInstanceCreateInfo create{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};create.pApplicationInfo=&application;
  auto createInstance=i<PFN_vkCreateInstance>("vkCreateInstance");
  if(!createInstance||createInstance(&create,nullptr,&instance)!=VK_SUCCESS)return false;
  auto enumerate=i<PFN_vkEnumeratePhysicalDevices>("vkEnumeratePhysicalDevices");uint32_t count=0;
  if(enumerate(instance,&count,nullptr)!=VK_SUCCESS||!count)return false;
  std::vector<VkPhysicalDevice> physicals(count);if(enumerate(instance,&count,physicals.data())!=VK_SUCCESS)return false;
  auto physical=physicals[0];VkPhysicalDeviceProperties gpu{};i<PFN_vkGetPhysicalDeviceProperties>("vkGetPhysicalDeviceProperties")(physical,&gpu);
  if(gpu.deviceType==VK_PHYSICAL_DEVICE_TYPE_CPU)return false;
  VkPhysicalDeviceExternalBufferInfo external{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_BUFFER_INFO};
  external.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT;external.handleType=VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
  VkExternalBufferProperties exported{VK_STRUCTURE_TYPE_EXTERNAL_BUFFER_PROPERTIES};
  i<PFN_vkGetPhysicalDeviceExternalBufferProperties>("vkGetPhysicalDeviceExternalBufferProperties")(physical,&external,&exported);
  auto features=exported.externalMemoryProperties.externalMemoryFeatures;
  if(!(features&VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT)||(features&VK_EXTERNAL_MEMORY_FEATURE_DEDICATED_ONLY_BIT))return false;
  auto queueProperties=i<PFN_vkGetPhysicalDeviceQueueFamilyProperties>("vkGetPhysicalDeviceQueueFamilyProperties");queueProperties(physical,&count,nullptr);
  std::vector<VkQueueFamilyProperties> queues(count);queueProperties(physical,&count,queues.data());uint32_t family=0;
  while(family<count&&!(queues[family].queueFlags&VK_QUEUE_GRAPHICS_BIT))++family;
  if(family==count)return false;
  float priority=0;VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};queue.queueFamilyIndex=family;queue.queueCount=1;queue.pQueuePriorities=&priority;
  const char* extensions[]={VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME};
  VkDeviceCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};info.queueCreateInfoCount=1;info.pQueueCreateInfos=&queue;info.enabledExtensionCount=2;info.ppEnabledExtensionNames=extensions;
  if(i<PFN_vkCreateDevice>("vkCreateDevice")(physical,&info,nullptr,&device)!=VK_SUCCESS)return false;
  deviceGet=i<PFN_vkGetDeviceProcAddr>("vkGetDeviceProcAddr");
  i<PFN_vkGetPhysicalDeviceMemoryProperties>("vkGetPhysicalDeviceMemoryProperties")(physical,&properties);
  fprintf(stderr,"Quest video: GPU-shareable buffer allocator on %s\n",gpu.deviceName);return true;
 }
public:
 StreamingMemory()=default;
 StreamingMemory(const StreamingMemory&)=delete;
 StreamingMemory& operator=(const StreamingMemory&)=delete;
 ~StreamingMemory(){
  if(device)d<PFN_vkDestroyDevice>("vkDestroyDevice")(device,nullptr);
  if(instance)i<PFN_vkDestroyInstance>("vkDestroyInstance")(instance,nullptr);
  if(library)dlclose(library);
 }
 int allocate(uint64_t bytes){
  // The DRM adapter serializes allocation and handle changes with its mutex.
  if(!initialize())return -1;
  VkExternalMemoryBufferCreateInfo external{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO};external.handleTypes=VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
  VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};info.pNext=&external;info.size=bytes;info.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  VkBuffer buffer=VK_NULL_HANDLE;if(d<PFN_vkCreateBuffer>("vkCreateBuffer")(device,&info,nullptr,&buffer)!=VK_SUCCESS)return -1;
  VkMemoryRequirements required{};d<PFN_vkGetBufferMemoryRequirements>("vkGetBufferMemoryRequirements")(device,buffer,&required);
  uint32_t type=0;constexpr VkMemoryPropertyFlags coherent=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  while(type<properties.memoryTypeCount&&(!(required.memoryTypeBits&(1u<<type))||(properties.memoryTypes[type].propertyFlags&coherent)!=coherent))++type;
  VkDeviceMemory memory=VK_NULL_HANDLE;int fd=-1;
  if(type<properties.memoryTypeCount){
   VkExportMemoryAllocateInfo exportInfo{VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO};exportInfo.handleTypes=external.handleTypes;
   VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};allocate.pNext=&exportInfo;allocate.allocationSize=required.size;allocate.memoryTypeIndex=type;
   if(d<PFN_vkAllocateMemory>("vkAllocateMemory")(device,&allocate,nullptr,&memory)==VK_SUCCESS){
    VkMemoryGetFdInfoKHR descriptor{VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR};descriptor.memory=memory;descriptor.handleType=VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
    if(d<PFN_vkGetMemoryFdKHR>("vkGetMemoryFdKHR")(device,&descriptor,&fd)!=VK_SUCCESS)fd=-1;
   }
  }
  d<PFN_vkDestroyBuffer>("vkDestroyBuffer")(device,buffer,nullptr);
  if(memory)d<PFN_vkFreeMemory>("vkFreeMemory")(device,memory,nullptr);
  struct stat data{};if(fd>=0&&(fstat(fd,&data)||data.st_size<0||uint64_t(data.st_size)<bytes)){close(fd);fd=-1;}
  return fd;
 }
};
}
