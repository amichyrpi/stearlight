#include "play_space.h"
#include <cassert>
#include <limits>
#include <iostream>
int main(){
 QuestPose head{{1,1.6f,-2},{0,std::sqrt(.5f),0,std::sqrt(.5f)},15};
 auto calibrated=questPlaySpace(head,2,3,"2026-10-12T10:00:00Z");
 assert(!calibrated.empty());std::cout<<calibrated;
 auto unavailable=questPlaySpace(head,0,0,"2026-10-12T10:00:00Z");
 assert(!unavailable.empty());std::cout<<unavailable;
 head.flags=0;assert(questPlaySpace(head,2,3,"").empty());
 head.flags=15;head.position[1]=std::numeric_limits<float>::quiet_NaN();
 assert(questPlaySpace(head,2,3,"").empty());
 head.position[1]=1.6f;head.orientation[3]=0;
 assert(questPlaySpace(head,2,3,"").empty());
}
