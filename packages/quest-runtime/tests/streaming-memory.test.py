"""Exercise the export allocator's ownership and fallback paths without a GPU."""
from pathlib import Path
import os
import subprocess
import tempfile

header = Path(__file__).resolve().parents[1] / 'linux/streaming_memory.h'
with tempfile.TemporaryDirectory(prefix='quest-stream-memory-') as directory:
    folder = Path(directory)
    (folder / 'mock.cpp').write_text(r'''
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <cassert>
#include <cstring>
#include <cstdlib>
static int instances,devices,buffers,memories,initializations;
static bool mode(const char* name){return !strcmp(getenv("TEST_MODE"),name);}
extern "C" int balanced(){return !instances&&!devices&&!buffers&&!memories&&initializations==1;}
static VkResult instance(const VkInstanceCreateInfo* p,const VkAllocationCallbacks*,VkInstance* out){assert(p->pApplicationInfo->apiVersion>=VK_API_VERSION_1_1);++instances;++initializations;*out=(VkInstance)1;return VK_SUCCESS;}
static void destroyInstance(VkInstance,const VkAllocationCallbacks*){--instances;}
static VkResult physicals(VkInstance,uint32_t* count,VkPhysicalDevice* out){if(out)*out=(VkPhysicalDevice)2;*count=1;return VK_SUCCESS;}
static void properties(VkPhysicalDevice,VkPhysicalDeviceProperties* out){out->deviceType=mode("cpu")?VK_PHYSICAL_DEVICE_TYPE_CPU:VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;strcpy(out->deviceName,"Mock GPU");}
static void external(VkPhysicalDevice,const VkPhysicalDeviceExternalBufferInfo* p,VkExternalBufferProperties* out){assert(p->handleType==VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT);out->externalMemoryProperties.externalMemoryFeatures=mode("unsupported")?0:VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT;if(mode("dedicated"))out->externalMemoryProperties.externalMemoryFeatures|=VK_EXTERNAL_MEMORY_FEATURE_DEDICATED_ONLY_BIT;}
static void queues(VkPhysicalDevice,uint32_t* count,VkQueueFamilyProperties* out){*count=1;if(out)out->queueFlags=VK_QUEUE_GRAPHICS_BIT;}
static VkResult device(VkPhysicalDevice,const VkDeviceCreateInfo* p,const VkAllocationCallbacks*,VkDevice* out){assert(p->enabledExtensionCount==2);if(mode("device-failure"))return VK_ERROR_EXTENSION_NOT_PRESENT;++devices;*out=(VkDevice)3;return VK_SUCCESS;}
static void destroyDevice(VkDevice,const VkAllocationCallbacks*){--devices;}
static void memoryProperties(VkPhysicalDevice,VkPhysicalDeviceMemoryProperties* out){out->memoryTypeCount=2;out->memoryTypes[0].propertyFlags=VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;out->memoryTypes[1].propertyFlags=mode("noncoherent")?VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT:VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;}
static VkResult buffer(VkDevice,const VkBufferCreateInfo* p,const VkAllocationCallbacks*,VkBuffer* out){assert(p->size>0);auto* x=(const VkExternalMemoryBufferCreateInfo*)p->pNext;assert(x->handleTypes==VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT);++buffers;*out=(VkBuffer)4;return VK_SUCCESS;}
static void destroyBuffer(VkDevice,VkBuffer,const VkAllocationCallbacks*){--buffers;}
static void requirements(VkDevice,VkBuffer,VkMemoryRequirements* out){out->size=8192;out->alignment=4096;out->memoryTypeBits=3;}
struct Allocation{int fd;};
static VkResult memory(VkDevice,const VkMemoryAllocateInfo* p,const VkAllocationCallbacks*,VkDeviceMemory* out){assert(p->allocationSize==8192&&p->memoryTypeIndex==1);assert(((const VkExportMemoryAllocateInfo*)p->pNext)->handleTypes==VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT);if(mode("allocation-failure"))return VK_ERROR_OUT_OF_DEVICE_MEMORY;auto* a=new Allocation{memfd_create("mock-gpu",MFD_CLOEXEC)};assert(a->fd>=0&&!ftruncate(a->fd,mode("short-fd")?16:p->allocationSize));++memories;*out=(VkDeviceMemory)a;return VK_SUCCESS;}
static void freeMemory(VkDevice,VkDeviceMemory memory,const VkAllocationCallbacks*){auto* a=(Allocation*)memory;close(a->fd);delete a;--memories;}
static VkResult descriptor(VkDevice,const VkMemoryGetFdInfoKHR* p,int* out){assert(p->handleType==VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT);if(mode("export-failure"))return VK_ERROR_TOO_MANY_OBJECTS;*out=fcntl(((Allocation*)p->memory)->fd,F_DUPFD_CLOEXEC,0);assert(*out>=0);return VK_SUCCESS;}
extern "C" PFN_vkVoidFunction vkGetInstanceProcAddr(VkInstance,const char*);
static PFN_vkVoidFunction deviceGet(VkDevice,const char* name){return vkGetInstanceProcAddr(VK_NULL_HANDLE,name);}
extern "C" PFN_vkVoidFunction vkGetInstanceProcAddr(VkInstance,const char* name){
#define MAP(api,fn) if(!strcmp(name,#api))return (PFN_vkVoidFunction)fn
 MAP(vkCreateInstance,instance);MAP(vkDestroyInstance,destroyInstance);MAP(vkEnumeratePhysicalDevices,physicals);
 MAP(vkGetPhysicalDeviceProperties,properties);MAP(vkGetPhysicalDeviceExternalBufferProperties,external);
 MAP(vkGetPhysicalDeviceQueueFamilyProperties,queues);MAP(vkCreateDevice,device);MAP(vkGetDeviceProcAddr,deviceGet);
 MAP(vkGetPhysicalDeviceMemoryProperties,memoryProperties);MAP(vkDestroyDevice,destroyDevice);
 MAP(vkCreateBuffer,buffer);MAP(vkDestroyBuffer,destroyBuffer);MAP(vkGetBufferMemoryRequirements,requirements);
 MAP(vkAllocateMemory,memory);MAP(vkFreeMemory,freeMemory);MAP(vkGetMemoryFdKHR,descriptor);
 return nullptr;
}
''')
    (folder / 'test.cpp').write_text('#include "' + str(header) + '"\n' + r'''
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <sys/mman.h>
#include <fcntl.h>
int main(){
 void* mock=dlopen("libvulkan.so.1",RTLD_NOW);assert(mock);auto balanced=(int(*)())dlsym(mock,"balanced");assert(balanced);
 bool good=!strcmp(getenv("TEST_MODE"),"success");
 {
  quest::StreamingMemory allocator;
  for(int i=0;i<3;i++){
   int fd=allocator.allocate(4096);
   if(!good){assert(fd<0);continue;}
   assert(fd>=0&&(fcntl(fd,F_GETFD)&FD_CLOEXEC));
   auto* pixels=(unsigned char*)mmap(nullptr,4096,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);assert(pixels!=MAP_FAILED);
   pixels[0]=42;pixels[4095]=99;assert(pixels[0]==42&&pixels[4095]==99);
   munmap(pixels,4096);close(fd);
  }
 }
 assert(balanced());dlclose(mock);
}
''')
    flags = ['g++', '-O2', '-std=c++17', '-Wall', '-Wextra', '-Werror',
             '-Wno-missing-field-initializers']
    subprocess.run([*flags, '-shared', '-fPIC', str(folder / 'mock.cpp'),
                    '-o', str(folder / 'libvulkan.so.1')], check=True)
    subprocess.run([*flags, str(folder / 'test.cpp'), '-ldl',
                    '-o', str(folder / 'test')], check=True)
    for mode in ('success', 'cpu', 'unsupported', 'dedicated', 'noncoherent',
                 'device-failure', 'allocation-failure', 'export-failure', 'short-fd'):
        subprocess.run([str(folder / 'test')], check=True, timeout=5,
                       env={**os.environ, 'LD_LIBRARY_PATH': str(folder), 'TEST_MODE': mode})
print('Streaming memory: exported FD lifetime, coherent memory selection, bounded initialization and failure cleanup pass')
