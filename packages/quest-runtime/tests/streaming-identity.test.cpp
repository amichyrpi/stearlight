#include "../linux/streaming_identity.h"
#include <cassert>
#include <cstdio>
#include <typeinfo>

class System:public quest::StreamingIdentity {
public:
 const char* model="Oculus Quest2";unsigned calls=0;
 uint32_t GetStringTrackedDeviceProperty(uint32_t device,vr::ETrackedDeviceProperty property,char* out,uint32_t size,vr::ETrackedPropertyError* error) override {
  ++calls;const char* value=property==vr::Prop_ModelNumber_String?(device?"Touch controller":model):"real serial";
  auto required=uint32_t(strlen(value)+1);
  if(out&&size>=required){memcpy(out,value,required);if(error)*error=vr::TrackedProp_Success;}
  else if(error)*error=vr::TrackedProp_BufferTooSmall;
  return required;
 }
 void GetRecommendedRenderTargetSize(uint32_t* width,uint32_t* height) override {*width=1832;*height=1920;++calls;}
 bool GetTimeSinceLastVsync(float* seconds,uint64_t* frame) override {*seconds=.002f;*frame=42;++calls;return true;}
};
int main(){
 System system;auto* object=static_cast<vr::IVRSystem*>(&system);
 assert(quest::StreamingIdentity::wrap(nullptr,object)==object);
 assert(quest::StreamingIdentity::wrap("IVRSystem_025",object)==object);
 assert(!quest::StreamingIdentity::wrap("IVRSystem_026",nullptr));
 assert(quest::StreamingIdentity::wrap("FnTable:IVRSystem_026",object)==object);
 auto* wrapped=static_cast<vr::IVRSystem*>(quest::StreamingIdentity::wrap("IVRSystem_026",object));
 assert(wrapped!=object&&typeid(*object)==typeid(System));
 assert(quest::StreamingIdentity::wrap("IVRSystem_026",object)==wrapped);
 assert(quest::StreamingIdentity::wrap("IVRSystem_026",wrapped)==wrapped);
 char out[128]{};vr::ETrackedPropertyError error{};
 for(const char* model:{"Oculus Quest2","Meta Quest 3","Meta Quest 3S"}){
  system.model=model;
  assert(wrapped->GetStringTrackedDeviceProperty(0,vr::Prop_ModelNumber_String,out,sizeof(out),&error)==12);
  assert(error==vr::TrackedProp_Success&&!strcmp(out,"Steam Frame"));
  object->GetStringTrackedDeviceProperty(0,vr::Prop_ModelNumber_String,out,sizeof(out),&error);assert(!strcmp(out,model));
 }
 unsigned char guard[4]={0xaa,0xbb,0xcc,0xdd};
 assert(wrapped->GetStringTrackedDeviceProperty(0,vr::Prop_ModelNumber_String,reinterpret_cast<char*>(guard+1),1,&error)==12);
 assert(error==vr::TrackedProp_BufferTooSmall&&guard[0]==0xaa&&!guard[1]&&guard[2]==0xcc&&guard[3]==0xdd);
 assert(wrapped->GetStringTrackedDeviceProperty(0,vr::Prop_ModelNumber_String,nullptr,0,&error)==12&&error==vr::TrackedProp_BufferTooSmall);
 wrapped->GetStringTrackedDeviceProperty(1,vr::Prop_ModelNumber_String,out,sizeof(out),&error);assert(!strcmp(out,"Touch controller"));
 wrapped->GetStringTrackedDeviceProperty(0,vr::Prop_SerialNumber_String,out,sizeof(out),&error);assert(!strcmp(out,"real serial"));
 system.model="Unrecognized headset";
 wrapped->GetStringTrackedDeviceProperty(0,vr::Prop_ModelNumber_String,out,sizeof(out),&error);assert(!strcmp(out,system.model));
 uint32_t width=0,height=0;wrapped->GetRecommendedRenderTargetSize(&width,&height);assert(width==1832&&height==1920);
 float seconds=0;uint64_t frame=0;assert(wrapped->GetTimeSinceLastVsync(&seconds,&frame)&&seconds==.002f&&frame==42);
 puts("Streaming identity: Frame handshake only; native object, display, controllers and serial preserved");
}
