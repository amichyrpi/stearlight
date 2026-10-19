// Private spectator readback. The display's existing release fence also waits
// for this copy, so neither Android nor the next frame can reuse its source.
struct MirrorReadback {
 VkDevice device=VK_NULL_HANDLE;VkImage image=VK_NULL_HANDLE;VkDeviceMemory imageMemory=VK_NULL_HANDLE;
 VkBuffer buffer=VK_NULL_HANDLE;VkDeviceMemory bufferMemory=VK_NULL_HANDLE;void* pixels=nullptr;
 VkCommandPool pool=VK_NULL_HANDLE;VkCommandBuffer command=VK_NULL_HANDLE;bool attempted=false;
 ~MirrorReadback(){clear();}
 void clear(){
  if(!device)return;
  if(pool)D(vkDestroyCommandPool)(device,pool,nullptr);
  if(pixels)D(vkUnmapMemory)(device,bufferMemory);
  if(buffer)D(vkDestroyBuffer)(device,buffer,nullptr);
  if(bufferMemory)D(vkFreeMemory)(device,bufferMemory,nullptr);
  if(image)D(vkDestroyImage)(device,image,nullptr);
  if(imageMemory)D(vkFreeMemory)(device,imageMemory,nullptr);
  pool=VK_NULL_HANDLE;command=VK_NULL_HANDLE;pixels=nullptr;buffer=VK_NULL_HANDLE;
  bufferMemory=VK_NULL_HANDLE;image=VK_NULL_HANDLE;imageMemory=VK_NULL_HANDLE;
 }
 bool allocate(VkMemoryRequirements requirements,VkMemoryPropertyFlags flags,VkDeviceMemory* memory){
  VkPhysicalDeviceMemoryProperties properties{};I(vkGetPhysicalDeviceMemoryProperties)(activePhysical,&properties);
  for(uint32_t type=0;type<properties.memoryTypeCount;type++){
   if(!(requirements.memoryTypeBits&(1u<<type))||(properties.memoryTypes[type].propertyFlags&flags)!=flags)continue;
   VkMemoryAllocateInfo info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};info.allocationSize=requirements.size;info.memoryTypeIndex=type;
   if(D(vkAllocateMemory)(device,&info,nullptr,memory)==VK_SUCCESS)return true;
  }
  return false;
 }
 bool initialize(VkDevice value,uint32_t family,VkFormat sourceFormat){
  if(attempted)return pixels!=nullptr;attempted=true;device=value;
  if(!create(family,sourceFormat)){clear();fprintf(stderr,"Quest sharing: retaining CPU readback\n");return false;}
  fprintf(stderr,"QUEST_SHARE_GPU_READBACK_READY %ux%u\n",QUEST_SHARE_WIDTH,QUEST_SHARE_HEIGHT);return true;
 }
 bool create(uint32_t family,VkFormat sourceFormat){
  VkFormatProperties source{},target{};I(vkGetPhysicalDeviceFormatProperties)(activePhysical,sourceFormat,&source);
  I(vkGetPhysicalDeviceFormatProperties)(activePhysical,VK_FORMAT_B8G8R8A8_SRGB,&target);
  if(!(source.linearTilingFeatures&VK_FORMAT_FEATURE_BLIT_SRC_BIT)||!(target.optimalTilingFeatures&VK_FORMAT_FEATURE_BLIT_DST_BIT))return false;
  VkImageCreateInfo createImage{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};createImage.imageType=VK_IMAGE_TYPE_2D;
  createImage.format=VK_FORMAT_B8G8R8A8_SRGB;createImage.extent={QUEST_SHARE_WIDTH,QUEST_SHARE_HEIGHT,1};
  createImage.mipLevels=createImage.arrayLayers=1;createImage.samples=VK_SAMPLE_COUNT_1_BIT;
  createImage.tiling=VK_IMAGE_TILING_OPTIMAL;createImage.usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  if(D(vkCreateImage)(device,&createImage,nullptr,&image)!=VK_SUCCESS)return false;
  VkMemoryRequirements requirements{};D(vkGetImageMemoryRequirements)(device,image,&requirements);
  if(!allocate(requirements,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,&imageMemory)||D(vkBindImageMemory)(device,image,imageMemory,0)!=VK_SUCCESS)return false;
  VkBufferCreateInfo createBuffer{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};createBuffer.size=(VkDeviceSize)QUEST_SHARE_WIDTH*QUEST_SHARE_HEIGHT*4;
  createBuffer.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  if(D(vkCreateBuffer)(device,&createBuffer,nullptr,&buffer)!=VK_SUCCESS)return false;
  D(vkGetBufferMemoryRequirements)(device,buffer,&requirements);
  if(!allocate(requirements,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_CACHED_BIT,&bufferMemory)||
     D(vkBindBufferMemory)(device,buffer,bufferMemory,0)!=VK_SUCCESS||D(vkMapMemory)(device,bufferMemory,0,VK_WHOLE_SIZE,0,&pixels)!=VK_SUCCESS)return false;
  VkCommandPoolCreateInfo createPool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};createPool.queueFamilyIndex=family;
  createPool.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  if(D(vkCreateCommandPool)(device,&createPool,nullptr,&pool)!=VK_SUCCESS)return false;
  VkCommandBufferAllocateInfo allocateCommand{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};allocateCommand.commandPool=pool;
  allocateCommand.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;allocateCommand.commandBufferCount=1;
  return D(vkAllocateCommandBuffers)(device,&allocateCommand,&command)==VK_SUCCESS;
 }
 bool record(VkImage source,int32_t left,int32_t top,uint32_t width,uint32_t height){
  if(!pixels||D(vkResetCommandBuffer)(command,0)!=VK_SUCCESS)return false;
  VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  if(D(vkBeginCommandBuffer)(command,&begin)!=VK_SUCCESS)return false;
  VkImageMemoryBarrier barriers[2]{};
  for(auto& b:barriers){b.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};}
  barriers[0].image=source;barriers[0].oldLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;barriers[0].newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  barriers[0].srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;barriers[0].dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
  barriers[1].image=image;barriers[1].oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;barriers[1].newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barriers[1].dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
  D(vkCmdPipelineBarrier)(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,2,barriers);
  VkImageBlit blit{};blit.srcSubresource=blit.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
  blit.srcOffsets[0]={left,top,0};blit.srcOffsets[1]={left+(int32_t)width,top+(int32_t)height,1};
  blit.dstOffsets[1]={QUEST_SHARE_WIDTH,QUEST_SHARE_HEIGHT,1};
  D(vkCmdBlitImage)(command,source,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&blit,VK_FILTER_NEAREST);
  barriers[0].oldLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;barriers[0].newLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  barriers[0].srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;barriers[0].dstAccessMask=VK_ACCESS_MEMORY_READ_BIT;
  barriers[1].oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barriers[1].newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  barriers[1].srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barriers[1].dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
  D(vkCmdPipelineBarrier)(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,2,barriers);
  VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={QUEST_SHARE_WIDTH,QUEST_SHARE_HEIGHT,1};
  D(vkCmdCopyImageToBuffer)(command,image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer,1,&copy);
  VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
  host.srcQueueFamilyIndex=host.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;host.buffer=buffer;host.size=VK_WHOLE_SIZE;
  D(vkCmdPipelineBarrier)(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&host,0,nullptr);
  return D(vkEndCommandBuffer)(command)==VK_SUCCESS;
 }
 const uint32_t* read(){
  VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};range.memory=bufferMemory;range.size=VK_WHOLE_SIZE;
  return D(vkInvalidateMappedMemoryRanges)(device,1,&range)==VK_SUCCESS?static_cast<const uint32_t*>(pixels):nullptr;
 }
};
