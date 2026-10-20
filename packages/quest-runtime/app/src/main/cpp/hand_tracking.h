#pragma once
#include "hand_input.h"
#include "hand_protocol.h"
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <android/log.h>
#include <vector>
#include <array>
#include <string>

class HandTracking {
 PFN_xrCreateHandTrackerEXT create=nullptr;
 PFN_xrDestroyHandTrackerEXT destroy=nullptr;
 PFN_xrLocateHandJointsEXT locate=nullptr;
 template<class T> bool load(XrInstance instance,const char* name,T& target){
  PFN_xrVoidFunction p=nullptr;auto result=xrGetInstanceProcAddr(instance,name,&p);
  target=reinterpret_cast<T>(p);return XR_SUCCEEDED(result)&&target;
 }
 quest::HandInput input;
 QuestHands* shared=nullptr;
public:
 struct Hand {
  XrHandTrackerEXT tracker=XR_NULL_HANDLE;
  XrHandJointLocationEXT joints[XR_HAND_JOINT_COUNT_EXT]{};
  XrHandTrackingAimStateFB aim{XR_TYPE_HAND_TRACKING_AIM_STATE_FB};
  bool visible=false;float scale=1;
 } hands[2];
 PFN_xrGetHandMeshFB getMesh=nullptr;
 bool available()const{return hands[0].tracker||hands[1].tracker;}
 void initialize(XrInstance instance,XrSystemId system,XrSession session,bool enabled,bool mesh,const std::string& directory){
  int fd=open((directory+"/hands").c_str(),O_CREAT|O_RDWR|O_CLOEXEC,0600);
  if(fd>=0){
   if(!ftruncate(fd,sizeof(QuestHands))){void* mapping=mmap(nullptr,sizeof(QuestHands),PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);if(mapping!=MAP_FAILED){shared=static_cast<QuestHands*>(mapping);*shared={};shared->magic=QUEST_HAND_MAGIC;shared->version=QUEST_HAND_VERSION;}}
   close(fd);
  }
  if(!enabled)return;
  XrSystemHandTrackingPropertiesEXT support{XR_TYPE_SYSTEM_HAND_TRACKING_PROPERTIES_EXT};
  XrSystemProperties props{XR_TYPE_SYSTEM_PROPERTIES};props.next=&support;
  if(XR_FAILED(xrGetSystemProperties(instance,system,&props))||!support.supportsHandTracking)return;
  if(!load(instance,"xrCreateHandTrackerEXT",create)||!load(instance,"xrDestroyHandTrackerEXT",destroy)||!load(instance,"xrLocateHandJointsEXT",locate))return;
  if(mesh)load(instance,"xrGetHandMeshFB",getMesh);
  for(int i=0;i<2;i++){
   XrHandTrackerCreateInfoEXT info{XR_TYPE_HAND_TRACKER_CREATE_INFO_EXT};info.hand=i?XR_HAND_RIGHT_EXT:XR_HAND_LEFT_EXT;info.handJointSet=XR_HAND_JOINT_SET_DEFAULT_EXT;
   auto result=create(session,&info,&hands[i].tracker);
   __android_log_print(ANDROID_LOG_INFO,"StearlightXR","Hand tracker %d result=%d",i,result);
  }
 }
 void update(QuestTracking& sample,int hand,bool controller,XrSpace space,XrTime time){
  auto& h=hands[hand];h.visible=false;
  if(controller){input.reset(hand);return;}
  if(!h.tracker)return;
  h.aim={XR_TYPE_HAND_TRACKING_AIM_STATE_FB};
  XrHandTrackingScaleFB scale{XR_TYPE_HAND_TRACKING_SCALE_FB};scale.overrideHandScale=XR_FALSE;scale.overrideValueInput=1;
  if(getMesh)h.aim.next=&scale;
  XrHandJointLocationsEXT locations{XR_TYPE_HAND_JOINT_LOCATIONS_EXT};locations.next=&h.aim;locations.jointCount=XR_HAND_JOINT_COUNT_EXT;locations.jointLocations=h.joints;
  XrHandJointsLocateInfoEXT info{XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT};info.baseSpace=space;info.time=time;
  auto result=locate(h.tracker,&info,&locations);
  h.aim.next=nullptr;
  bool active=XR_SUCCEEDED(result)&&locations.isActive;
  input.apply(sample,hand,active,h.aim);
  h.visible=active&&sample.session_visible&&sample.session_focused;
  for(auto& joint:h.joints)h.visible=h.visible&&(joint.locationFlags&3)==3&&quest::validHandPose(joint.pose);
  h.scale=getMesh&&std::isfinite(scale.currentOutput)&&scale.currentOutput>.5f&&scale.currentOutput<2?scale.currentOutput:1;
 }
 bool visible()const{return hands[0].visible||hands[1].visible;}
 void publish(const QuestTracking& tracking){
  if(!shared)return;
  QuestHands sample{};sample.predicted_monotonic_ns=tracking.predicted_monotonic_ns;
  for(int hand=0;hand<2;hand++){
   auto& output=sample.hands[hand];const auto& h=hands[hand];
   output.active=h.visible&&(tracking.grip[hand].flags&QUEST_POSE_HAND)!=0;
   if(!output.active)continue;
   for(int joint=0;joint<QUEST_HAND_JOINTS;joint++){
    const auto& p=h.joints[joint];output.joints[joint]={{p.pose.position.x,p.pose.position.y,p.pose.position.z},{p.pose.orientation.x,p.pose.orientation.y,p.pose.orientation.z,p.pose.orientation.w},uint32_t(p.locationFlags)};
   }
  }
  questPublishHands(shared,sample);
 }
 void shutdown(){for(auto& h:hands)if(h.tracker){destroy(h.tracker);h.tracker=XR_NULL_HANDLE;}if(shared){questPublishHands(shared,QuestHands{});munmap(shared,sizeof(QuestHands));shared=nullptr;}}
};
