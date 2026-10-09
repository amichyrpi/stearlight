#pragma once
#include <vulkan/vulkan.h>
#include <cstdint>
namespace quest {
// Frame's VR player computes the chroma offset for Qualcomm compressed
// formats only. Our decoder negotiates linear NV12; its chroma follows Y.
inline bool linearVideoPlanes(const VkImageCreateInfo& image,
                              const VkImageDrmFormatModifierExplicitCreateInfoEXT& drm,
                              VkSubresourceLayout (&planes)[2]) {
 if(image.format!=VK_FORMAT_G8_B8R8_2PLANE_420_UNORM||
    image.tiling!=VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT||
    (drm.drmFormatModifier!=0&&drm.drmFormatModifier!=0x0500000000000001ull)||
    drm.drmFormatModifierPlaneCount!=2||!drm.pPlaneLayouts||
    drm.pPlaneLayouts[0].offset!=0||drm.pPlaneLayouts[1].offset!=0||
    !image.extent.width||!image.extent.height||image.extent.depth!=1||
    image.arrayLayers!=1||image.mipLevels!=1||(image.flags&VK_IMAGE_CREATE_DISJOINT_BIT)||
    drm.pPlaneLayouts[0].rowPitch<image.extent.width||
    drm.pPlaneLayouts[1].rowPitch!=drm.pPlaneLayouts[0].rowPitch)return false;
 planes[0]=drm.pPlaneLayouts[0];planes[1]=drm.pPlaneLayouts[1];
 planes[0].size=planes[0].rowPitch*uint64_t(image.extent.height);
 planes[1].offset=planes[0].size;
 planes[1].size=planes[1].rowPitch*((uint64_t(image.extent.height)+1)/2);
 return true;
}
struct LinearVideoImage {
 VkImageCreateInfo image;
 VkExternalMemoryImageCreateInfo external;
 VkImageDrmFormatModifierExplicitCreateInfoEXT drm;
 VkSubresourceLayout planes[2];
 bool prepare(const VkImageCreateInfo& source){
  auto first=static_cast<const VkBaseInStructure*>(source.pNext);
  if(!first)return false;
  bool externalFirst=first->sType==VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
  if(externalFirst){
   if(!first->pNext||first->pNext->sType!=VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT)return false;
   external=*reinterpret_cast<const VkExternalMemoryImageCreateInfo*>(first);
   drm=*reinterpret_cast<const VkImageDrmFormatModifierExplicitCreateInfoEXT*>(first->pNext);
  }else if(first->sType==VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT){
   drm=*reinterpret_cast<const VkImageDrmFormatModifierExplicitCreateInfoEXT*>(first);
  }else return false;
  if(!linearVideoPlanes(source,drm,planes))return false;
  image=source;drm.drmFormatModifier=0;drm.pPlaneLayouts=planes;
  if(externalFirst){external.pNext=&drm;image.pNext=&external;}
  else image.pNext=&drm;
  return true;
 }
};
}
