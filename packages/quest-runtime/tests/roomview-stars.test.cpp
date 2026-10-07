#include "../linux/roomview_shaders.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

int main(int argc,char** argv) {
 assert(argc==2);
 unsigned checked=0,matched=0;
 for(const auto& entry:std::filesystem::directory_iterator(argv[1])) {
  if(entry.path().extension()!=".spv")continue;
  std::ifstream file(entry.path(),std::ios::binary|std::ios::ate);
  auto bytes=file.tellg();assert(bytes>0&&bytes%4==0);
  std::vector<uint32_t> words(static_cast<size_t>(bytes)/4);
  file.seekg(0);file.read(reinterpret_cast<char*>(words.data()),bytes);assert(file);
  bool star=quest::roomViewStarShader(words.data(),words.size());
  assert(star==(entry.path().filename()=="distort_geom_fresnel_ps.spv"));
  if(star) {
   matched++;words.back()^=1;
   assert(!quest::roomViewStarShader(words.data(),words.size()));
   assert(!quest::roomViewStarShader(words.data(),words.size()-1));
  }
  checked++;
 }
 assert(matched==1&&checked>1);
 assert(!quest::roomViewStarShader(nullptr,0));
 assert(quest::roomViewStarDraw(54576,1,0,0,0));
 // Native controller components, lasers and panel quads seen in the trace.
 for(auto count:{15852u,1056u,846u,1248u,5952u,1008u,6u})
  assert(!quest::roomViewStarDraw(count,1,0,0,0));
 assert(!quest::roomViewStarDraw(54576,2,0,0,0));
 assert(!quest::roomViewStarDraw(54576,1,1,0,0));
 assert(!quest::roomViewStarDraw(54576,1,0,1,0));
 assert(!quest::roomViewStarDraw(54576,1,0,0,1));
 std::cout<<"Star selector passed against "<<checked<<" shipped shaders and controller draws\n";
}
