#pragma once
#include "hand_protocol.h"
#include <openvr_driver.h>
#include <cmath>

namespace quest {
struct HandQuaternion { float x,y,z,w; };
inline HandQuaternion handQuaternion(const QuestPose& p){return {p.orientation[0],p.orientation[1],p.orientation[2],p.orientation[3]};}
inline HandQuaternion handInverse(HandQuaternion q){return {-q.x,-q.y,-q.z,q.w};}
inline HandQuaternion handMultiply(HandQuaternion a,HandQuaternion b){return {
 a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
 a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};}
inline void handRotate(HandQuaternion q,const float* position,float* output){
 auto result=handMultiply(handMultiply(q,{position[0],position[1],position[2],0}),handInverse(q));
 output[0]=result.x;output[1]=result.y;output[2]=result.z;
}
inline bool handPoseValid(const QuestPose& p){
 if((p.flags&3)!=3)return false;
 for(float v:p.position)if(!std::isfinite(v))return false;
 float norm=0;for(float v:p.orientation){if(!std::isfinite(v))return false;norm+=v*v;}
 return norm>.9f&&norm<1.1f;
}
constexpr int HandBoneParents[31]={-1,0,1,2,3,4,1,6,7,8,9,1,11,12,13,14,1,16,17,18,19,1,21,22,23,24,0,0,0,0,0};
inline bool handSkeleton(const QuestHandSample& sample,const QuestPose& root,int hand,vr::VRBoneTransform_t (&bones)[31]){
 if(!sample.active||!handPoseValid(root))return false;
 for(const auto& joint:sample.joints)if(!handPoseValid(joint))return false;
 // OpenXR's finger axis is -Z. SteamVR's local bone axes differ by hand.
 // Convert global frames first, then derive parent-relative transforms; this
 // keeps the measured joint positions intact even as the pointer/root turns.
 constexpr float half=.70710678118f;
 HandQuaternion basis=hand?HandQuaternion{0,half,0,half}:HandQuaternion{half,0,-half,0};
 QuestPose global[31]{};global[0]=root;
 for(int i=1;i<26;i++){
  global[i]=sample.joints[i];auto orientation=handMultiply(handQuaternion(global[i]),handInverse(basis));
  global[i].orientation[0]=orientation.x;global[i].orientation[1]=orientation.y;global[i].orientation[2]=orientation.z;global[i].orientation[3]=orientation.w;
 }
 constexpr int auxiliary[5]={4,9,14,19,24};for(int i=0;i<5;i++)global[26+i]=global[auxiliary[i]];
 bones[0]={};bones[0].orientation.w=1;bones[0].position.v[3]=1;
 for(int i=1;i<31;i++){
  const auto& parent=global[HandBoneParents[i]];const auto& current=global[i];
  auto inverse=handInverse(handQuaternion(parent));auto orientation=handMultiply(inverse,handQuaternion(current));
  float delta[3];for(int axis=0;axis<3;axis++)delta[axis]=current.position[axis]-parent.position[axis];
  handRotate(inverse,delta,bones[i].position.v);bones[i].position.v[3]=1;
  bones[i].orientation={orientation.w,orientation.x,orientation.y,orientation.z};
 }
 return true;
}
}
