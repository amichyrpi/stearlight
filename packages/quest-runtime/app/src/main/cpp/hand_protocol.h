#pragma once
#include "quest_bridge.h"

// Separate optional mapping: existing controller/tracking readers keep their
// wire layout, and an older APK continues to work with the updated driver.
enum { QUEST_HAND_MAGIC=0x53514831, QUEST_HAND_VERSION=1, QUEST_HAND_JOINTS=26 };
struct QuestHandSample { uint32_t active;QuestPose joints[QUEST_HAND_JOINTS]; };
struct QuestHands {
 uint32_t magic,version,sequence,reserved;
 uint64_t predicted_monotonic_ns;
 QuestHandSample hands[2];
};
inline void questPublishHands(QuestHands* shared,const QuestHands& sample){
 __atomic_add_fetch(&shared->sequence,1,__ATOMIC_ACQ_REL);
 constexpr size_t offset=offsetof(QuestHands,predicted_monotonic_ns);
 memcpy(reinterpret_cast<char*>(shared)+offset,reinterpret_cast<const char*>(&sample)+offset,sizeof(sample)-offset);
 __atomic_add_fetch(&shared->sequence,1,__ATOMIC_RELEASE);
}
inline bool questReadHands(const QuestHands* shared,QuestHands& sample){
 if(!shared)return false;
 for(int retry=0;retry<8;retry++){
  uint32_t sequence=__atomic_load_n(&shared->sequence,__ATOMIC_ACQUIRE);if(sequence&1)continue;
  memcpy(&sample,shared,sizeof(sample));
  if(sequence==__atomic_load_n(&shared->sequence,__ATOMIC_ACQUIRE))return sample.magic==QUEST_HAND_MAGIC&&sample.version==QUEST_HAND_VERSION;
 }
 return false;
}
