#include "../linux/eye_projection.h"
#include <cassert>
#include <cstdio>
#include <limits>
#include <initializer_list>

static void near(float a,float b){assert(std::fabs(a-b)<1e-5f);}
int main(){
 // Test unequal vertical and horizontal half-angles. Symmetric 90-degree
 // fixtures cannot catch a swapped/negated up/down mapping.
 const float fov[2][4]={{-.91f,.74f,.69f,-.88f},{-.74f,.91f,.69f,-.88f}};
 for(const auto& eye:fov){
  assert(quest::validEyeFov(eye));auto raw=quest::eyeProjection(eye);
  near(std::atan(raw.left),eye[0]);near(std::atan(raw.right),eye[1]);
  near(std::atan(raw.top),eye[3]);near(std::atan(raw.bottom),eye[2]);
  // OpenXR's up/down and OpenVR's bottom/top must project the same rays to
  // the same image coordinates, including the optical axis (not just edges).
  for(float y:{-.5f,0.f,.5f}){
   float xr=(y-std::tan(eye[3]))/(std::tan(eye[2])-std::tan(eye[3]));
   float vr=(y-raw.top)/(raw.bottom-raw.top);near(xr,vr);
  }
 }
 float bad[4]={0,0,0,0};assert(!quest::validEyeFov(bad));
 bad[0]=-.7f;bad[1]=.7f;bad[2]=.7f;bad[3]=-.7f;assert(quest::validEyeFov(bad));
 auto symmetric=quest::eyeProjection(bad);near(symmetric.top,-symmetric.bottom);
 bad[0]=std::numeric_limits<float>::quiet_NaN();assert(!quest::validEyeFov(bad));
 bad[0]=-1.6f;assert(!quest::validEyeFov(bad));
 puts("Asymmetric stereo projection preserves OpenXR rays and optical center");
}
