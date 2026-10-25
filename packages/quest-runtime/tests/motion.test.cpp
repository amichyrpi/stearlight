#include "quest_motion.h"
#include <cassert>
#include <limits>
int main(){
 QuestMotion shared{};shared.magic=QUEST_MOTION_MAGIC;shared.version=QUEST_MOTION_VERSION;
 QuestMotion sample{};sample.predicted_time_ns=1234;sample.flags=3;
 sample.linear[0]=1;sample.linear[1]=-2;sample.linear[2]=3;
 sample.angular[0]=-.5f;sample.angular[1]=2;sample.angular[2]=-.25f;
 questPublishMotion(&shared,sample);assert(!(shared.sequence&1));
 double linear[3],angular[3];
 auto zero=[&]{for(int i=0;i<3;i++)assert(linear[i]==0&&angular[i]==0);};
 questMotionVelocity(&shared,1234,true,linear,angular);
 for(int i=0;i<3;i++){assert(linear[i]==sample.linear[i]);assert(angular[i]==sample.angular[i]);}
 questMotionVelocity(&shared,1235,true,linear,angular);zero();
 questMotionVelocity(&shared,1234,false,linear,angular);zero();
 questMotionVelocity(nullptr,1234,true,linear,angular);zero();
 sample.flags=0;questPublishMotion(&shared,sample);
 questMotionVelocity(&shared,1234,true,linear,angular);zero();
 sample.flags=3;sample.angular[1]=std::numeric_limits<float>::quiet_NaN();questPublishMotion(&shared,sample);
 questMotionVelocity(&shared,1234,true,linear,angular);
 for(int i=0;i<3;i++){assert(linear[i]==sample.linear[i]);assert(angular[i]==0);}
 shared.sequence|=1;questMotionVelocity(&shared,1234,true,linear,angular);zero();
 shared.sequence++;shared.version++;questMotionVelocity(&shared,1234,true,linear,angular);zero();
}
