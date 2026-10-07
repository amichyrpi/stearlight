#include "../linux/controller_pose.h"
#include <cassert>
#include <cstdio>

static void near(float actual, float expected) { assert(std::fabs(actual-expected)<.00001f); }

int main() {
 const float a=20.6f*3.14159265358979323846f/360.f;
 const float s=std::sin(a), c=std::cos(a), h=std::sqrt(.5f);
 for(int hand=0;hand<2;hand++) {
  const float x=hand?-.007f:.007f;
  // Valve's grip component located on an unrotated model at the world origin.
  QuestPose grip{{x,-.00182941f,.1019482f},{s,0,0,c},15};
  auto model=quest::controllerModelPose(grip,hand);
  for(float p:model.position)near(p,0);
  near(model.orientation[0],0);near(model.orientation[1],0);
  near(model.orientation[2],0);near(model.orientation[3],1);
  assert(model.flags==15);near(grip.position[0],x);
  // The same model at (1,2,3), turned 90 degrees around world Y.
  // Its component translation now points along X; a world-space subtraction
  // or reversed quaternion product fails this check.
  grip={{1+.1019482f,2-.00182941f,3-x},{h*s,h*c,-h*s,h*c},3};
  model=quest::controllerModelPose(grip,hand);
  near(model.position[0],1);near(model.position[1],2);near(model.position[2],3);
  near(model.orientation[0],0);near(model.orientation[1],h);
  near(model.orientation[2],0);near(model.orientation[3],h);
  assert(model.flags==3);
  grip.flags=0;assert(quest::controllerModelPose(grip,hand).flags==0);
 }
 std::puts("Quest controller model/grip alignment passed for both hands");
}
