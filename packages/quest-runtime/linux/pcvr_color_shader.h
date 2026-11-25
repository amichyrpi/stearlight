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
 if(!words||(count!=1655&&count!=1133&&count!=7158&&count!=6535&&count!=4883&&count!=5015&&count!=11002)||words[0]!=0x07230203)return {};
 uint64_t hash=14695981039346656037ull;
 for(size_t i=0;i<count;i++)for(unsigned b=0;b<4;b++) {
  hash^=(words[i]>>(b*8))&255;hash*=1099511628211ull;
 }
 struct Target {
  uint32_t scalar,vector,boolean,zero,multiply,result,rgb,gain;
 };
 Target target;
 switch(hash) {
  case 0xf333a137b4e79cedull: target={8,30,96,1009,133,911,910,907};break;
  case 0xe55773ce01ff41f1ull: target={6,7,128,210,133,578,577,574};break;
  case 0x400da357bac85e08ull: target={6,7,132,222,142,460,459,531};break;
  case 0x05af858f571de71dull:
  case 0xd112af22fd2a896bull: target={6,7,134,441,142,1199,1198,1270};break;
  // SGSR blends its own NV12-to-RGB output over the corrected base scene.
  // Convert that RGB too, leaving its region mask and blend alpha intact.
  case 0x17e581b63603b494ull: target={8,35,138,3510,133,1334,1333,1331};break;
  case 0xd7a6c76a70f7a5a0ull: target={8,35,143,3663,133,1343,1342,1340};break;
  case 0xa35477bfe51ef9caull: target={8,35,77,6145,133,1520,1519,1517};break;
  default:return {};
 }
 const auto [scalar,vector,boolean,zero,multiply,result,rgb,gain]=target;
 unsigned conversions=0;
 const uint32_t id=words[3];
 // Fifth-degree fit of the nonlinear sRGB segment over [0.04045, 1.5].
 // Its round-trip error is below 0.13 of an 8-bit code value. Five fused
 // multiply-adds avoid the per-pixel power operation on the Quest GPU.
 const uint32_t constants[]={0x3d25aee6,0x3d9e8391,0x3a8e39ae,0x3ce7014a,
                             0x3f0df590,0x3f10612a,0xbe3bec3b,0x3d12edae};
 std::vector<uint32_t> output(words,words+5);output[3]=id+26;
 for(size_t i=5;i<count;) {
  const uint32_t n=words[i]>>16,op=words[i]&65535;
  if(!n||n>count-i)return {};
  if(op==54) {
   output.insert(output.end(),{(4u<<16)|23,id,boolean,3});
   for(uint32_t j=0;j<8;j++) {
    const uint32_t constant=id+1+j;
    output.insert(output.end(),{(4u<<16)|43,scalar,constant,constants[j]});
    output.insert(output.end(),{(6u<<16)|44,vector,id+9+j,constant,constant,constant});
   }
  }
  // Keep the original gain/fade, chroma-key alpha and reprojection code.
  if(op==multiply&&n==5&&words[i+2]==result&&words[i+3]==rgb&&words[i+4]==gain) {
   output.insert(output.end(),{(7u<<16)|12,vector,id+17,1,40,rgb,zero});
   output.insert(output.end(),{(5u<<16)|133,vector,id+18,id+17,id+10});
   uint32_t polynomial=id+16;
   for(uint32_t j=0;j<5;j++) {
    output.insert(output.end(),{(8u<<16)|12,vector,id+19+j,1,50,polynomial,id+17,id+15-j});
    polynomial=id+19+j;
   }
   output.insert(output.end(),{(5u<<16)|188,id,id+24,id+17,id+9});
   output.insert(output.end(),{(6u<<16)|169,vector,id+25,id+24,id+18,polynomial});
   output.insert(output.end(),{(5u<<16)|multiply,vector,result,id+25,gain});
   ++conversions;
  }else output.insert(output.end(),words+i,words+i+n);
  i+=n;
 }
 return conversions==1?output:std::vector<uint32_t>{};
}
}
