#pragma once
#include "gamepad_state.h"
#include <cmath>

// Separate writer and mapping from the Touch pair; Android owns physical HID.
struct QuestExternalGamepad {
 uint32_t magic=0x53514550,version=1,sequence=0,connected=0;
 uint32_t vendor=0,product=0;
 char name[80]{};
 QuestGamepadState input{};
};
static_assert(sizeof(QuestExternalGamepad)==152,"External gamepad shared layout");
namespace quest {
inline void publishExternalGamepad(QuestExternalGamepad* shared,const QuestExternalGamepad& value){
 __atomic_add_fetch(&shared->sequence,1,__ATOMIC_ACQ_REL);
 constexpr size_t offset=offsetof(QuestExternalGamepad,connected);
 memcpy(reinterpret_cast<char*>(shared)+offset,reinterpret_cast<const char*>(&value)+offset,sizeof(value)-offset);
 __atomic_add_fetch(&shared->sequence,1,__ATOMIC_RELEASE);
}
inline bool readExternalGamepad(const QuestExternalGamepad* shared,QuestExternalGamepad& value,uint64_t now){
 for(int attempt=0;attempt<4;attempt++){
  auto before=__atomic_load_n(&shared->sequence,__ATOMIC_ACQUIRE);if(before&1)continue;
  memcpy(&value,shared,sizeof value);
  if(before!=__atomic_load_n(&shared->sequence,__ATOMIC_ACQUIRE))continue;
  return value.magic==0x53514550&&value.version==1&&value.connected&&
   now>=value.input.monotonic_ns&&now-value.input.monotonic_ns<2000000000;
 }
 return false;
}
inline float gamepadAxis(float value,float flat=0){
 if(!std::isfinite(value)||std::fabs(value)<=std::max(flat,.08f))return 0;
 return std::clamp(value,-1.f,1.f);
}
}
