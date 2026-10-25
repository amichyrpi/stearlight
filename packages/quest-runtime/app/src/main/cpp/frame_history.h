#pragma once

namespace quest {
// Caller serializes these operations. Adoption retires the previous image
// only after the eye-rendering context has completed its reads of that image.
class FrameHistory {
 int held=-1,pending=-1,writing=-1;
public:
 int beginWrite(){
  if(writing>=0)return -1;
  for(int slot=0;slot<3;slot++)if(slot!=held&&slot!=pending){writing=slot;return slot;}
  return -1;
 }
 bool publish(int slot){
  if(slot<0||slot!=writing)return false;
  pending=slot;writing=-1;return true;
 }
 int adopt(bool readsComplete=true){if(!readsComplete||pending<0)return -1;held=pending;pending=-1;return held;}
};
}
