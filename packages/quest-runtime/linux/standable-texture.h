#pragma once
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <openvr.h>
#include <dlfcn.h>
#include <cstring>
#include <string>
#include <vector>

// Upload X11 settings frames into one Vulkan image. Reuse its allocation
// instead of asking OpenVR to replace a raw-image texture on every repaint.
class StandableTexture {
    void* library=nullptr;
    PFN_vkGetInstanceProcAddr lookup=nullptr;
    VkInstance instance=VK_NULL_HANDLE;
    VkPhysicalDevice physical=VK_NULL_HANDLE;
    VkDevice device=VK_NULL_HANDLE;
    VkQueue queue=VK_NULL_HANDLE;
    uint32_t family=0,width=0,height=0;
    VkCommandPool pool=VK_NULL_HANDLE;
    VkCommandBuffer command=VK_NULL_HANDLE;
    VkBuffer staging=VK_NULL_HANDLE;
    VkImage image=VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory=VK_NULL_HANDLE,imageMemory=VK_NULL_HANDLE;
    void* mapped=nullptr;
    bool uploaded=false;
    bool attempted=false;
    VkPhysicalDeviceMemoryProperties memory{};
#define STANDABLE_VK_FUNCTIONS(F) \
    F(DestroyInstance) F(GetPhysicalDeviceMemoryProperties) F(GetPhysicalDeviceQueueFamilyProperties) \
    F(CreateDevice) F(DestroyDevice) F(GetDeviceQueue) F(CreateCommandPool) F(DestroyCommandPool) \
    F(AllocateCommandBuffers) F(ResetCommandBuffer) F(BeginCommandBuffer) F(EndCommandBuffer) \
    F(CreateImage) F(DestroyImage) F(GetImageMemoryRequirements) F(BindImageMemory) \
    F(CreateBuffer) F(DestroyBuffer) F(GetBufferMemoryRequirements) F(BindBufferMemory) \
    F(AllocateMemory) F(FreeMemory) F(MapMemory) F(UnmapMemory) \
    F(CmdPipelineBarrier) F(CmdCopyBufferToImage) F(QueueSubmit) F(QueueWaitIdle)
#define DECLARE(name) PFN_vk##name name=nullptr;
    STANDABLE_VK_FUNCTIONS(DECLARE)
#undef DECLARE
    std::vector<const char*> extensions(std::string& text) {
        std::vector<const char*> result;
        for(size_t pos=0;pos<text.size();) {
            while(pos<text.size()&&(text[pos]==' '||text[pos]=='\0'))++pos;
            if(pos==text.size())break;
            result.push_back(text.data()+pos);
            while(pos<text.size()&&text[pos]!=' '&&text[pos]!='\0')++pos;
            if(pos<text.size())text[pos++]='\0';
        }
        return result;
    }
    bool allocate(const VkMemoryRequirements& requirements,VkMemoryPropertyFlags flags,VkDeviceMemory& target) {
        for(uint32_t i=0;i<memory.memoryTypeCount;++i) {
            if(!(requirements.memoryTypeBits&(1u<<i))||(memory.memoryTypes[i].propertyFlags&flags)!=flags)continue;
            VkMemoryAllocateInfo info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            info.allocationSize=requirements.size;info.memoryTypeIndex=i;
            return AllocateMemory(device,&info,nullptr,&target)==VK_SUCCESS;
        }
        return false;
    }
    void releaseImage() {
        if(!device)return;
        QueueWaitIdle(queue);
        if(mapped)UnmapMemory(device,stagingMemory);
        if(staging)DestroyBuffer(device,staging,nullptr);
        if(image)DestroyImage(device,image,nullptr);
        if(stagingMemory)FreeMemory(device,stagingMemory,nullptr);
        if(imageMemory)FreeMemory(device,imageMemory,nullptr);
        mapped=nullptr;staging=VK_NULL_HANDLE;image=VK_NULL_HANDLE;
        stagingMemory=imageMemory=VK_NULL_HANDLE;width=height=0;uploaded=false;
    }
public:
    bool init() {
        if(attempted)return device&&command;
        attempted=true; // Failed initialization is not retried each frame.
        library=dlopen("libvulkan.so.1",RTLD_NOW|RTLD_GLOBAL);
        if(!library)return false;
        lookup=reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(library,"vkGetInstanceProcAddr"));
        if(!lookup)return false;
        auto* compositor=vr::VRCompositor();
        std::string names(compositor->GetVulkanInstanceExtensionsRequired(nullptr,0),'\0');
        compositor->GetVulkanInstanceExtensionsRequired(names.data(),names.size());
        auto required=extensions(names);
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.pApplicationName="Standable settings";app.apiVersion=VK_API_VERSION_1_1;
        VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};info.pApplicationInfo=&app;
        info.enabledExtensionCount=required.size();info.ppEnabledExtensionNames=required.data();
        auto create=reinterpret_cast<PFN_vkCreateInstance>(lookup(VK_NULL_HANDLE,"vkCreateInstance"));
        if(!create||create(&info,nullptr,&instance)!=VK_SUCCESS)return false;
#define LOAD(name) name=reinterpret_cast<PFN_vk##name>(lookup(instance,"vk" #name));if(!name)return false;
        STANDABLE_VK_FUNCTIONS(LOAD)
#undef LOAD
        uint64_t output=0;vr::VRSystem()->GetOutputDevice(&output,vr::TextureType_Vulkan,instance);
        physical=reinterpret_cast<VkPhysicalDevice>(output);
        // Some virtual HMDs do not publish an adapter identifier. Quest's
        // Vulkan ICD exposes one physical Adreno; never guess on multi-GPU.
        if(!physical){
            auto enumerate=reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(lookup(instance,"vkEnumeratePhysicalDevices"));
            uint32_t available=0;
            if(!enumerate||enumerate(instance,&available,nullptr)!=VK_SUCCESS||available!=1||
               enumerate(instance,&available,&physical)!=VK_SUCCESS)return false;
        }
        if(!physical)return false;
        uint32_t count=0;GetPhysicalDeviceQueueFamilyProperties(physical,&count,nullptr);
        std::vector<VkQueueFamilyProperties> families(count);GetPhysicalDeviceQueueFamilyProperties(physical,&count,families.data());
        for(family=0;family<count;++family)if(families[family].queueFlags&VK_QUEUE_GRAPHICS_BIT)break;
        if(family==count)return false;
        names.assign(compositor->GetVulkanDeviceExtensionsRequired(physical,nullptr,0),'\0');
        compositor->GetVulkanDeviceExtensionsRequired(physical,names.data(),names.size());required=extensions(names);
        float priority=1;VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        qi.queueFamilyIndex=family;qi.queueCount=1;qi.pQueuePriorities=&priority;
        VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};di.queueCreateInfoCount=1;di.pQueueCreateInfos=&qi;
        di.enabledExtensionCount=required.size();di.ppEnabledExtensionNames=required.data();
        if(CreateDevice(physical,&di,nullptr,&device)!=VK_SUCCESS)return false;
        GetDeviceQueue(device,family,0,&queue);GetPhysicalDeviceMemoryProperties(physical,&memory);
        VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pi.queueFamilyIndex=family;pi.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        if(CreateCommandPool(device,&pi,nullptr,&pool)!=VK_SUCCESS)return false;
        VkCommandBufferAllocateInfo ci{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ci.commandPool=pool;ci.commandBufferCount=1;ci.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        return AllocateCommandBuffers(device,&ci,&command)==VK_SUCCESS;
    }
    bool update(const void* pixels,uint32_t w,uint32_t h) {
        if(!init()||!command)return false;
        if(w!=width||h!=height) {
            releaseImage();
            VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};bi.size=VkDeviceSize(w)*h*4;bi.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            if(CreateBuffer(device,&bi,nullptr,&staging)!=VK_SUCCESS)return false;
            VkMemoryRequirements requirements{};GetBufferMemoryRequirements(device,staging,&requirements);
            if(!allocate(requirements,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,stagingMemory)||
               BindBufferMemory(device,staging,stagingMemory,0)!=VK_SUCCESS||MapMemory(device,stagingMemory,0,VK_WHOLE_SIZE,0,&mapped)!=VK_SUCCESS)return false;
            VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};ii.imageType=VK_IMAGE_TYPE_2D;ii.format=VK_FORMAT_R8G8B8A8_UNORM;
            ii.extent={w,h,1};ii.mipLevels=1;ii.arrayLayers=1;ii.samples=VK_SAMPLE_COUNT_1_BIT;
            ii.tiling=VK_IMAGE_TILING_OPTIMAL;ii.usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
            if(CreateImage(device,&ii,nullptr,&image)!=VK_SUCCESS)return false;
            GetImageMemoryRequirements(device,image,&requirements);
            if(!allocate(requirements,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,imageMemory)||BindImageMemory(device,image,imageMemory,0)!=VK_SUCCESS)return false;
            width=w;height=h;
        }
        QueueWaitIdle(queue);std::memcpy(mapped,pixels,size_t(w)*h*4);ResetCommandBuffer(command,0);
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if(BeginCommandBuffer(command,&begin)!=VK_SUCCESS)return false;
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};barrier.image=image;
        barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        barrier.oldLayout=uploaded?VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barrier.srcAccessMask=uploaded?VK_ACCESS_TRANSFER_READ_BIT:0;barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
        CmdPipelineBarrier(command,uploaded?VK_PIPELINE_STAGE_TRANSFER_BIT:VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
        VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={w,h,1};
        CmdCopyBufferToImage(command,staging,image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
        barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
        CmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
        if(EndCommandBuffer(command)!=VK_SUCCESS)return false;
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&command;
        if(QueueSubmit(queue,1,&submit,VK_NULL_HANDLE)!=VK_SUCCESS||QueueWaitIdle(queue)!=VK_SUCCESS)return false;
        uploaded=true;return true;
    }
    vr::VRVulkanTextureData_t frame() const {
        return {uint64_t(image),device,physical,instance,queue,family,width,height,VK_FORMAT_R8G8B8A8_UNORM,1};
    }
    ~StandableTexture() {
        // StandableWindow outlives VR_Shutdown, so OpenVR no longer references
        // this device when these resources are destroyed.
        if(device){releaseImage();if(pool)DestroyCommandPool(device,pool,nullptr);DestroyDevice(device,nullptr);}
        if(instance&&DestroyInstance)DestroyInstance(instance,nullptr);
        if(library)dlclose(library);
    }
#undef STANDABLE_VK_FUNCTIONS
};
