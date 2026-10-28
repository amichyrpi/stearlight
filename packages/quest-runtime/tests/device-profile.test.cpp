#include "../app/src/main/cpp/device_profile.h"
#include "../app/src/main/cpp/quest_bridge.h"
#include "../linux/controller_pose.h"
#include <cassert>
#include <cstring>
#include <cstdio>
int main(){
 using namespace quest;
 static_assert(sizeof(QuestTracking)==384,"Keep the Linux/Android tracking ABI");
 assert(headsetFromName("Oculus Quest2")==Headset::Quest2);
 assert(headsetFromName("Meta Quest 3")==Headset::Quest3);
 assert(headsetFromName("Quest 3S")==Headset::Quest3S);
 assert(headsetFromName("Meta Quest 3S")==Headset::Quest3S);
 assert(headsetFromName("Oculus Quest3S")==Headset::Quest3S);
 assert(headsetFromName("Quest Pro")==Headset::Unknown);
 assert(headsetFromName("Quest 30")==Headset::Unknown);
 for(auto device:{Headset::Quest2,Headset::Quest3,Headset::Quest3S})assert(headsetFromName(headsetId(device))==device);
 auto two=eyeSize(Headset::Quest2,1440,1540,4096,4096);
 auto three=eyeSize(Headset::Quest3,1680,1760,4096,4096);
 auto threeS=eyeSize(Headset::Quest3S,1680,1760,4096,4096);
 assert(two.width==1832&&two.height==1920);
 assert(three.width==2064&&three.height==2208);
 assert(threeS.width==two.width&&threeS.height==two.height);
 auto limited=eyeSize(Headset::Quest3,1680,1760,1800,2000);
 assert(limited.width==1800&&limited.height==2000);
 auto unknown=eyeSize(Headset::Unknown,1400,1500,4096,4096);
 assert(unknown.width==1400&&unknown.height==1500);
 assert(!touchPlus(Headset::Quest2)&&touchPlus(Headset::Quest3)&&touchPlus(Headset::Quest3S));
 assert(!strcmp(controllerModel(false,0),"oculus_quest2_controller_left"));
 assert(!strcmp(controllerModel(true,1),"oculus_quest_plus_controller_right"));
 QuestPose grip{{1,2,3},{0,0,0,1},15};
 auto twoPose=controllerModelPose(grip,0);grip.flags|=QUEST_POSE_TOUCH_PLUS;
 auto plusPose=controllerModelPose(grip,0);
 for(int i=0;i<3;i++)assert(twoPose.position[i]==plusPose.position[i]);
 for(int i=0;i<4;i++)assert(twoPose.orientation[i]==plusPose.orientation[i]);
 grip.flags|=QUEST_POSE_HAND;
 auto hand=controllerModelPose(grip,0);assert(hand.position[0]==grip.position[0]&&hand.orientation[3]==1);
 puts("Quest identities, native/clamped eye sizes, Touch Plus models and unchanged pose ABI passed");
}
