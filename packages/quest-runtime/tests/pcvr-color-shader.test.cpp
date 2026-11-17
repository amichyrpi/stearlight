#include "pcvr_color_shader.h"
#include <cassert>
#include <fstream>
#include <iostream>

// Supply the runtime's distort_ps_achromatic_se2.spv fixture. Verify the
// conversion does not alter any original depth, motion, alpha or gain code.
int main(int argc,char** argv) {
 assert(argc>=2);
 std::ifstream file(argv[1],std::ios::binary|std::ios::ate);
 assert(file&&(file.tellg()==1655*4||file.tellg()==1133*4));
 const bool legacy=file.tellg()==1655*4;
 std::vector<uint32_t> original(static_cast<size_t>(file.tellg())/4);file.seekg(0);
 file.read(reinterpret_cast<char*>(original.data()),original.size()*4);
 auto corrected=quest::pcvrColorShader(original.data(),original.size());
 assert(!corrected.empty());
 assert(quest::pcvrSceneShader(original.data(),original.size()));
 std::vector<uint32_t> restored(corrected.begin(),corrected.begin()+5);
 restored[3]=original[3];unsigned conversions=0;
 for(size_t i=5;i<corrected.size();) {
  auto size=corrected[i]>>16,op=corrected[i]&65535;
  assert(size&&size<=corrected.size()-i);
  bool declaration=(op==23||op==43||op==44)&&corrected[i+(op==23?1:2)]>=1010;
  bool conversion=(op==12||op==136||op==129||op==188||op==169)&&corrected[i+2]>=1021;
  if(op==133&&corrected[i+2]==(legacy?911u:578u)) {
   assert(corrected[i+3]==1027&&corrected[i+4]==(legacy?907u:574u));
   restored.insert(restored.end(),{(5u<<16)|133,legacy?30u:7u,legacy?911u:578u,legacy?910u:577u,legacy?907u:574u});conversions++;
  }else if(!declaration&&!conversion)restored.insert(restored.end(),corrected.begin()+i,corrected.begin()+i+size);
  i+=size;
 }
 assert(conversions==1&&restored==original);
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
