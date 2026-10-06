#pragma once
#include <cstdint>
#include <cstring>
#include <vector>

namespace quest {
// The desktop compositor combines the scene and system layer, then writes
// alpha=1. Quest room view needs the premultiplied system layer by itself.
// Recognize that shader's descriptor/data flow; leave other variants intact.
inline std::vector<uint32_t> roomViewShader(const uint32_t* words, size_t count) {
 if(count<5||words[0]!=0x07230203)return {};
 bool distort=false;uint32_t imageVariable=0,imageValue=0,sampledImage=0,
  overlay=0,overlayType=0,one=0,vec4=0;size_t composite=0;
 for(size_t i=5;i<count;) {
  uint32_t size=words[i]>>16,op=words[i]&0xffff;
  if(!size||size>count-i)return {};
  if(op==15&&size>=4&&words[i+1]==4) {
   const char* name=reinterpret_cast<const char*>(words+i+3);
   size_t available=(size-3)*4;
   if(available>=11&&!std::memcmp(name,"PS_DISTORT\0",11))distort=true;
  }
  if(op==71&&size==4&&words[i+2]==33&&words[i+3]==13)imageVariable=words[i+1];
  if(op==23&&size==4&&words[i+3]==4)vec4=words[i+1];
  if(op==43&&size==4&&words[i+3]==0x3f800000)one=words[i+2];
  if(op==61&&size==4&&imageVariable&&words[i+3]==imageVariable)imageValue=words[i+2];
  if(op==86&&size==5&&imageValue&&words[i+3]==imageValue)sampledImage=words[i+2];
  if(op==87&&size>=5&&sampledImage&&words[i+3]==sampledImage) {
   if(overlay)return {};overlay=words[i+2];overlayType=words[i+1];
  }
  if(op==80&&size==7&&one&&words[i+1]==vec4&&words[i+6]==one) {
   if(composite)return {};composite=i;
  }
  i+=size;
 }
 if(!distort||!overlay||!composite||overlayType!=vec4)return {};
 std::vector<uint32_t> output(words,words+composite);
 // OpCopyObject keeps the original result ID and subsequent color correction.
 output.insert(output.end(),{(4u<<16)|83u,overlayType,words[composite+2],overlay});
 output.insert(output.end(),words+composite+7,words+count);
 return output;
}

inline bool roomViewBackgroundShader(const uint32_t* words,size_t count) {
 if(count<5||words[0]!=0x07230203)return false;
 for(size_t i=5;i<count;) {
  uint32_t size=words[i]>>16,op=words[i]&0xffff;
  if(!size||size>count-i)return false;
  if(op==15&&size>=4&&words[i+1]==4) {
   const char* name=reinterpret_cast<const char*>(words+i+3);
   size_t available=(size-3)*4;
   if((available>=10&&!std::memcmp(name,"PS_AURORA\0",10))||
      (available>=12&&!std::memcmp(name,"PS_PANORAMA\0",12)))return true;
  }
  i+=size;
 }
 return false;
}
}
