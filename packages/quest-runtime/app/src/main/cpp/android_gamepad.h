#pragma once
#include "external_gamepad.h"
#include <android/input.h>
#include <android/keycodes.h>
#include <jni.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <chrono>
#include <cstdio>
#include <string>
#include <stdexcept>
#include <android/log.h>

class AndroidGamepad {
 JNIEnv* env=nullptr;jclass deviceClass=nullptr;
 jmethodID ids=nullptr,get=nullptr,external=nullptr,sources=nullptr,vendor=nullptr,product=nullptr,name=nullptr;
 jmethodID motionRange=nullptr,rangeFlat=nullptr;
 int stickAxes[4]={AMOTION_EVENT_AXIS_X,AMOTION_EVENT_AXIS_Y,AMOTION_EVENT_AXIS_Z,AMOTION_EVENT_AXIS_RZ};float flats[4]{};
 QuestExternalGamepad* shared=nullptr;QuestExternalGamepad state{};
 int selected=-1;bool focused=false;uint32_t keys=0,hat=0;bool triggers[2]{};
 uint64_t nextInventory=0;
 static uint64_t now(){return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
 void release(){state.input=QuestGamepadState{};keys=hat=0;triggers[0]=triggers[1]=false;}
 void inventory(){
  auto devices=static_cast<jintArray>(env->CallStaticObjectMethod(deviceClass,ids));
  if(env->ExceptionCheck()||!devices){env->ExceptionClear();selected=-1;state.connected=0;release();return;}
  jsize count=env->GetArrayLength(devices);auto list=env->GetIntArrayElements(devices,nullptr);int found=-1;jobject chosen=nullptr;
  for(int i=0;i<count;i++){
   jobject device=env->CallStaticObjectMethod(deviceClass,get,list[i]);if(!device)continue;
   int source=env->CallIntMethod(device,sources);
   bool pad=env->CallBooleanMethod(device,external)&&((source&AINPUT_SOURCE_GAMEPAD)==AINPUT_SOURCE_GAMEPAD||(source&AINPUT_SOURCE_JOYSTICK)==AINPUT_SOURCE_JOYSTICK);
   if(pad&&(found<0||list[i]==selected)){if(chosen)env->DeleteLocalRef(chosen);chosen=device;found=list[i];if(found==selected)break;}
   else env->DeleteLocalRef(device);
  }
  env->ReleaseIntArrayElements(devices,list,JNI_ABORT);env->DeleteLocalRef(devices);
  if(found!=selected){release();selected=found;state.connected=found>=0;
   if(chosen){state.vendor=env->CallIntMethod(chosen,vendor);state.product=env->CallIntMethod(chosen,product);
    for(int i=0;i<4;i++){
     auto range=env->CallObjectMethod(chosen,motionRange,stickAxes[i],AINPUT_SOURCE_JOYSTICK);
     flats[i]=range?env->CallFloatMethod(range,rangeFlat):0;
     if(!std::isfinite(flats[i])||flats[i]<0||flats[i]>1)flats[i]=0;
     if(range)env->DeleteLocalRef(range);
    }
    auto label=static_cast<jstring>(env->CallObjectMethod(chosen,name));const char* text=label?env->GetStringUTFChars(label,nullptr):nullptr;
    std::snprintf(state.name,sizeof(state.name),"%s",text?text:"Android gamepad");
    if(text)env->ReleaseStringUTFChars(label,text);
    if(label)env->DeleteLocalRef(label);
   }
   __android_log_print(ANDROID_LOG_INFO,"StearlightGamepad","Physical gamepad %s: %s",found>=0?"connected":"disconnected",state.name);
  }
  if(chosen)env->DeleteLocalRef(chosen);
  if(env->ExceptionCheck()){env->ExceptionClear();selected=-1;state.connected=0;release();}
 }
public:
 void initialize(JNIEnv* value,const std::string& directory){
  env=value;deviceClass=env->FindClass("android/view/InputDevice");
  ids=env->GetStaticMethodID(deviceClass,"getDeviceIds","()[I");get=env->GetStaticMethodID(deviceClass,"getDevice","(I)Landroid/view/InputDevice;");
  external=env->GetMethodID(deviceClass,"isExternal","()Z");sources=env->GetMethodID(deviceClass,"getSources","()I");
  vendor=env->GetMethodID(deviceClass,"getVendorId","()I");product=env->GetMethodID(deviceClass,"getProductId","()I");name=env->GetMethodID(deviceClass,"getName","()Ljava/lang/String;");
  motionRange=env->GetMethodID(deviceClass,"getMotionRange","(II)Landroid/view/InputDevice$MotionRange;");
  jclass range=env->FindClass("android/view/InputDevice$MotionRange");rangeFlat=env->GetMethodID(range,"getFlat","()F");env->DeleteLocalRef(range);
  if(env->ExceptionCheck()||!ids||!get||!external||!sources||!vendor||!product||!name||!motionRange||!rangeFlat){env->ExceptionClear();throw std::runtime_error("Android gamepad device API");}
  int fd=open((directory+"/external-gamepad").c_str(),O_CREAT|O_RDWR|O_CLOEXEC,0600);
  if(fd<0)throw std::runtime_error("external gamepad file");
  if(ftruncate(fd,sizeof(QuestExternalGamepad))){close(fd);throw std::runtime_error("external gamepad size");}
  shared=static_cast<QuestExternalGamepad*>(mmap(nullptr,sizeof(*shared),PROT_READ|PROT_WRITE,MAP_SHARED,fd,0));close(fd);
  if(shared==MAP_FAILED){shared=nullptr;throw std::runtime_error("external gamepad mapping");}
  *shared=QuestExternalGamepad{};tick(false);
 }
 int handle(AInputEvent* event){
  if(!shared)return 0;
  if(AInputEvent_getDeviceId(event)!=selected)return 0;
  int type=AInputEvent_getType(event);
  if(type==AINPUT_EVENT_TYPE_KEY){
   int key=AKeyEvent_getKeyCode(event),button=-1;
   switch(key){
    case AKEYCODE_BUTTON_A:button=0;break;case AKEYCODE_BUTTON_B:button=1;break;
    case AKEYCODE_BUTTON_X:button=2;break;case AKEYCODE_BUTTON_Y:button=3;break;
    case AKEYCODE_BUTTON_SELECT:button=4;break;case AKEYCODE_BUTTON_MODE:button=5;break;
    case AKEYCODE_BUTTON_START:button=6;break;case AKEYCODE_BUTTON_THUMBL:button=7;break;
    case AKEYCODE_BUTTON_THUMBR:button=8;break;case AKEYCODE_BUTTON_L1:button=9;break;case AKEYCODE_BUTTON_R1:button=10;break;
    case AKEYCODE_DPAD_UP:button=11;break;case AKEYCODE_DPAD_DOWN:button=12;break;
    case AKEYCODE_DPAD_LEFT:button=13;break;case AKEYCODE_DPAD_RIGHT:button=14;break;
    case AKEYCODE_BUTTON_L2:case AKEYCODE_BUTTON_R2:{if(focused){int i=key==AKEYCODE_BUTTON_R2;triggers[i]=AKeyEvent_getAction(event)==AKEY_EVENT_ACTION_DOWN;state.input.axes[4+i]=triggers[i]?1:-1;}return 1;}
    default:return 0; // Preserve Horizon's Home/Back/system shortcuts.
   }
   if(focused){if(AKeyEvent_getAction(event)==AKEY_EVENT_ACTION_DOWN)keys|=1u<<button;else keys&=~(1u<<button);}
   state.input.buttons=keys|hat;return 1;
  }
  if(type!=AINPUT_EVENT_TYPE_MOTION||(AInputEvent_getSource(event)&AINPUT_SOURCE_JOYSTICK)!=AINPUT_SOURCE_JOYSTICK)return 0;
  if(!focused)return 1;
  if((AMotionEvent_getAction(event)&AMOTION_EVENT_ACTION_MASK)==AMOTION_EVENT_ACTION_CANCEL){release();return 1;}
  auto axis=[&](int code){return AMotionEvent_getAxisValue(event,code,0);};
  for(int i=0;i<4;i++)state.input.axes[i]=quest::gamepadAxis(axis(stickAxes[i]),flats[i]);
  for(int i=0;i<2;i++){float value=std::max(axis(i?AMOTION_EVENT_AXIS_RTRIGGER:AMOTION_EVENT_AXIS_LTRIGGER),axis(i?AMOTION_EVENT_AXIS_GAS:AMOTION_EVENT_AXIS_BRAKE));if(!std::isfinite(value))value=0;state.input.axes[4+i]=triggers[i]?1:std::clamp(value,0.f,1.f)*2-1;}
  float x=axis(AMOTION_EVENT_AXIS_HAT_X),y=axis(AMOTION_EVENT_AXIS_HAT_Y);
  hat=(y<-.5f?1u<<11:0)|(y>.5f?1u<<12:0)|(x<-.5f?1u<<13:0)|(x>.5f?1u<<14:0);
  state.input.buttons=keys|hat;return 1;
 }
 void tick(bool active){if(!shared)return;auto ns=now();if(focused&&!active)release();focused=active;
  if(ns>=nextInventory){inventory();nextInventory=ns+500000000;}
  state.input.monotonic_ns=ns;quest::publishExternalGamepad(shared,state);
 }
 void shutdown(){if(shared){release();state.connected=0;quest::publishExternalGamepad(shared,state);munmap(shared,sizeof(*shared));shared=nullptr;}if(deviceClass&&env)env->DeleteLocalRef(deviceClass);deviceClass=nullptr;}
};
