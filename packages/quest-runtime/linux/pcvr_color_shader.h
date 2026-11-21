#pragma once
#include <cstdint>
#include <vector>

namespace quest {
// Unified dashboard facets are separate from this opaque streamed 3D scene.
// Hide only these exact scene shaders during room view, retaining the facets.
inline bool pcvrSceneShader(const uint32_t* words,size_t count) {
 if(!words||count<5||words[0]!=0x07230203)return false;
 if(count!=1655&&count!=1133&&count!=3006&&count!=7158&&count!=8803)return false;
 uint64_t hash=14695981039346656037ull;
 for(size_t i=0;i<count;i++)for(unsigned b=0;b<4;b++){hash^=(words[i]>>(b*8))&255;hash*=1099511628211ull;}
 return (count==1655&&hash==0xf333a137b4e79cedull)||
        (count==1133&&hash==0xe55773ce01ff41f1ull)||
        (count==3006&&hash==0xfb1ee3d7dcb6b170ull)||
        (count==7158&&hash==0x48703194de356152ull)||
        (count==8803&&hash==0x1b5d8151474965fdull);
}
// Frame's spatial-video final pass reconstructs encoded RGB from NV12.
// Quest presents through an sRGB attachment, which requires linear RGB.
// Identify the shipped SE2 shader exactly: other shaders and packed depth,
// motion, alpha and tracking data must retain Valve's native interpretation.
inline std::vector<uint32_t> pcvrColorShader(const uint32_t* words,size_t count) {
 if(!words||(count!=1655&&count!=1133)||words[0]!=0x07230203)return {};
 uint64_t hash=14695981039346656037ull;
 for(size_t i=0;i<count;i++)for(unsigned b=0;b<4;b++) {
  hash^=(words[i]>>(b*8))&255;hash*=1099511628211ull;
 }
 bool legacy=count==1655&&hash==0xf333a137b4e79cedull;
 bool current=count==1133&&hash==0xe55773ce01ff41f1ull;
 if(!legacy&&!current)return {};
 const uint32_t scalar=legacy?8:6,vector=legacy?30:7,boolean=legacy?96:128;
 const uint32_t rgb=legacy?910:577,gain=legacy?907:574,result=legacy?911:578,zero=legacy?1009:210;
 unsigned conversions=0;
 std::vector<uint32_t> output(words,words+5);output[3]=1028;
 for(size_t i=5;i<count;) {
  const uint32_t n=words[i]>>16,op=words[i]&65535;
  if(!n||n>count-i)return {};
  if(op==54) {
   const uint32_t declarations[]={
    (4u<<16)|23,1010,boolean,3,
    (4u<<16)|43,scalar,1011,0x3d25aee6,
    (4u<<16)|43,scalar,1012,0x414eb852,
    (4u<<16)|43,scalar,1013,0x3d6147ae,
    (4u<<16)|43,scalar,1014,0x3f870a3d,
    (4u<<16)|43,scalar,1015,0x4019999a,
    (6u<<16)|44,vector,1016,1011,1011,1011,
    (6u<<16)|44,vector,1017,1012,1012,1012,
    (6u<<16)|44,vector,1018,1013,1013,1013,
    (6u<<16)|44,vector,1019,1014,1014,1014,
    (6u<<16)|44,vector,1020,1015,1015,1015
   };
   output.insert(output.end(),std::begin(declarations),std::end(declarations));
  }
  // Convert the reconstructed scene RGB before the existing display gain.
  if(op==133&&n==5&&words[i+2]==result&&words[i+3]==rgb&&words[i+4]==gain) {
   const uint32_t conversion[]={
    (7u<<16)|12,vector,1021,1,40,rgb,zero,
    (5u<<16)|136,vector,1022,1021,1017,
    (5u<<16)|129,vector,1023,1021,1018,
    (5u<<16)|136,vector,1024,1023,1019,
    (7u<<16)|12,vector,1025,1,26,1024,1020,
    (5u<<16)|188,1010,1026,1021,1016,
    (6u<<16)|169,vector,1027,1026,1022,1025,
    (5u<<16)|133,vector,result,1027,gain
   };
   output.insert(output.end(),std::begin(conversion),std::end(conversion));++conversions;
  }else output.insert(output.end(),words+i,words+i+n);
  i+=n;
 }
 return conversions==1?output:std::vector<uint32_t>{};
}
}
