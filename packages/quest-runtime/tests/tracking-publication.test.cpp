#include "../app/src/main/cpp/quest_bridge.h"
#include <cassert>
#include <cstdio>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

int main() {
 auto* shared=static_cast<QuestTracking*>(mmap(nullptr,sizeof(QuestTracking),PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0));
 assert(shared!=MAP_FAILED);
 shared->magic=QUEST_BRIDGE_MAGIC;shared->version=QUEST_BRIDGE_VERSION;
 pid_t reader=fork();assert(reader>=0);
 if(!reader){
  unsigned reads=0;
  for(;;){
   auto sequence=__atomic_load_n(&shared->sequence,__ATOMIC_ACQUIRE);
   if(sequence&1)continue;
   QuestTracking sample=*shared;
   if(sequence!=__atomic_load_n(&shared->sequence,__ATOMIC_ACQUIRE))continue;
   assert(sample.magic==QUEST_BRIDGE_MAGIC&&sample.version==QUEST_BRIDGE_VERSION);
   assert(sample.width==sample.height);
   assert(sample.buttons[0]==sample.width&&sample.buttons[1]==sample.width);
   assert(sample.predicted_time_ns==sample.width);
   ++reads;
   if(sample.width==100000){assert(reads>0);_exit(0);}
  }
 }
 for(unsigned i=1;i<=100000;++i){
  QuestTracking sample=*shared;
  sample.width=sample.height=sample.buttons[0]=sample.buttons[1]=i;
  sample.predicted_time_ns=i;
  // A slow OpenXR/controller query leaves the last complete sample readable.
  if(i%1000==0){assert(!(shared->sequence&1));usleep(100);}
  questPublishTracking(shared,sample);
 }
 int status;assert(waitpid(reader,&status,0)==reader);assert(WIFEXITED(status)&&WEXITSTATUS(status)==0);
 assert(shared->sequence==200000);
 munmap(shared,sizeof(QuestTracking));
 puts("Tracking publication: concurrent complete samples and sequence ownership passed");
}
