#include "video_image_layout.h"
#include <cassert>
#include <cstdio>
int main(){
 VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
 image.format=VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
 image.tiling=VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
 image.extent={1152,4608,1};image.arrayLayers=image.mipLevels=1;
 VkSubresourceLayout original[2]{};original[0].rowPitch=original[1].rowPitch=1152;
 VkImageDrmFormatModifierExplicitCreateInfoEXT drm{VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT};
 drm.drmFormatModifierPlaneCount=2;drm.pPlaneLayouts=original;
 VkSubresourceLayout result[2];assert(quest::linearVideoPlanes(image,drm,result));
 assert(result[0].offset==0&&result[0].size==5308416);
 assert(result[1].offset==5308416&&result[1].size==2654208);
 assert(result[1].offset+result[1].size==7962624);
 assert(original[1].offset==0); // Caller-owned structures are never modified.
 VkExternalMemoryImageCreateInfo external{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};
 VkImageFormatListCreateInfo tail{VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO};
 external.pNext=&drm;drm.pNext=&tail;image.pNext=&external;
 quest::LinearVideoImage fixed;assert(fixed.prepare(image));
 assert(fixed.image.pNext==&fixed.external&&fixed.external.pNext==&fixed.drm&&fixed.drm.pNext==&tail);
 assert(fixed.drm.drmFormatModifier==0&&fixed.planes[1].offset==5308416);
 // SteamVR imports with the reverse chain order and zero plane sizes.
 drm.drmFormatModifier=0x0500000000000001ull;drm.pNext=&external;external.pNext=&tail;image.pNext=&drm;
 assert(fixed.prepare(image));
 assert(fixed.image.pNext==&fixed.drm&&fixed.drm.pNext==&external&&external.pNext==&tail);
 assert(fixed.drm.drmFormatModifier==0&&fixed.planes[1].offset==5308416);
 assert(drm.drmFormatModifier==0x0500000000000001ull&&drm.pPlaneLayouts==original&&original[1].offset==0);
 image.pNext=nullptr;assert(!fixed.prepare(image));drm.pNext=nullptr;drm.drmFormatModifier=0;
 original[1].offset=result[1].offset;assert(!quest::linearVideoPlanes(image,drm,result));
 drm.drmFormatModifier=0x0500000000000001ull;assert(!quest::linearVideoPlanes(image,drm,result));
 original[1].offset=0;assert(quest::linearVideoPlanes(image,drm,result));
 drm.drmFormatModifier=42;assert(!quest::linearVideoPlanes(image,drm,result));
 drm.drmFormatModifier=0;image.flags=VK_IMAGE_CREATE_DISJOINT_BIT;assert(!quest::linearVideoPlanes(image,drm,result));image.flags=0;
 drm.drmFormatModifier=0;image.format=VK_FORMAT_R8G8B8A8_UNORM;assert(!quest::linearVideoPlanes(image,drm,result));
 image.format=VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;original[0].rowPitch=1024;assert(!quest::linearVideoPlanes(image,drm,result));
 puts("PASS: tall linear NV12 planes, valid layouts and compressed/non-video formats");
}
