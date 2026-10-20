#include "../linux/hand_skeleton.h"
#include <cassert>
#include <cstdio>
#include <limits>

// Reconstruct bone positions independently with a rotation matrix, then
// compare them to the measured OpenXR world positions for both hand bases.
static void rotate(const vr::HmdQuaternionf_t& q,const float* p,float* out){
 float matrix[3][3]={{1-2*(q.y*q.y+q.z*q.z),2*(q.x*q.y-q.z*q.w),2*(q.x*q.z+q.y*q.w)},
                   {2*(q.x*q.y+q.z*q.w),1-2*(q.x*q.x+q.z*q.z),2*(q.y*q.z-q.x*q.w)},
                   {2*(q.x*q.z-q.y*q.w),2*(q.y*q.z+q.x*q.w),1-2*(q.x*q.x+q.y*q.y)}};
 for(int i=0;i<3;i++){out[i]=0;for(int j=0;j<3;j++)out[i]+=matrix[i][j]*p[j];}
}
int main(){
 QuestHands shared{};shared.magic=QUEST_HAND_MAGIC;shared.version=QUEST_HAND_VERSION;
 QuestHands input{};input.predicted_monotonic_ns=123;input.hands[0].active=1;
 questPublishHands(&shared,input);QuestHands copy{};assert(questReadHands(&shared,copy));
 assert(copy.predicted_monotonic_ns==123&&copy.hands[0].active&&copy.sequence==2);
 ++shared.sequence;assert(!questReadHands(&shared,copy));++shared.sequence;
 shared.version=99;assert(!questReadHands(&shared,copy));
 for(int hand=0;hand<2;hand++)for(int turned=0;turned<2;turned++){
  QuestHandSample sample{};sample.active=1;
  QuestPose root{{.5f,1.5f,-.3f},{0,0,0,1},15|QUEST_POSE_HAND};
  if(turned){root.orientation[2]=.70710678f;root.orientation[3]=.70710678f;}
  for(int i=0;i<26;i++)sample.joints[i]={{.4f+float(i%5)*.01f,1.2f+float(i/5)*.02f,-.5f+float(i)*.003f},{0,.38268343f,0,.92387953f},15};
  vr::VRBoneTransform_t bones[31];assert(quest::handSkeleton(sample,root,hand,bones));
  vr::VRBoneTransform_t world[31]{};world[0].orientation={root.orientation[3],root.orientation[0],root.orientation[1],root.orientation[2]};
  for(int axis=0;axis<3;axis++)world[0].position.v[axis]=root.position[axis];
  for(int i=1;i<31;i++){
   const auto& parent=world[quest::HandBoneParents[i]];float delta[3];rotate(parent.orientation,bones[i].position.v,delta);
   for(int axis=0;axis<3;axis++)world[i].position.v[axis]=parent.position.v[axis]+delta[axis];
   auto a=parent.orientation,b=bones[i].orientation;
   world[i].orientation={a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z,a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
                         a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w};
   constexpr int auxiliary[5]={4,9,14,19,24};int joint=i<26?i:auxiliary[i-26];
   for(int axis=0;axis<3;axis++)assert(std::fabs(world[i].position.v[axis]-sample.joints[joint].position[axis])<.00001f);
   auto q=bones[i].orientation;assert(std::fabs(q.w*q.w+q.x*q.x+q.y*q.y+q.z*q.z-1)<.00001f);
  }
  sample.active=0;assert(!quest::handSkeleton(sample,root,hand,bones));sample.active=1;
  sample.joints[10].position[0]=std::numeric_limits<float>::quiet_NaN();assert(!quest::handSkeleton(sample,root,hand,bones));
 }
 puts("Hands: protocol validation, both bone bases, rotated pointer roots and measured joint positions passed");
}
