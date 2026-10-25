#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>

// Read only the dimensions needed to initialize Android's stateful decoder.
// The codec still validates and decodes the original, unchanged bitstream.
namespace quest {
class SequenceBits {
 const std::vector<uint8_t>& data;size_t offset=0;bool valid=true;
public:
 explicit SequenceBits(const std::vector<uint8_t>& bytes):data(bytes){}
 uint32_t read(unsigned count){
  if(count>32||offset+count>data.size()*8){valid=false;return 0;}
  uint32_t result=0;while(count--){result=(result<<1)|((data[offset/8]>>(7-offset%8))&1);++offset;}return result;
 }
 void skip(unsigned count){while(count>=32){read(32);count-=32;}read(count);}
 uint32_t ue(){unsigned zeros=0;while(valid&&!read(1))if(++zeros>30){valid=false;return 0;}return valid?((1u<<zeros)-1)+read(zeros):0;}
 bool good()const{return valid;}
};
inline bool hevcSpsDimensions(const uint8_t* data,size_t size,uint32_t& width,uint32_t& height){
 if(size<3||size>1024*1024||(data[0]&0x80)||((data[0]>>1)&63)!=33||!(data[1]&7))return false;
 std::vector<uint8_t> rbsp;rbsp.reserve(size-2);
 unsigned zeros=0;
 for(size_t i=2;i<size;i++){
  if(zeros>=2&&data[i]==3){if(i+1>=size||data[i+1]>3)return false;zeros=0;continue;}
  rbsp.push_back(data[i]);zeros=data[i]==0?zeros+1:0;
 }
 SequenceBits bits(rbsp);bits.skip(4);unsigned layers=bits.read(3);bits.skip(1);if(layers>6)return false;
 bits.skip(96);bool profiles[7]{},levels[7]{};
 for(unsigned i=0;i<layers;i++){profiles[i]=bits.read(1);levels[i]=bits.read(1);}
 if(layers)bits.skip((8-layers)*2);
 for(unsigned i=0;i<layers;i++){if(profiles[i])bits.skip(88);if(levels[i])bits.skip(8);}
 bits.ue();unsigned chroma=bits.ue();if(chroma>3)return false;if(chroma==3)bits.skip(1);
 uint32_t w=bits.ue(),h=bits.ue();
 if(!bits.good()||!w||!h||w>8192||h>8192||uint64_t(w)*h>4096*4096)return false;
 width=w;height=h;return true;
}
inline bool hevcDimensions(const uint8_t* data,size_t size,uint32_t& width,uint32_t& height){
 auto prefix=[&](size_t i)->unsigned{
  if(i+3>size||data[i]||data[i+1])return 0;
  if(data[i+2]==1)return 3;
  return i+4<=size&&!data[i+2]&&data[i+3]==1?4:0;
 };
 for(size_t i=0;i+3<=size;){
  unsigned length=prefix(i);if(!length){++i;continue;}
  size_t start=i+length,end=start;
  while(end+3<=size&&!prefix(end))++end;
  if(end+3>size)end=size;
  if(hevcSpsDimensions(data+start,end-start,width,height))return true;
  i=end;
 }
 return false;
}
}
