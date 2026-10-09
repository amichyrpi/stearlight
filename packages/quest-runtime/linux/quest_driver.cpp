// OpenVR devices backed by real Quest OpenXR poses and Android device state.
#include <openvr_driver.h>
#include "quest_bridge.h"
#include "controller_pose.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <unistd.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <array>
#include <memory>
#include <chrono>
#include <time.h>

namespace {
QuestTracking state{};const QuestTracking* shared=nullptr;
bool readTracking(){if(!shared)return false;for(int attempt=0;attempt<8;attempt++){uint32_t before=__atomic_load_n(&shared->sequence,__ATOMIC_ACQUIRE);if(before&1)continue;memcpy(&state,shared,sizeof(state));uint32_t after=__atomic_load_n(&shared->sequence,__ATOMIC_ACQUIRE);if(before==after)return state.magic==QUEST_BRIDGE_MAGIC&&state.version==QUEST_BRIDGE_VERSION;}return false;}
vr::DriverPose_t trackedPose(const QuestPose& source){vr::DriverPose_t p{};p.qWorldFromDriverRotation.w=p.qDriverFromHeadRotation.w=1;p.qRotation={source.orientation[3],source.orientation[0],source.orientation[1],source.orientation[2]};for(int i=0;i<3;i++)p.vecPosition[i]=source.position[i];if(state.predicted_monotonic_ns){timespec now{};clock_gettime(CLOCK_MONOTONIC,&now);double seconds=static_cast<double>(now.tv_sec)+now.tv_nsec*1e-9;p.poseTimeOffset=static_cast<double>(state.predicted_monotonic_ns)*1e-9-seconds;}p.poseIsValid=(source.flags&3)==3;p.deviceIsConnected=state.session_visible&&source.flags!=0;p.result=p.poseIsValid?vr::TrackingResult_Running_OK:vr::TrackingResult_Running_OutOfRange;return p;}
vr::HmdQuaternion_t multiply(vr::HmdQuaternion_t a,vr::HmdQuaternion_t b){return {a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z,a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w};}
vr::HmdMatrix34_t eyeToHead(int eye){const auto& h=state.headset;const auto& e=state.eyes[eye];vr::HmdQuaternion_t inverse{h.orientation[3],-h.orientation[0],-h.orientation[1],-h.orientation[2]},eq{e.orientation[3],e.orientation[0],e.orientation[1],e.orientation[2]},q=multiply(inverse,eq);vr::HmdQuaternion_t delta{0,e.position[0]-h.position[0],e.position[1]-h.position[1],e.position[2]-h.position[2]},normal{h.orientation[3],h.orientation[0],h.orientation[1],h.orientation[2]},p=multiply(multiply(inverse,delta),normal);vr::HmdMatrix34_t matrix{};matrix.m[0][0]=1-2*(q.y*q.y+q.z*q.z);matrix.m[0][1]=2*(q.x*q.y-q.z*q.w);matrix.m[0][2]=2*(q.x*q.z+q.y*q.w);matrix.m[1][0]=2*(q.x*q.y+q.z*q.w);matrix.m[1][1]=1-2*(q.x*q.x+q.z*q.z);matrix.m[1][2]=2*(q.y*q.z-q.x*q.w);matrix.m[2][0]=2*(q.x*q.z-q.y*q.w);matrix.m[2][1]=2*(q.y*q.z+q.x*q.w);matrix.m[2][2]=1-2*(q.x*q.x+q.y*q.y);matrix.m[0][3]=p.x;matrix.m[1][3]=p.y;matrix.m[2][3]=p.z;return matrix;}

class Device:public vr::ITrackedDeviceServerDriver {
protected:vr::TrackedDeviceIndex_t index=vr::k_unTrackedDeviceIndexInvalid;vr::PropertyContainerHandle_t properties=0;
public:
 void Deactivate()override{index=vr::k_unTrackedDeviceIndexInvalid;}
 void EnterStandby()override{}
 void* GetComponent(const char*)override{return nullptr;}
 void DebugRequest(const char*,char* response,uint32_t size)override{if(response&&size)*response=0;}
 void base(uint32_t id,const char* serial,const char* model){index=id;properties=vr::VRProperties()->TrackedDeviceToPropertyContainer(id);auto* p=vr::VRProperties();p->SetStringProperty(properties,vr::Prop_TrackingSystemName_String,"stearlight-quest");p->SetStringProperty(properties,vr::Prop_ModelNumber_String,model);p->SetStringProperty(properties,vr::Prop_ManufacturerName_String,"Meta");p->SetStringProperty(properties,vr::Prop_SerialNumber_String,serial);p->SetUint64Property(properties,vr::Prop_CurrentUniverseId_Uint64,2);p->SetBoolProperty(properties,vr::Prop_DeviceIsWireless_Bool,true);p->SetBoolProperty(properties,vr::Prop_DeviceCanPowerOff_Bool,false);}
 virtual void frame()=0;
};
class Headset final:public Device,public vr::IVRDisplayComponent {
 vr::VRInputComponentHandle_t proximity=0;std::chrono::steady_clock::time_point lastBattery{};
 bool haveProximity=false,lastProximity=false,haveEyes=false;float lastRate=0,lastIpd=-1;vr::HmdMatrix34_t lastEyes[2]{};
public:
 vr::EVRInitError Activate(uint32_t id)override{
  haveProximity=haveEyes=false;lastRate=0;lastIpd=-1;lastBattery={};
  base(id,"STEARLIGHT-QUEST2-HMD","Quest 2");auto* p=vr::VRProperties();bool camera=access("/run/stearlight/quest/passthrough.supported",F_OK)==0;p->SetBoolProperty(properties,vr::Prop_Hmd_SupportsRoomViewDirect_Bool,camera);p->SetBoolProperty(properties,vr::Prop_HasCamera_Bool,camera);p->SetInt32Property(properties,vr::Prop_NumCameras_Int32,camera?1:0);p->SetStringProperty(properties,vr::Prop_RenderModelName_String,"generic_hmd");p->SetStringProperty(properties,vr::Prop_ResourceRoot_String,"stearlight-quest");p->SetStringProperty(properties,vr::Prop_AdditionalDeviceSettingsPath_String,"{stearlight-quest}/frame_hmd_additional.vrsettings");p->SetStringProperty(properties,vr::Prop_RegisteredDeviceType_String,"stearlight-quest/quest2");p->SetFloatProperty(properties,vr::Prop_DisplayFrequency_Float,state.refresh_hz);p->SetFloatProperty(properties,vr::Prop_SecondsFromVsyncToPhotons_Float,0);p->SetBoolProperty(properties,vr::Prop_IsOnDesktop_Bool,true);p->SetBoolProperty(properties,vr::Prop_HasDisplayComponent_Bool,true);p->SetBoolProperty(properties,vr::Prop_HasDriverDirectModeComponent_Bool,false);p->SetBoolProperty(properties,vr::Prop_HasVirtualDisplayComponent_Bool,false);p->SetBoolProperty(properties,vr::Prop_ContainsProximitySensor_Bool,true);p->SetInt32Property(properties,vr::Prop_ExpectedControllerCount_Int32,2);vr::VRDriverInput()->CreateBooleanComponent(properties,"/proximity",&proximity);frame();return vr::VRInitError_None;
 }
 void* GetComponent(const char* version)override{return version&&!strcmp(version,vr::IVRDisplayComponent_Version)?static_cast<vr::IVRDisplayComponent*>(this):nullptr;}
 void GetWindowBounds(int32_t* x,int32_t* y,uint32_t* w,uint32_t* h)override{*x=*y=0;*w=state.width*2;*h=state.height;}
 bool IsDisplayOnDesktop()override{return true;}bool IsDisplayRealDisplay()override{return false;}
 void GetRecommendedRenderTargetSize(uint32_t* w,uint32_t* h)override{*w=state.width;*h=state.height;}
 void GetEyeOutputViewport(vr::EVREye eye,uint32_t* x,uint32_t* y,uint32_t* w,uint32_t* h)override{*x=eye==vr::Eye_Left?0:state.width;*y=0;*w=state.width;*h=state.height;}
 void GetProjectionRaw(vr::EVREye eye,float* left,float* right,float* top,float* bottom)override{int i=eye==vr::Eye_Left?0:1;*left=std::tan(state.fov[i][0]);*right=std::tan(state.fov[i][1]);*top=-std::tan(state.fov[i][2]);*bottom=-std::tan(state.fov[i][3]);}
 vr::DistortionCoordinates_t ComputeDistortion(vr::EVREye,float u,float v)override{vr::DistortionCoordinates_t result{};result.rfRed[0]=result.rfGreen[0]=result.rfBlue[0]=u;result.rfRed[1]=result.rfGreen[1]=result.rfBlue[1]=v;return result;}
 bool ComputeInverseDistortion(vr::HmdVector2_t* out,vr::EVREye,uint32_t,float u,float v)override{if(!out)return false;out->v[0]=u;out->v[1]=v;return true;}
 vr::DriverPose_t GetPose()override{auto p=trackedPose(state.headset);p.deviceIsConnected=true;return p;}
 void frame()override{
  if(index==vr::k_unTrackedDeviceIndexInvalid)return;
  bool focused=state.session_focused!=0;
  if(!haveProximity||focused!=lastProximity){vr::VRDriverInput()->UpdateBooleanComponent(proximity,focused,0);lastProximity=focused;haveProximity=true;}
  vr::VRServerDriverHost()->TrackedDevicePoseUpdated(index,GetPose(),sizeof(vr::DriverPose_t));
  if((state.headset.flags&3)==3){
   vr::HmdMatrix34_t eyes[]={eyeToHead(0),eyeToHead(1)};bool changed=!haveEyes;
   for(int eye=0;eye<2;eye++)for(int row=0;row<3;row++)for(int column=0;column<4;column++)changed=changed||std::fabs(eyes[eye].m[row][column]-lastEyes[eye].m[row][column])>1e-5f;
   if(changed){vr::VRServerDriverHost()->SetDisplayEyeToHead(index,eyes[0],eyes[1]);memcpy(lastEyes,eyes,sizeof(eyes));haveEyes=true;}
   float ipd=std::fabs(eyes[1].m[0][3]-eyes[0].m[0][3]);
   if(std::fabs(ipd-lastIpd)>1e-5f){vr::VRProperties()->SetFloatProperty(properties,vr::Prop_UserIpdMeters_Float,ipd);lastIpd=ipd;}
  }
  if(state.refresh_hz!=lastRate){vr::VRProperties()->SetFloatProperty(properties,vr::Prop_DisplayFrequency_Float,state.refresh_hz);lastRate=state.refresh_hz;}
  auto now=std::chrono::steady_clock::now();if(now-lastBattery<std::chrono::seconds(1))return;lastBattery=now;std::ifstream file("/run/stearlight/quest/device.json");std::string json((std::istreambuf_iterator<char>(file)),{});auto key=json.find("\"battery_percent\":");if(key!=std::string::npos){int value=std::atoi(json.c_str()+key+18);vr::VRProperties()->SetBoolProperty(properties,vr::Prop_DeviceProvidesBatteryStatus_Bool,value>=0);if(value>=0)vr::VRProperties()->SetFloatProperty(properties,vr::Prop_DeviceBatteryPercentage_Float,value/100.f);}key=json.find("\"charging\":");if(key!=std::string::npos)vr::VRProperties()->SetBoolProperty(properties,vr::Prop_DeviceIsCharging_Bool,json.compare(key+11,4,"true")==0);
 }
};
class Controller final:public Device {
 int hand;std::array<vr::VRInputComponentHandle_t,7> clicks{};vr::VRInputComponentHandle_t trigger=0,squeeze=0,x=0,y=0,touch=0,haptic=0;
 bool haveInputs=false;std::array<bool,8> previousClicks{};std::array<float,4> previousScalars{};
public:
 explicit Controller(int value):hand(value){}
 const char* serial(){return hand?"STEARLIGHT-QUEST2-RIGHT":"STEARLIGHT-QUEST2-LEFT";}
 bool acceptsHaptic(const vr::VREvent_HapticVibration_t& event)const{return haptic&&event.componentHandle==haptic&&event.containerHandle==properties;}
 vr::EVRInitError Activate(uint32_t id)override{
  haveInputs=false;
  base(id,serial(),hand?"Oculus Quest2 (Right Controller)":"Oculus Quest2 (Left Controller)");auto* p=vr::VRProperties();p->SetStringProperty(properties,vr::Prop_RenderModelName_String,hand?"oculus_quest2_controller_right":"oculus_quest2_controller_left");p->SetStringProperty(properties,vr::Prop_ResourceRoot_String,"frame_controller");p->SetStringProperty(properties,vr::Prop_InputProfilePath_String,"{frame_controller}/input/frame_controller_profile.json");p->SetStringProperty(properties,vr::Prop_ControllerType_String,"frame_controller");p->SetInt32Property(properties,vr::Prop_ControllerRoleHint_Int32,hand?vr::TrackedControllerRole_RightHand:vr::TrackedControllerRole_LeftHand);p->SetBoolProperty(properties,vr::Prop_DeviceProvidesBatteryStatus_Bool,false);
  const char* paths[]={"/input/a/click","/input/b/click","/input/x/click","/input/y/click","/input/thumbstick/click","/input/system/click","/input/trigger/click"};for(int i=0;i<7;i++)vr::VRDriverInput()->CreateBooleanComponent(properties,paths[i],&clicks[i]);vr::VRDriverInput()->CreateBooleanComponent(properties,"/input/trigger/touch",&touch);vr::VRDriverInput()->CreateScalarComponent(properties,"/input/trigger/value",&trigger,vr::VRScalarType_Absolute,vr::VRScalarUnits_NormalizedOneSided);vr::VRDriverInput()->CreateScalarComponent(properties,"/input/grip/value",&squeeze,vr::VRScalarType_Absolute,vr::VRScalarUnits_NormalizedOneSided);vr::VRDriverInput()->CreateScalarComponent(properties,"/input/thumbstick/x",&x,vr::VRScalarType_Absolute,vr::VRScalarUnits_NormalizedTwoSided);vr::VRDriverInput()->CreateScalarComponent(properties,"/input/thumbstick/y",&y,vr::VRScalarType_Absolute,vr::VRScalarUnits_NormalizedTwoSided);vr::VRDriverInput()->CreateHapticComponent(properties,"/output/haptic",&haptic);return vr::VRInitError_None;
 }
 vr::DriverPose_t GetPose()override{return trackedPose(quest::controllerModelPose(state.grip[hand],hand));}
 void frame()override{
  if(index==vr::k_unTrackedDeviceIndexInvalid)return;
  vr::VRServerDriverHost()->TrackedDevicePoseUpdated(index,GetPose(),sizeof(vr::DriverPose_t));
  std::array<bool,8> buttons{};for(int i=0;i<6;i++)buttons[i]=(state.buttons[hand]&(1u<<i))!=0;
  buttons[6]=state.trigger[hand]>.75f;buttons[7]=state.touches[hand]!=0;
  for(int i=0;i<8;i++)if(!haveInputs||buttons[i]!=previousClicks[i])vr::VRDriverInput()->UpdateBooleanComponent(i<7?clicks[i]:touch,buttons[i],0);
  std::array<float,4> scalars={state.trigger[hand],state.squeeze[hand],state.thumbstick[hand][0],state.thumbstick[hand][1]};
  vr::VRInputComponentHandle_t handles[]={trigger,squeeze,x,y};
  for(int i=0;i<4;i++)if(!haveInputs||scalars[i]!=previousScalars[i])vr::VRDriverInput()->UpdateScalarComponent(handles[i],scalars[i],0);
  previousClicks=buttons;previousScalars=scalars;haveInputs=true;
 }
};
class Provider final:public vr::IServerTrackedDeviceProvider {
 Headset head;Controller left{0},right{1};
 int hapticSocket=-1;
 uint32_t measuredSequence=0,measuredCalls=0,measuredSamples=0;
 bool havePublished=false;uint32_t publishedSequence=0;
 std::chrono::steady_clock::time_point measuredSince=std::chrono::steady_clock::now();
public:
 vr::EVRInitError Init(vr::IVRDriverContext* context)override{VR_INIT_SERVER_DRIVER_CONTEXT(context);int fd=open("/run/stearlight/quest/tracking",O_RDONLY|O_CLOEXEC);if(fd<0)return vr::VRInitError_Driver_Failed;struct stat info{};if(fstat(fd,&info)||info.st_size!=sizeof(QuestTracking)){close(fd);return vr::VRInitError_Driver_Failed;}shared=static_cast<QuestTracking*>(mmap(nullptr,sizeof(QuestTracking),PROT_READ,MAP_SHARED,fd,0));close(fd);if(shared==MAP_FAILED){shared=nullptr;return vr::VRInitError_Driver_Failed;}if(!readTracking()||!state.width||!state.height)return vr::VRInitError_Driver_Failed;if(!vr::VRServerDriverHost()->TrackedDeviceAdded("STEARLIGHT-QUEST2-HMD",vr::TrackedDeviceClass_HMD,&head)||!vr::VRServerDriverHost()->TrackedDeviceAdded(left.serial(),vr::TrackedDeviceClass_Controller,&left)||!vr::VRServerDriverHost()->TrackedDeviceAdded(right.serial(),vr::TrackedDeviceClass_Controller,&right))return vr::VRInitError_Driver_Failed;vr::VRDriverLog()->Log("stearlight-quest: real OpenXR headset/controllers registered");return vr::VRInitError_None;}
 void Cleanup()override{head.Deactivate();left.Deactivate();right.Deactivate();if(hapticSocket>=0)close(hapticSocket);hapticSocket=-1;if(shared)munmap(const_cast<QuestTracking*>(shared),sizeof(QuestTracking));shared=nullptr;VR_CLEANUP_SERVER_DRIVER_CONTEXT();}
 const char* const* GetInterfaceVersions()override{return vr::k_InterfaceVersions;}
 void RunFrame()override{
  ++measuredCalls;
  if(readTracking()){
   if(state.sequence!=measuredSequence){measuredSequence=state.sequence;++measuredSamples;}
   // OpenXR writes at the headset rate. Re-reading a sample must not repeat
   // SteamVR property/input work, but haptics below are polled on every call.
   if(!havePublished||publishedSequence!=state.sequence){head.frame();left.frame();right.frame();publishedSequence=state.sequence;havePublished=true;}
  }
  auto measuredNow=std::chrono::steady_clock::now();double measuredSeconds=std::chrono::duration<double>(measuredNow-measuredSince).count();
  if(measuredSeconds>=10){char message[160];snprintf(message,sizeof(message),"Quest tracking: %.1f calls/s, %.1f new samples/s",measuredCalls/measuredSeconds,measuredSamples/measuredSeconds);vr::VRDriverLog()->Log(message);measuredCalls=measuredSamples=0;measuredSince=measuredNow;}
  vr::VREvent_t event;
  while(vr::VRServerDriverHost()->PollNextEvent(&event,sizeof(event))){
   if(event.eventType!=vr::VREvent_Input_HapticVibration)continue;
   const auto& vibration=event.data.hapticVibration;
   int hand=left.acceptsHaptic(vibration)?0:right.acceptsHaptic(vibration)?1:-1;
   if(hand<0)continue;
   if(hapticSocket<0)hapticSocket=socket(AF_UNIX,SOCK_DGRAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);
   if(hapticSocket<0)continue;
   sockaddr_un address{};address.sun_family=AF_UNIX;
   strcpy(address.sun_path,"/run/stearlight/quest/haptics.sock");
   QuestHaptic packet{QUEST_BRIDGE_MAGIC,static_cast<uint32_t>(hand),vibration.fDurationSeconds,vibration.fFrequency,vibration.fAmplitude};
   sendto(hapticSocket,&packet,sizeof(packet),MSG_NOSIGNAL,reinterpret_cast<sockaddr*>(&address),sizeof(address));
  }
 }
 bool ShouldBlockStandbyMode()override{return false;}void EnterStandby()override{}void LeaveStandby()override{}
};
Provider provider;
}
extern "C" __attribute__((visibility("default"))) void* HmdDriverFactory(const char* name,int* result){if(name&&!strcmp(name,vr::IServerTrackedDeviceProvider_Version))return &provider;if(result)*result=vr::VRInitError_Init_InterfaceNotFound;return nullptr;}
