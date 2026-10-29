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
  // Touch Plus's live aiming space must locate the model even if its grip
  // frame differs from legacy Touch. Place Valve's aim component on a model
  // at (1,2,3), yawed 90 degrees, and reconstruct that model origin.
  const float aimAngle=-39.4f*3.14159265358979323846f/360.f;
  const float as=std::sin(aimAngle),ac=std::cos(aimAngle);
  QuestPose aim{{1+.00949694f,2-.03894766f,3-x},{h*as,h*ac,-h*as,h*ac},3};
  grip={{7,8,9},{0,0,0,1},3|QUEST_POSE_TOUCH_PLUS};
  model=quest::controllerModelPose(grip,aim,hand);
  near(model.position[0],1);near(model.position[1],2);near(model.position[2],3);
  near(model.orientation[0],0);near(model.orientation[1],h);
  near(model.orientation[2],0);near(model.orientation[3],h);
  assert(model.flags==grip.flags);
  // Sleeping/unavailable aim falls back to the calibrated grip transform.
  aim.flags=0;auto fallback=quest::controllerModelPose(grip,aim,hand);
  auto legacy=quest::controllerModelPose(grip,hand);
  for(int i=0;i<3;i++)near(fallback.position[i],legacy.position[i]);
  for(int i=0;i<4;i++)near(fallback.orientation[i],legacy.orientation[i]);
  aim.flags=3;grip.flags=3;auto two=quest::controllerModelPose(grip,aim,hand);
  legacy=quest::controllerModelPose(grip,hand);
  for(int i=0;i<3;i++)near(two.position[i],legacy.position[i]);
  grip.flags|=QUEST_POSE_HAND|QUEST_POSE_TOUCH_PLUS;
  auto trackedHand=quest::controllerModelPose(grip,aim,hand);
  for(int i=0;i<3;i++)near(trackedHand.position[i],grip.position[i]);
  for(int i=0;i<4;i++)near(trackedHand.orientation[i],grip.orientation[i]);
 }
 std::puts("Quest 2 grip, Touch Plus aim, fallback and hand alignment passed for both hands");
}
