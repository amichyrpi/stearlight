#pragma once
#include "konami.h"
#include "tracker_protocol.h"
#include <openxr/openxr.h>
#include <fstream>
#include <string>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

// ABI from Khronos OpenXR-SDK release-1.1.54. Keep compatibility with the
// older pinned headers; load the optional META functions only at runtime.
class Multimodal {
public:
 struct Properties {XrStructureType type=static_cast<XrStructureType>(1000532001);void* next=nullptr;XrBool32 supported=XR_FALSE;};
 struct Info {XrStructureType type;const void* next=nullptr;};
private:
 using Toggle=XrResult(XRAPI_PTR*)(XrSession,const Info*);
 Toggle resume=nullptr,pause=nullptr;
 XrSession session=XR_NULL_HANDLE; std::string directory;
 bool wanted=false,detachedAvailable=false;quest::Konami code;QuestTrackers* shared=nullptr;
 void status(const char* text){
  std::ofstream out(directory+"/multimodal-status.next");out<<text<<'\n';out.close();
  if(out)rename((directory+"/multimodal-status.next").c_str(),(directory+"/multimodal-status").c_str());
 }
public:
 bool supported=false,detached=false,active=false,attempted=false;
 // Device role conversion is independent of Meta's detached-pose capability.
 // Unsupported detached controllers remain trackers only while poses exist.
 bool handsAreControllers()const{return active;}
 const char* activeStatus()const{return detached?"hands-and-trackers":"hands-and-trackers-held-only";}
 void controllerCompatibility(bool allowed){
  bool next=detachedAvailable&&allowed;if(next==detached)return;detached=next;
  if(active)status(activeStatus());
 }
 void initialize(XrInstance instance,XrSystemId system,XrSession value,const std::string& path,bool advertised,bool detachedExtension,bool hands){
  session=value;directory=path;detached=detachedAvailable=advertised&&detachedExtension&&hands;
  std::ifstream setting(directory+"/multimodal-mode");char flag=0;setting>>flag;wanted=flag=='1';
  if(advertised&&hands){
   Properties support;XrSystemProperties props{XR_TYPE_SYSTEM_PROPERTIES};props.next=&support;
   PFN_xrVoidFunction r=nullptr,p=nullptr;
   supported=XR_SUCCEEDED(xrGetSystemProperties(instance,system,&props))&&support.supported&&
    XR_SUCCEEDED(xrGetInstanceProcAddr(instance,"xrResumeSimultaneousHandsAndControllersTrackingMETA",&r))&&
    XR_SUCCEEDED(xrGetInstanceProcAddr(instance,"xrPauseSimultaneousHandsAndControllersTrackingMETA",&p))&&r&&p;
   resume=reinterpret_cast<Toggle>(r);pause=reinterpret_cast<Toggle>(p);
  }
  int fd=open((directory+"/controller-trackers").c_str(),O_CREAT|O_RDWR|O_CLOEXEC,0600);
  if(fd>=0){if(!ftruncate(fd,sizeof(QuestTrackers))){auto p=mmap(nullptr,sizeof(QuestTrackers),PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);if(p!=MAP_FAILED){shared=static_cast<QuestTrackers*>(p);*shared=QuestTrackers{};}}close(fd);}
  std::ofstream capabilities(directory+"/multimodal-capabilities");
  capabilities<<"simultaneous="<<supported<<"\ndetached_extension="<<detachedAvailable<<"\n";
  status(supported?"off":"unsupported-runtime");
 }
 void begin(){
  active=false;code.reset();
  if(!wanted||!supported)return;
  Info info{static_cast<XrStructureType>(1000532002)};
  if(XR_SUCCEEDED(resume(session,&info)))active=true;
  status(active?activeStatus():"runtime-rejected-mode");
 }
 void end(){if(active&&supported){Info info{static_cast<XrStructureType>(1000532003)};pause(session,&info);}active=false;code.reset();questPublishTrackers(shared,QuestTrackers{});}
 bool input(float x,float y,bool a,bool b,bool focused,uint64_t now){
  attempted=false;
  if(!code.update(x,y,a,b,focused,now))return false;
  attempted=true;
  if(!supported&&!wanted){status("unsupported-runtime");return false;}
  bool next=!wanted;Info info{static_cast<XrStructureType>(next?1000532002:1000532003)};
  XrResult result=supported?(next?resume:pause)(session,&info):XR_SUCCESS;
  if(XR_FAILED(result)){status("runtime-rejected-mode");return false;}
  std::ofstream out(directory+"/multimodal-mode.next");out<<(next?'1':'0')<<'\n';out.close();
  if(!out||rename((directory+"/multimodal-mode.next").c_str(),(directory+"/multimodal-mode").c_str())){
   Info undo{static_cast<XrStructureType>(next?1000532003:1000532002)};
   if(supported)(next?pause:resume)(session,&undo);
   status("cannot-save-mode");return false;
  }
  wanted=active=next;status(active?activeStatus():"off");return true;
 }
 void publish(const QuestTracking& tracking,const QuestPose* controllers){
  QuestTrackers sample;sample.enabled=active;sample.predicted_monotonic_ns=tracking.predicted_monotonic_ns;
  if(sample.enabled&&tracking.session_visible)for(int i=0;i<2;i++){
   sample.poses[i]=controllers[i];
   if((sample.poses[i].flags&15)!=15)sample.poses[i].flags=0;
  }
  questPublishTrackers(shared,sample);
 }
 void shutdown(){end();if(shared)munmap(shared,sizeof(QuestTrackers));shared=nullptr;}
};
