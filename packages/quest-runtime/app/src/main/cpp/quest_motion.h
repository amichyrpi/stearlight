#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cmath>

// Optional companion to QuestTracking; keep the existing tracking ABI intact.
// OpenXR and OpenVR both express these velocities in the stage/driver space.
constexpr uint32_t QUEST_MOTION_MAGIC=0x53514d31, QUEST_MOTION_VERSION=1;
enum { QUEST_LINEAR_VELOCITY_VALID=1, QUEST_ANGULAR_VELOCITY_VALID=2 };
struct QuestMotion {
 uint32_t magic,version,sequence,reserved;
 uint64_t predicted_time_ns;
 uint32_t flags,padding;
 float linear[3],angular[3];
};
static_assert(sizeof(QuestMotion)==56,"Quest motion wire layout");
inline void questPublishMotion(QuestMotion* shared,const QuestMotion& sample){
 if(!shared)return;
 __atomic_add_fetch(&shared->sequence,1,__ATOMIC_ACQ_REL);
 constexpr size_t offset=offsetof(QuestMotion,predicted_time_ns);
 std::memcpy(reinterpret_cast<char*>(shared)+offset,reinterpret_cast<const char*>(&sample)+offset,sizeof(sample)-offset);
 __atomic_add_fetch(&shared->sequence,1,__ATOMIC_RELEASE);
}
inline bool questReadMotion(const QuestMotion* shared,QuestMotion& sample){
 if(!shared)return false;
 for(int attempt=0;attempt<8;attempt++){
  uint32_t before=__atomic_load_n(&shared->sequence,__ATOMIC_ACQUIRE);
  if(before&1)continue;
  std::memcpy(&sample,shared,sizeof(sample));
  if(before==__atomic_load_n(&shared->sequence,__ATOMIC_ACQUIRE))
   return sample.magic==QUEST_MOTION_MAGIC&&sample.version==QUEST_MOTION_VERSION;
 }
 return false;
}
inline void questMotionVelocity(const QuestMotion* shared,uint64_t predictedTime,bool tracked,double* linear,double* angular){
 for(int i=0;i<3;i++)linear[i]=angular[i]=0;
 QuestMotion sample{};
 if(!tracked||!questReadMotion(shared,sample)||sample.predicted_time_ns!=predictedTime)return;
 auto copy=[](const float* source,double* target){
  for(int i=0;i<3;i++)if(!std::isfinite(source[i]))return;
  for(int i=0;i<3;i++)target[i]=source[i];
 };
 if(sample.flags&QUEST_LINEAR_VELOCITY_VALID)copy(sample.linear,linear);
 if(sample.flags&QUEST_ANGULAR_VELOCITY_VALID)copy(sample.angular,angular);
}
