#pragma once
#include "quest_bridge.h"
#include <cmath>

namespace quest {
// Valve's Quest 2 and Touch Plus models share this openxr_grip component:
// origin (+/-0.007, -0.00182941, 0.1019482), rotate_xyz (20.6, 0, 0).
// OpenXR locates this component, while OpenVR locates the model's origin.
inline QuestPose controllerComponentPose(const QuestPose& grip,float degrees,const float* t) {
 const float angle = degrees * 3.14159265358979323846f / 360.f;
 const float s = std::sin(angle), c = std::cos(angle);
 const auto* q = grip.orientation;
 QuestPose model = grip;
 auto* r = model.orientation;
 // worldFromModel = worldFromGrip * inverse(modelFromGrip).
 r[0] = -q[3]*s + q[0]*c;
 r[1] = q[1]*c - q[2]*s;
 r[2] = q[2]*c + q[1]*s;
 r[3] = q[3]*c + q[0]*s;
 const float cross[3] = {r[1]*t[2]-r[2]*t[1], r[2]*t[0]-r[0]*t[2], r[0]*t[1]-r[1]*t[0]};
 const float twice[3] = {2*cross[0], 2*cross[1], 2*cross[2]};
 const float rotated[3] = {
  t[0]+r[3]*twice[0]+r[1]*twice[2]-r[2]*twice[1],
  t[1]+r[3]*twice[1]+r[2]*twice[0]-r[0]*twice[2],
  t[2]+r[3]*twice[2]+r[0]*twice[1]-r[1]*twice[0]
 };
 for(int i=0;i<3;i++) model.position[i] -= rotated[i];
 return model;
}
inline QuestPose controllerModelPose(const QuestPose& grip,int hand){
 if(grip.flags&QUEST_POSE_HAND)return grip;
 const float t[3]={hand?-.007f:.007f,-.00182941f,.1019482f};
 return controllerComponentPose(grip,20.6f,t);
}
inline QuestPose controllerModelPose(const QuestPose& grip,const QuestPose& aim,int hand){
 // Keep Quest 2's calibrated grip path. For Touch Plus, anchor the model's
 // openxr_aim component to the runtime's actual pointing space, instead of
 // assuming the newer profile's grip origin matches legacy Touch. These
 // constants come from Valve's Touch Plus render-model component definition.
 if(!(grip.flags&QUEST_POSE_HAND)&&(grip.flags&QUEST_POSE_TOUCH_PLUS)&&
    (grip.flags&3)==3&&(aim.flags&3)==3){
  const float t[3]={hand?-.007f:.007f,-.03894766f,.00949694f};
  auto model=controllerComponentPose(aim,-39.4f,t);model.flags=grip.flags;return model;
 }
 return controllerModelPose(grip,hand);
}
}
