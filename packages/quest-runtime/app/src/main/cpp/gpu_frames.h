#pragma once
#include <cstdint>

namespace quest {
// Serialized by the bridge lock. The held image is released only after both
// eye reads finish. Unread pending images may be superseded immediately.
class FrameLeases {
 int held=-1,pending=-1;unsigned available=7,releases=0,accepted=0;
 uint64_t frames[3]{};
 void release(int slot){available|=1u<<slot;releases|=1u<<slot;}
public:
 unsigned reconnect(){pending=-1;releases=accepted=0;available=7u&~(held<0?0u:1u<<held);return available;}
 bool publish(unsigned slot,uint64_t frame){
  if(slot>=3||!frame||!(available&(1u<<slot)))return false;
  if(pending>=0)release(pending);
  available&=~(1u<<slot);frames[slot]=frame;pending=slot;accepted|=1u<<slot;return true;
 }
 int adopt(){if(pending<0)return -1;if(held>=0)release(held);held=pending;pending=-1;return held;}
 unsigned releaseMask()const{return releases;}
 unsigned acceptedMask()const{return accepted;}
 uint64_t frame(unsigned slot)const{return frames[slot];}
 void sent(unsigned slot){releases&=~(1u<<slot);}
 void sentAccepted(unsigned slot){accepted&=~(1u<<slot);}
};

// Producer-side ownership, including frame tags to reject stale/duplicate
// releases. Acquired or consumer-owned images cannot be acquired again.
class FramePool {
 unsigned available=0,inherited=0,next=0;uint64_t frames[3]{};
public:
 bool initialize(unsigned mask){if(!mask||(mask&~7u))return false;available=mask;inherited=7u&~mask;return true;}
 int acquire(){for(unsigned i=0;i<3;i++){unsigned slot=(next+i)%3;if(available&(1u<<slot)){available&=~(1u<<slot);frames[slot]=0;next=(slot+1)%3;return slot;}}return -1;}
 void submitted(unsigned slot,uint64_t frame){frames[slot]=frame;}
 bool matches(unsigned slot,uint64_t frame)const{return slot<3&&frame&&frames[slot]==frame;}
 bool release(unsigned slot,uint64_t frame){
  if(slot>=3||!frame||(available&(1u<<slot))||(!(inherited&(1u<<slot))&&frames[slot]!=frame))return false;
  inherited&=~(1u<<slot);available|=1u<<slot;return true;
 }
};
}
