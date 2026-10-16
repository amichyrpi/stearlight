#pragma once
#include "quest_bridge.h"
#include <openxr/openxr.h>
#include <cmath>
#include <initializer_list>

namespace quest {
inline bool validHandPose(const XrPosef& p) {
 for(float v:{p.position.x,p.position.y,p.position.z,p.orientation.x,p.orientation.y,p.orientation.z,p.orientation.w})if(!std::isfinite(v))return false;
 float norm=p.orientation.x*p.orientation.x+p.orientation.y*p.orientation.y+p.orientation.z*p.orientation.z+p.orientation.w*p.orientation.w;
 return norm>.9f&&norm<1.1f;
}
// On source changes, wait for an open hand before accepting a pinch. A held
// pinch must never become a click when tracking returns or Horizon closes.
class HandInput {
 bool armed[2]{};
public:
 void reset(int hand){armed[hand]=false;}
 bool apply(QuestTracking& sample,int hand,bool active,const XrHandTrackingAimStateFB& aim) {
  sample.grip[hand]={};sample.aim[hand]={};
  sample.trigger[hand]=sample.squeeze[hand]=0;sample.buttons[hand]=sample.touches[hand]=0;
  sample.thumbstick[hand][0]=sample.thumbstick[hand][1]=0;
  bool valid=active&&sample.session_visible&&sample.session_focused&&
   (aim.status&XR_HAND_TRACKING_AIM_VALID_BIT_FB)&&validHandPose(aim.aimPose);
  if(!valid){reset(hand);return false;}
  auto& p=aim.aimPose;
  sample.grip[hand]={{p.position.x,p.position.y,p.position.z},{p.orientation.x,p.orientation.y,p.orientation.z,p.orientation.w},15|QUEST_POSE_HAND};
  sample.aim[hand]=sample.grip[hand];
  // Meta supplies this one-frame event on the non-dominant hand. It can
  // coincide with system-gesture processing; forward only this menu event.
  if(aim.status&XR_HAND_TRACKING_AIM_MENU_PRESSED_BIT_FB)sample.buttons[hand]=QUEST_MENU;
  if(aim.status&XR_HAND_TRACKING_AIM_SYSTEM_GESTURE_BIT_FB){reset(hand);return true;}
  bool pinch=aim.status&XR_HAND_TRACKING_AIM_INDEX_PINCHING_BIT_FB;
  if(!pinch)armed[hand]=true;
  sample.trigger[hand]=armed[hand]&&pinch?1:0;
  sample.touches[hand]=sample.trigger[hand]>0;
  return true;
 }
};
}
