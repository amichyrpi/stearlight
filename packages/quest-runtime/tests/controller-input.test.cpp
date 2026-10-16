#include "controller_input.h"
#include <cassert>
#include <limits>
int main(){
 QuestTracking tracking{};tracking.session_visible=tracking.session_focused=1;
 tracking.grip[0].flags=tracking.grip[1].flags=15;
 tracking.buttons[0]=QUEST_X|QUEST_Y;tracking.buttons[1]=QUEST_A;
 tracking.trigger[0]=.5f;tracking.squeeze[1]=1;tracking.thumbstick[0][0]=-.8f;
 auto left=quest::controllerInput(tracking,0),right=quest::controllerInput(tracking,1);
 assert(quest::frameButtons(right,left,1)==(QUEST_A|QUEST_X|QUEST_Y));
 assert(quest::frameButtons(left,left,0)==(QUEST_X|QUEST_Y));
 assert(left.x==-.8f&&left.trigger==.5f&&right.squeeze==1);
 tracking.session_focused=0;
 left=quest::controllerInput(tracking,0);right=quest::controllerInput(tracking,1);
 assert(!quest::frameButtons(right,left,1)&&!left.x&&!left.trigger&&!right.squeeze);
 tracking.session_focused=1;tracking.grip[0].flags=0;
 left=quest::controllerInput(tracking,0);right=quest::controllerInput(tracking,1);
 assert(quest::frameButtons(right,left,1)==QUEST_A&&!left.buttons&&!left.x);
 tracking.grip[0].flags=15;tracking.grip[1].flags=0;
 left=quest::controllerInput(tracking,0);right=quest::controllerInput(tracking,1);
 assert(!quest::frameButtons(right,left,1));
 tracking.grip[0].flags=15;tracking.trigger[0]=std::numeric_limits<float>::quiet_NaN();
 tracking.thumbstick[0][0]=2;tracking.squeeze[0]=-1;
 left=quest::controllerInput(tracking,0);assert(!left.trigger&&left.x==1&&!left.squeeze);
 tracking.session_visible=0;assert(!quest::controllerInput(tracking,1).buttons);
}
