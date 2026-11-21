#pragma once
#include <cstdint>
#include <cmath>

namespace quest {
// Edges, not frames: the stick must return to center between directions.
class Konami {
 unsigned progress=0; int held=0; uint64_t last=0;
public:
 void reset(){progress=0;held=0;last=0;}
 bool update(float x,float y,bool a,bool b,bool focused,uint64_t now){
  if(!focused||!std::isfinite(x)||!std::isfinite(y)){reset();return false;}
  if(last&&(now<last||now-last>3000000000ull))progress=0;
  int key=0;
  if(a&&b){progress=0;held=7;return false;}
  if(a)key=6;else if(b)key=5;
  else if(std::fabs(x)>.75f&&std::fabs(y)<.35f)key=x<0?3:4;
  else if(std::fabs(y)>.75f&&std::fabs(x)<.35f)key=y>0?1:2;
  else if(std::fabs(x)>.35f||std::fabs(y)>.35f)return false;
  if(!key){held=0;return false;}
  if(held)return false;
  held=key;last=now;
  constexpr int sequence[]={1,1,2,2,3,4,3,4,5,6};
  if(key==sequence[progress])++progress;else progress=key==1?1:0;
  if(progress!=10)return false;
  progress=0;return true;
 }
};
}
