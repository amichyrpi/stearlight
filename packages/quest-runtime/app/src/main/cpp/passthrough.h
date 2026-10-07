#pragma once
#include <openxr/openxr.h>
#include <android/log.h>
#include <unistd.h>
#include <cstdio>
#include <string>

// Quest's compositor owns camera images; Linux receives no raw camera frames.
class Passthrough {
 XrPassthroughFB feature=XR_NULL_HANDLE;
 PFN_xrPassthroughStartFB start=nullptr;
 PFN_xrPassthroughPauseFB pause=nullptr;
 PFN_xrPassthroughLayerResumeFB resume=nullptr;
 PFN_xrPassthroughLayerPauseFB pauseLayer=nullptr;
 PFN_xrDestroyPassthroughLayerFB destroyLayer=nullptr;
 PFN_xrDestroyPassthroughFB destroy=nullptr;
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
public:
 XrPassthroughLayerFB layer=XR_NULL_HANDLE;
 bool active=false;
 void initialize(XrInstance instance,XrSystemId system,XrSession session,const std::string& stateDirectory,bool enabled){
  directory=stateDirectory;unlink((directory+"/passthrough.supported").c_str());
  if(!enabled){status("unavailable");return;}
  XrSystemPassthroughPropertiesFB support{XR_TYPE_SYSTEM_PASSTHROUGH_PROPERTIES_FB};
  XrSystemProperties properties{XR_TYPE_SYSTEM_PROPERTIES};properties.next=&support;
  if(XR_FAILED(xrGetSystemProperties(instance,system,&properties))||!support.supportsPassthrough){status("unavailable");return;}
  PFN_xrCreatePassthroughFB create=nullptr;PFN_xrCreatePassthroughLayerFB createLayer=nullptr;PFN_xrPassthroughLayerSetStyleFB setStyle=nullptr;
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
  bool requested=access((directory+"/passthrough.enabled").c_str(),F_OK)==0;
  if(!layer||requested==previousRequest)return;previousRequest=requested;
  if(requested){
   XrResult result=start(feature);
   if(XR_SUCCEEDED(result))result=resume(layer);
   if(XR_FAILED(result)){pause(feature);status("start failed");return;}
   active=true;status("active");
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
