#pragma once
#include <openxr/openxr.h>
#include <android/log.h>
#include <unistd.h>
#include <cstdio>
#include <string>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <sstream>

// Quest's compositor owns camera images; Linux receives no raw camera frames.
class Passthrough {
 XrPassthroughFB feature=XR_NULL_HANDLE;
 PFN_xrPassthroughStartFB start=nullptr;
 PFN_xrPassthroughPauseFB pause=nullptr;
 PFN_xrPassthroughLayerResumeFB resume=nullptr;
 PFN_xrPassthroughLayerPauseFB pauseLayer=nullptr;
 PFN_xrDestroyPassthroughLayerFB destroyLayer=nullptr;
 PFN_xrDestroyPassthroughFB destroy=nullptr;
 PFN_xrPassthroughLayerSetStyleFB setStyle=nullptr;
 std::chrono::steady_clock::time_point nextStyle;
 std::string lastStyle;
 std::string directory;
 bool previousRequest=false;
 template<class T> bool load(XrInstance instance,const char* name,T& output){
  PFN_xrVoidFunction function=nullptr;
  if(XR_FAILED(xrGetInstanceProcAddr(instance,name,&function)))return false;
  output=reinterpret_cast<T>(function);return output!=nullptr;
 }
 void status(const char* value){
  if(FILE* file=fopen((directory+"/passthrough.status").c_str(),"w")){fputs(value,file);fclose(file);}
  __android_log_print(ANDROID_LOG_INFO,"StearlightXR","Passthrough %s",value);
 }
 void updateStyle(){
  auto now=std::chrono::steady_clock::now();if(now<nextStyle)return;nextStyle=now+std::chrono::milliseconds(250);
  std::ifstream file(directory+"/passthrough-style.tsv");std::string text((std::istreambuf_iterator<char>(file)),{});
  if(text.empty()||text==lastStyle)return;
  int version=0,mode=0;float hue=.67f,r=0,g=1,b=1,a=.6f,strength=.78f;
  std::istringstream input(text);if(!(input>>version>>mode>>hue>>r>>g>>b>>a>>strength)||version!=1)return;
  hue=std::isfinite(hue)?hue-std::floor(hue):.67f;
  auto clamp=[](float v){return std::isfinite(v)?std::clamp(v,0.f,1.f):0.f;};
  // Quest 2 has monochrome cameras. Recolor their luminance through the
  // compositor's LUT; camera images remain inside Horizon's compositor.
  float h=hue*6, x=1-std::fabs(std::fmod(h,2.f)-1);std::array<float,3> tint;
  if(h<1)tint={1,x,0};else if(h<2)tint={x,1,0};else if(h<3)tint={0,1,x};
  else if(h<4)tint={0,x,1};else if(h<5)tint={x,0,1};else tint={1,0,x};
  XrPassthroughColorMapMonoToRgbaFB colors{XR_TYPE_PASSTHROUGH_COLOR_MAP_MONO_TO_RGBA_FB};
  for(unsigned i=0;i<XR_PASSTHROUGH_COLOR_MAP_MONO_SIZE_FB;i++){
   float level=float(i)/(XR_PASSTHROUGH_COLOR_MAP_MONO_SIZE_FB-1);
   colors.textureColorMap[i]={level*tint[0],level*tint[1],level*tint[2],1};
  }
  XrPassthroughStyleFB style{XR_TYPE_PASSTHROUGH_STYLE_FB};style.next=&colors;
  style.textureOpacityFactor=mode==1?0.f:mode==3?.5f:1.f;
  if(mode==0||mode==1||mode==2)style.edgeColor={clamp(r),clamp(g),clamp(b),clamp(a)*clamp(strength)};
  XrResult result=setStyle(layer,&style);
  __android_log_print(ANDROID_LOG_INFO,"StearlightXR","Passthrough style %d hue %.2f result %d",mode,hue,result);
  if(XR_SUCCEEDED(result))lastStyle=text;
 }
public:
 XrPassthroughLayerFB layer=XR_NULL_HANDLE;
 bool active=false;
 void initialize(XrInstance instance,XrSystemId system,XrSession session,const std::string& stateDirectory,bool enabled){
  directory=stateDirectory;unlink((directory+"/passthrough.supported").c_str());
  if(!enabled){status("unavailable");return;}
  XrSystemPassthroughPropertiesFB support{XR_TYPE_SYSTEM_PASSTHROUGH_PROPERTIES_FB};
  XrSystemProperties properties{XR_TYPE_SYSTEM_PROPERTIES};properties.next=&support;
  if(XR_FAILED(xrGetSystemProperties(instance,system,&properties))||!support.supportsPassthrough){status("unavailable");return;}
  PFN_xrCreatePassthroughFB create=nullptr;PFN_xrCreatePassthroughLayerFB createLayer=nullptr;
  if(!load(instance,"xrCreatePassthroughFB",create)||!load(instance,"xrCreatePassthroughLayerFB",createLayer)||
     !load(instance,"xrPassthroughStartFB",start)||!load(instance,"xrPassthroughPauseFB",pause)||
     !load(instance,"xrPassthroughLayerResumeFB",resume)||!load(instance,"xrPassthroughLayerPauseFB",pauseLayer)||
     !load(instance,"xrDestroyPassthroughLayerFB",destroyLayer)||!load(instance,"xrDestroyPassthroughFB",destroy)||
     !load(instance,"xrPassthroughLayerSetStyleFB",setStyle)){status("unavailable");return;}
  XrPassthroughCreateInfoFB info{XR_TYPE_PASSTHROUGH_CREATE_INFO_FB};
  if(XR_FAILED(create(session,&info,&feature))){status("unavailable");return;}
  XrPassthroughLayerCreateInfoFB layerInfo{XR_TYPE_PASSTHROUGH_LAYER_CREATE_INFO_FB};
  layerInfo.passthrough=feature;layerInfo.purpose=XR_PASSTHROUGH_LAYER_PURPOSE_RECONSTRUCTION_FB;
  if(XR_FAILED(createLayer(session,&layerInfo,&layer))){destroy(feature);feature=XR_NULL_HANDLE;status("unavailable");return;}
  XrPassthroughStyleFB style{XR_TYPE_PASSTHROUGH_STYLE_FB};style.textureOpacityFactor=1;
  if(XR_FAILED(setStyle(layer,&style))){shutdown();status("unavailable");return;}
  if(FILE* file=fopen((directory+"/passthrough.supported").c_str(),"w"))fclose(file);
  status("inactive");
 }
 void update(){
  if(layer)updateStyle();
  bool requested=access((directory+"/passthrough.enabled").c_str(),F_OK)==0;
  if(!layer||requested==previousRequest)return;previousRequest=requested;
  if(requested){
   XrResult result=start(feature);
   if(XR_SUCCEEDED(result))result=resume(layer);
   if(XR_FAILED(result)){pause(feature);status("start failed");return;}
   active=true;lastStyle.clear();nextStyle={};updateStyle();status("active");
  }else suspend();
 }
 void suspend(){
  if(active){pauseLayer(layer);pause(feature);active=false;status("inactive");}
  previousRequest=false;
 }
 void shutdown(){
  suspend();if(layer&&destroyLayer)destroyLayer(layer);layer=XR_NULL_HANDLE;
  if(feature&&destroy)destroy(feature);feature=XR_NULL_HANDLE;
  if(!directory.empty())unlink((directory+"/passthrough.supported").c_str());
 }
};
