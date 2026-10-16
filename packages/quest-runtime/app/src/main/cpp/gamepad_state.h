#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "controller_input.h"

struct QuestGamepadState {
 uint32_t magic=0x53514750,version=1,sequence=0,buttons=0;
 uint64_t monotonic_ns=0;
 float axes[6]={0,0,0,0,-1,-1};
};
static_assert(sizeof(QuestGamepadState)==48,"Gamepad shared layout");
namespace quest {
inline QuestGamepadState gamepadState(const QuestTracking& tracking,bool leftMode,bool rightMode,uint64_t now){
 QuestGamepadState result;result.monotonic_ns=now;
 auto left=controllerInput(tracking,0),right=controllerInput(tracking,1);
 if(!leftMode)left={};
 if(!rightMode)right={};
 auto button=[&](int index,bool down){if(down)result.buttons|=1u<<index;};
 button(0,right.buttons&QUEST_A);button(1,right.buttons&QUEST_B);
 button(2,left.buttons&QUEST_X);button(3,left.buttons&QUEST_Y);
 bool lg=left.squeeze>.8f,rg=right.squeeze>.8f;
 bool ls=left.buttons&QUEST_THUMBSTICK,rs=right.buttons&QUEST_THUMBSTICK;
 button(4,lg&&ls);button(6,rg&&rs);button(7,ls&&!lg);button(8,rs&&!rg);
 button(9,lg);button(10,rg);
 result.axes[0]=left.x;result.axes[1]=-left.y;
 result.axes[2]=right.x;result.axes[3]=-right.y;
 result.axes[4]=left.trigger*2-1;result.axes[5]=right.trigger*2-1;
 return result;
}
inline void publishGamepad(QuestGamepadState* shared,const QuestGamepadState& value){
 __atomic_add_fetch(&shared->sequence,1,__ATOMIC_ACQ_REL);
 constexpr size_t offset=offsetof(QuestGamepadState,buttons);
 memcpy(reinterpret_cast<char*>(shared)+offset,reinterpret_cast<const char*>(&value)+offset,sizeof(value)-offset);
 __atomic_add_fetch(&shared->sequence,1,__ATOMIC_RELEASE);
}
inline bool readGamepad(const QuestGamepadState* shared,QuestGamepadState& value){
 for(int attempt=0;attempt<4;attempt++){
  uint32_t before=__atomic_load_n(&shared->sequence,__ATOMIC_ACQUIRE);if(before&1)continue;
  memcpy(&value,shared,sizeof value);
  if(before==__atomic_load_n(&shared->sequence,__ATOMIC_ACQUIRE))return value.magic==0x53514750&&value.version==1;
 }
 return false;
}
}
