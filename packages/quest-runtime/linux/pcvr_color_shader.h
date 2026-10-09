#pragma once
#include <cstdint>
#include <vector>

namespace quest {
// Frame's spatial-video final pass reconstructs encoded RGB from NV12.
// Quest presents through an sRGB attachment, which requires linear RGB.
// Identify the shipped SE2 shader exactly: other shaders and packed depth,
// motion, alpha and tracking data must retain Valve's native interpretation.
inline std::vector<uint32_t> pcvrColorShader(const uint32_t* words,size_t count) {
 if(!words||count!=1655||words[0]!=0x07230203)return {};
 uint64_t hash=14695981039346656037ull;
 for(size_t i=0;i<count;i++)for(unsigned b=0;b<4;b++) {
  hash^=(words[i]>>(b*8))&255;hash*=1099511628211ull;
 }
 if(hash!=0xf333a137b4e79cedull)return {};
 std::vector<uint32_t> output(words,words+5);output[3]=1028;
 for(size_t i=5;i<count;) {
  const uint32_t n=words[i]>>16,op=words[i]&65535;
  if(!n||n>count-i)return {};
  if(op==54) {
   const uint32_t declarations[]={
    (4u<<16)|23,1010,96,3,
    (4u<<16)|43,8,1011,0x3d25aee6,
    (4u<<16)|43,8,1012,0x414eb852,
    (4u<<16)|43,8,1013,0x3d6147ae,
    (4u<<16)|43,8,1014,0x3f870a3d,
    (4u<<16)|43,8,1015,0x4019999a,
    (6u<<16)|44,30,1016,1011,1011,1011,
    (6u<<16)|44,30,1017,1012,1012,1012,
    (6u<<16)|44,30,1018,1013,1013,1013,
    (6u<<16)|44,30,1019,1014,1014,1014,
    (6u<<16)|44,30,1020,1015,1015,1015
   };
   output.insert(output.end(),std::begin(declarations),std::end(declarations));
  }
  // Convert the reconstructed scene RGB before the existing display gain.
  if(op==133&&n==5&&words[i+2]==911&&words[i+3]==910&&words[i+4]==907) {
   const uint32_t conversion[]={
    (7u<<16)|12,30,1021,1,40,910,1009,
    (5u<<16)|136,30,1022,1021,1017,
    (5u<<16)|129,30,1023,1021,1018,
    (5u<<16)|136,30,1024,1023,1019,
    (7u<<16)|12,30,1025,1,26,1024,1020,
    (5u<<16)|188,1010,1026,1021,1016,
    (6u<<16)|169,30,1027,1026,1022,1025,
    (5u<<16)|133,30,911,1027,907
   };
   output.insert(output.end(),std::begin(conversion),std::end(conversion));
  }else output.insert(output.end(),words+i,words+i+n);
  i+=n;
 }
 return output;
}
}
