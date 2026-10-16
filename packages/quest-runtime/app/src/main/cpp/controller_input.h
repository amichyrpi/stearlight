#pragma once
#include "quest_bridge.h"
#include <algorithm>
#include <cmath>

namespace quest {
struct ControllerInput {
 bool active=false;
 uint32_t buttons=0,touches=0;
 float trigger=0,squeeze=0,x=0,y=0;
};
inline ControllerInput controllerInput(const QuestTracking& tracking,int hand) {
 ControllerInput result;
 if(hand<0||hand>1||!tracking.session_focused||!tracking.session_visible||!tracking.grip[hand].flags)return result;
 auto scalar=[](float value,float low){return std::isfinite(value)?std::clamp(value,low,1.f):0.f;};
 result.active=true;
 result.buttons=tracking.buttons[hand];result.touches=tracking.touches[hand];
 result.trigger=scalar(tracking.trigger[hand],0);result.squeeze=scalar(tracking.squeeze[hand],0);
 result.x=scalar(tracking.thumbstick[hand][0],-1);result.y=scalar(tracking.thumbstick[hand][1],-1);
 return result;
}
// Frame puts all four face buttons on its right hand. Keep Touch's original
// per-hand paths as well, for existing motion-controller bindings.
inline uint32_t frameButtons(const ControllerInput& input,const ControllerInput& left,int hand) {
 return hand&&input.active?input.buttons|(left.buttons&(QUEST_X|QUEST_Y)):input.buttons;
}
}
