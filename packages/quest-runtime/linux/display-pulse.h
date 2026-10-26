#pragma once
#include "quest_bridge.h"
#include <chrono>
#include <cerrno>
#include <sys/syscall.h>
#include <linux/futex.h>
#include <unistd.h>

// Drive Linux from OpenXR's application pulse, rather than making Linux start
// rendering at the predicted display deadline (after Android needs its image).
class QuestDisplayPulse {
 uint32_t lastSequence=0;
public:
 bool wait(const QuestTracking* tracking,uint64_t timeoutNs){
  if(!tracking)return false;
  auto deadline=std::chrono::steady_clock::now()+std::chrono::nanoseconds(timeoutNs);
  do{
   uint32_t sequence=__atomic_load_n(&tracking->sequence,__ATOMIC_ACQUIRE);
   if(!(sequence&1)&&sequence!=lastSequence){
    QuestTracking sample{};memcpy(&sample,tracking,sizeof(sample));
    if(sequence==__atomic_load_n(&tracking->sequence,__ATOMIC_ACQUIRE)&&sample.magic==QUEST_BRIDGE_MAGIC&&sample.version==QUEST_BRIDGE_VERSION&&sample.session_visible){lastSequence=sequence;return true;}
   }
   // Shared futex keys work across Android and the guest's mapping. The short
   // timeout also supports an older APK publisher without the wake operation.
   timespec timeout{0,1000000};
   if(syscall(SYS_futex,&tracking->sequence,FUTEX_WAIT,sequence,&timeout,nullptr,0)<0&&errno!=EAGAIN&&errno!=EINTR&&errno!=ETIMEDOUT)nanosleep(&timeout,nullptr);
  }while(std::chrono::steady_clock::now()<deadline);
  return false;
 }
};
