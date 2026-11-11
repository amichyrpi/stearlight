#include "multimodal.h"
#include <cassert>
#include <filesystem>
#include <limits>
#include <cstdio>

static bool support=true,failResume=false;static unsigned resumes=0,pauses=0;
static XrResult XRAPI_CALL fakeResume(XrSession,const Multimodal::Info* info){assert(int(info->type)==1000532002);++resumes;return failResume?XR_ERROR_FEATURE_UNSUPPORTED:XR_SUCCESS;}
static XrResult XRAPI_CALL fakePause(XrSession,const Multimodal::Info* info){assert(int(info->type)==1000532003);++pauses;return XR_SUCCESS;}
extern "C" XrResult XRAPI_CALL xrGetSystemProperties(XrInstance,XrSystemId,XrSystemProperties* properties){
 auto* value=static_cast<Multimodal::Properties*>(properties->next);assert(int(value->type)==1000532001);value->supported=support;return XR_SUCCESS;
}
extern "C" XrResult XRAPI_CALL xrGetInstanceProcAddr(XrInstance,const char* name,PFN_xrVoidFunction* function){
 *function=reinterpret_cast<PFN_xrVoidFunction>(strstr(name,"Resume")?fakeResume:fakePause);return XR_SUCCESS;
}
static uint64_t now=1;
static bool toggle(Multimodal& mode){
 constexpr int pattern[]={1,1,2,2,3,4,3,4,5,6};bool changed=false;
 for(int key:pattern){now+=100000000;assert(!mode.input(0,0,false,false,true,now));now+=100000000;
  bool result=mode.input(key==3?-1.f:key==4?1.f:0.f,key==1?1.f:key==2?-1.f:0.f,key==6,key==5,true,now);
  if(key!=6)assert(!result);else changed=result;
 }return changed;
}
int main(){
 char directory[]="/tmp/stearlight-multimodal-XXXXXX";assert(mkdtemp(directory));
 auto session=reinterpret_cast<XrSession>(1);
 auto initialize=[&](Multimodal& mode,bool advertised,bool detached){mode.initialize(XR_NULL_HANDLE,1,session,directory,advertised,detached,true);mode.begin();};
 {
  Multimodal mode;initialize(mode,false,false);assert(!mode.active&&!mode.supported);assert(!toggle(mode));mode.shutdown();
 }
 {
  support=false;Multimodal mode;initialize(mode,true,true);assert(!mode.supported&&!mode.active);assert(!toggle(mode));mode.shutdown();support=true;
 }
 {
  Multimodal mode;initialize(mode,true,false);assert(!mode.active&&mode.supported);assert(toggle(mode));assert(mode.active&&!mode.detached&&mode.handsAreControllers());
  // Original Touch still gets GenericTracker devices, without claiming
  // detached hardware support or leaking its buttons into the hand devices.
  QuestTracking tracking{};tracking.session_visible=1;tracking.predicted_monotonic_ns=1000000000;
  QuestPose controllers[2]{};controllers[0].orientation[3]=1;controllers[0].flags=15;
  auto read=[&](){QuestTrackers shared;std::ifstream file(std::string(directory)+"/controller-trackers",std::ios::binary);file.read(reinterpret_cast<char*>(&shared),sizeof(shared));assert(file);return shared;};
  mode.publish(tracking,controllers);auto shared=read();assert(shared.enabled&&shared.poses[0].flags==15&&shared.poses[1].flags==0);
  tracking.session_visible=0;mode.publish(tracking,controllers);shared=read();assert(shared.enabled&&shared.poses[0].flags==0);
  std::ifstream in(std::string(directory)+"/multimodal-mode");char flag=0;in>>flag;assert(flag=='1');
  mode.end();assert(!mode.active&&!read().enabled);mode.begin();assert(mode.active&&mode.handsAreControllers());mode.shutdown();
 }
 {
  Multimodal mode;initialize(mode,true,true);assert(mode.active&&mode.detached&&mode.handsAreControllers());
  mode.controllerCompatibility(false);assert(!mode.detached&&mode.handsAreControllers());mode.controllerCompatibility(true);assert(mode.detached&&mode.handsAreControllers());
  assert(toggle(mode));assert(!mode.active&&!mode.handsAreControllers());mode.shutdown();
 }
 {
  Multimodal mode;initialize(mode,true,true);failResume=true;assert(!toggle(mode));assert(!mode.active);mode.shutdown();failResume=false;
 }
 {
  Multimodal mode;initialize(mode,true,true);assert(!mode.active);assert(toggle(mode));mode.shutdown();
  // A downgrade to a runtime without the extension must still let the user
  // clear an earlier saved preference using the same controller gesture.
  Multimodal unsupported;initialize(unsupported,false,false);assert(toggle(unsupported));assert(!unsupported.active);unsupported.shutdown();
 }
 {
  QuestTrackers shared,sample;sample.enabled=1;sample.predicted_monotonic_ns=1000000000;
  sample.poses[0].orientation[3]=1;sample.poses[0].flags=15;
  questPublishTrackers(&shared,sample);QuestTrackers received;assert(questReadTrackers(&shared,received));
  assert(received.enabled&&questTrackerPoseValid(received.poses[0],received.predicted_monotonic_ns,1000000000));
  assert(!questTrackerPoseValid(received.poses[0],received.predicted_monotonic_ns,1300000000));
  received.poses[0].flags=3;assert(!questTrackerPoseValid(received.poses[0],1000000000,1000000000));
  received.poses[0].flags=15;received.poses[0].position[0]=std::numeric_limits<float>::quiet_NaN();
  assert(!questTrackerPoseValid(received.poses[0],1000000000,1000000000));
  shared.sequence|=1;assert(!questReadTrackers(&shared,received));
 }
 assert(resumes>=4&&pauses>=1);
 std::filesystem::remove_all(directory);
 puts("Multimodal: capability gating, runtime rejection, persistent toggles, session restart, downgrade and tracker validity pass");
}
