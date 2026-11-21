#pragma once
#include "quest_bridge.h"
#include <cmath>

// Optional bridge; do not change the existing tracking/streaming ABI.
struct QuestTrackers {
 uint32_t magic=0x53515431,version=1,sequence=0,enabled=0;
 uint64_t predicted_monotonic_ns=0;
 QuestPose poses[2]{};
};
static_assert(sizeof(QuestTrackers)==88&&offsetof(QuestTrackers,poses)==24,"Controller tracker ABI");
inline bool questTrackerPoseValid(const QuestPose& pose,uint64_t timestamp,uint64_t now){
 if(!timestamp||timestamp>now+100000000||now>timestamp+250000000||(pose.flags&15)!=15)return false;
 for(float value:pose.position)if(!std::isfinite(value))return false;
 float norm=0;for(float value:pose.orientation){if(!std::isfinite(value))return false;norm+=value*value;}
 return norm>.9f&&norm<1.1f;
}
inline void questPublishTrackers(QuestTrackers* shared,const QuestTrackers& sample){
 if(!shared)return;
 __atomic_add_fetch(&shared->sequence,1,__ATOMIC_ACQ_REL);
 constexpr size_t offset=offsetof(QuestTrackers,enabled);
 memcpy(reinterpret_cast<char*>(shared)+offset,reinterpret_cast<const char*>(&sample)+offset,sizeof(sample)-offset);
 __atomic_add_fetch(&shared->sequence,1,__ATOMIC_RELEASE);
}
inline bool questReadTrackers(const QuestTrackers* shared,QuestTrackers& sample){
 if(!shared)return false;
 for(int retry=0;retry<8;retry++){
  uint32_t before=__atomic_load_n(&shared->sequence,__ATOMIC_ACQUIRE);if(before&1)continue;
  memcpy(&sample,shared,sizeof(sample));
  if(before==__atomic_load_n(&shared->sequence,__ATOMIC_ACQUIRE))return sample.magic==0x53515431&&sample.version==1;
 }
 return false;
}
