#include "../app/src/main/cpp/hand_input.h"
#include "../app/src/main/cpp/hand_math.h"
#include "../app/src/main/cpp/gamepad_state.h"
#include "../linux/controller_pose.h"
#include <cassert>
#include <limits>
#include <cstdio>
int main(){
 static_assert(sizeof(QuestTracking)==384,"Keep existing WSI tracking ABI");
 QuestTracking sample{};sample.session_focused=sample.session_visible=1;
 quest::HandInput input;XrHandTrackingAimStateFB aim{XR_TYPE_HAND_TRACKING_AIM_STATE_FB};
 aim.aimPose.orientation.w=1;aim.aimPose.position={.2f,1.2f,-.4f};aim.status=XR_HAND_TRACKING_AIM_VALID_BIT_FB|XR_HAND_TRACKING_AIM_INDEX_PINCHING_BIT_FB;
 assert(input.apply(sample,0,true,aim)&&sample.trigger[0]==0); // reacquisition while pinched
 aim.status=XR_HAND_TRACKING_AIM_VALID_BIT_FB;input.apply(sample,0,true,aim);
 aim.status|=XR_HAND_TRACKING_AIM_INDEX_PINCHING_BIT_FB;input.apply(sample,0,true,aim);
 assert(sample.trigger[0]==1&&(sample.grip[0].flags&QUEST_POSE_HAND));
 auto model=quest::controllerModelPose(sample.grip[0],0);assert(model.position[2]==-.4f&&model.orientation[3]==1);
 auto pad=quest::gamepadState(sample,true,true,1);assert(pad.axes[4]==-1); // hands are not a physical gamepad
 aim.status|=XR_HAND_TRACKING_AIM_SYSTEM_GESTURE_BIT_FB;input.apply(sample,0,true,aim);assert(sample.trigger[0]==0);
 aim.status|=XR_HAND_TRACKING_AIM_MENU_PRESSED_BIT_FB;input.apply(sample,0,true,aim);assert(sample.buttons[0]==QUEST_MENU&&sample.trigger[0]==0);
 aim.status&=~XR_HAND_TRACKING_AIM_MENU_PRESSED_BIT_FB;
 aim.status&=~XR_HAND_TRACKING_AIM_SYSTEM_GESTURE_BIT_FB;input.apply(sample,0,true,aim);assert(sample.trigger[0]==0);
 aim.status&=~XR_HAND_TRACKING_AIM_INDEX_PINCHING_BIT_FB;input.apply(sample,0,true,aim);
 aim.status|=XR_HAND_TRACKING_AIM_MENU_PRESSED_BIT_FB;input.apply(sample,0,true,aim);assert(sample.buttons[0]==QUEST_MENU);
 sample.session_focused=0;input.apply(sample,0,true,aim);assert(!sample.grip[0].flags&&!sample.buttons[0]);
 sample.session_focused=1;aim.aimPose.position.x=std::numeric_limits<float>::quiet_NaN();assert(!input.apply(sample,0,true,aim));
 aim.aimPose.position.x=0;aim.status=0;assert(!input.apply(sample,0,true,aim));
 input.apply(sample,0,false,aim);assert(!sample.grip[0].flags&&!sample.trigger[0]);
 XrPosef p{};p.orientation.w=1;p.position={1,2,3};auto product=quest::multiply(quest::poseMatrix(p),quest::inversePoseMatrix(p));
 for(int i=0;i<16;i++)assert(std::fabs(product[i]-(i%5==0?1.f:0.f))<.00001f);
 p.orientation={0,std::sqrt(.5f),0,std::sqrt(.5f)};product=quest::multiply(quest::poseMatrix(p),quest::inversePoseMatrix(p));
 for(int i=0;i<16;i++)assert(std::fabs(product[i]-(i%5==0?1.f:0.f))<.00001f);
 auto skin=quest::handSkinMatrix(p,p,1);for(int i=0;i<16;i++)assert(std::fabs(skin[i]-(i%5==0?1.f:0.f))<.00001f);
 puts("Hand aim, pinch release/rearm, system gestures, gamepad exclusion and pose transforms passed");
}
