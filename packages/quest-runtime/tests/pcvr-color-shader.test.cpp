#include "pcvr_color_shader.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>

// Supply a known SE2 fragment fixture. Verify that the correction changes
// only the RGB input to the display gain or spatial fade.
int main(int argc,char** argv) {
 assert(argc>=2);
 std::ifstream file(argv[1],std::ios::binary|std::ios::ate);
 assert(file&&file.tellg()>=5*4&&file.tellg()%4==0);
 std::vector<uint32_t> original(static_cast<size_t>(file.tellg())/4);file.seekg(0);
 file.read(reinterpret_cast<char*>(original.data()),original.size()*4);
 auto corrected=quest::pcvrColorShader(original.data(),original.size());
 assert(!corrected.empty());
 const bool legacy=original.size()==1655;
 const bool keyed=original.size()==7158||original.size()==6535;
 const bool spatial=original.size()==1133&&original[3]==686;
 const bool sharpened=original.size()==4883;
 assert(quest::pcvrSceneShader(original.data(),original.size())==!(keyed||spatial||sharpened));
 const uint32_t first=original[3],multiply=keyed||spatial?142:133;
 const uint32_t vector=sharpened?35:legacy?30:7;
 const uint32_t result=sharpened?1334:keyed?1199:spatial?460:legacy?911:578;
 const uint32_t rgb=sharpened?1333:keyed?1198:spatial?459:legacy?910:577;
 const uint32_t gain=sharpened?1331:keyed?1270:spatial?531:legacy?907:574;
 std::vector<uint32_t> restored(corrected.begin(),corrected.begin()+5);
 restored[3]=original[3];unsigned conversions=0;
 float constants[8]{};
 for(size_t i=5;i<corrected.size();) {
  auto size=corrected[i]>>16,op=corrected[i]&65535;
  assert(size&&size<=corrected.size()-i);
  bool declaration=(op==23||op==43||op==44)&&corrected[i+(op==23?1:2)]>=first;
  if(op==43&&corrected[i+2]>first&&corrected[i+2]<=first+8)
   std::memcpy(&constants[corrected[i+2]-first-1],&corrected[i+3],sizeof(float));
  bool conversion=(op==12||op==133||op==188||op==169)&&corrected[i+2]>=first+17&&corrected[i+2]<=first+25;
  if(op==multiply&&corrected[i+2]==result) {
   assert(corrected[i+3]==first+25&&corrected[i+4]==gain);
   restored.insert(restored.end(),{(5u<<16)|multiply,vector,result,rgb,gain});conversions++;
  }else if(!declaration&&!conversion)restored.insert(restored.end(),corrected.begin()+i,corrected.begin()+i+size);
  i+=size;
 }
 assert(conversions==1&&restored==original);
 for(int i=0;i<=15000;i++){
  float x=i/10000.f,linear=x*constants[1];
  if(x>constants[0]){
   linear=constants[7];
   for(int j=6;j>=2;j--)linear=std::fma(linear,x,constants[j]);
  }
  float encoded=linear<=.0031308f?12.92f*linear:1.055f*std::pow(linear,1.f/2.4f)-.055f;
  assert(std::abs(encoded-x)*255<.13f);
 }
 original.back()^=1;
 assert(quest::pcvrColorShader(original.data(),original.size()).empty());
 assert(!quest::pcvrSceneShader(original.data(),original.size()));
 assert(quest::pcvrColorShader(nullptr,0).empty());
 assert(!quest::pcvrSceneShader(nullptr,0));
 for(int arg=2;arg<argc;arg++){
  std::ifstream scene(argv[arg],std::ios::binary|std::ios::ate);assert(scene&&scene.tellg()>0);
  std::vector<uint32_t> code(static_cast<size_t>(scene.tellg())/4);scene.seekg(0);scene.read(reinterpret_cast<char*>(code.data()),code.size()*4);
  assert(quest::pcvrSceneShader(code.data(),code.size()));
  code.back()^=1;assert(!quest::pcvrSceneShader(code.data(),code.size()));
 }
 std::cout<<"PASS: native spatial RGB conversion; all other instructions retained; unknown shaders rejected\n";
}
