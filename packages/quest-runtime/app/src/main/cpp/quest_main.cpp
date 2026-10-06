#include <android_native_app_glue.h>
#include <android/log.h>
#include <android/bitmap.h>
#define XR_USE_TIMESPEC
#include <openxr/openxr.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <openxr/openxr_platform.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include "quest_bridge.h"
#include "gpu_bridge.h"

#define LOG(...) __android_log_print(ANDROID_LOG_INFO,"StearlightXR",__VA_ARGS__)
static void check(XrResult result,const char* name){if(XR_FAILED(result)){LOG("%s failed: %d",name,result);throw std::runtime_error(name);}}
#define XR(call) check((call),#call)
template<class T> static T function(XrInstance instance,const char* name){PFN_xrVoidFunction p=nullptr;XR(xrGetInstanceProcAddr(instance,name,&p));return reinterpret_cast<T>(p);}
static XrPosef identity(){XrPosef p{};p.orientation.w=1;return p;}
static QuestPose pose(XrPosef p,XrSpaceLocationFlags flags){return {{p.position.x,p.position.y,p.position.z},{p.orientation.x,p.orientation.y,p.orientation.z,p.orientation.w},static_cast<uint32_t>(flags)};}

struct Eye {
 XrSwapchain chain=XR_NULL_HANDLE;uint32_t width=0,height=0;
 std::vector<XrSwapchainImageOpenGLESKHR> images;
};
struct Runtime {
 android_app* app=nullptr;std::string directory;
 EGLDisplay display=EGL_NO_DISPLAY;EGLConfig config=nullptr;EGLContext context=EGL_NO_CONTEXT;EGLSurface surface=EGL_NO_SURFACE;
 XrInstance instance=XR_NULL_HANDLE;XrSystemId system=XR_NULL_SYSTEM_ID;XrSession session=XR_NULL_HANDLE;
 XrSpace stage=XR_NULL_HANDLE,head=XR_NULL_HANDLE,grip[2]{},aim[2]{};
 XrActionSet actions=XR_NULL_HANDLE;XrAction gripAction=XR_NULL_HANDLE,aimAction=XR_NULL_HANDLE,triggerAction=XR_NULL_HANDLE,squeezeAction=XR_NULL_HANDLE,stickAction=XR_NULL_HANDLE;
 XrAction buttonActions[6]{};XrAction touchAction=XR_NULL_HANDLE,hapticAction=XR_NULL_HANDLE,selectAction=XR_NULL_HANDLE;
 XrPath hands[2]{};Eye eyes[2],statusEye;XrTime statusUpdated=0;bool running=false,visible=false,focused=false,passthroughActive=false;
 QuestTracking* tracking=nullptr;GLuint framebuffer=0;GpuBridge gpu;
 XrView frameViews[2]={{XR_TYPE_VIEW},{XR_TYPE_VIEW}};bool haveFrameViews=false;
 PFN_xrConvertTimeToTimespecTimeKHR timeToMonotonic=nullptr;
 PFN_xrGetDisplayRefreshRateFB getRate=nullptr;PFN_xrRequestDisplayRefreshRateFB requestRate=nullptr;
 PFN_xrPerfSettingsSetPerformanceLevelEXT setPerformance=nullptr;
 int hapticSocket=-1;
 XrPassthroughFB passthrough=XR_NULL_HANDLE;XrPassthroughLayerFB passthroughLayer=XR_NULL_HANDLE;
 PFN_xrPassthroughStartFB startPassthrough=nullptr;PFN_xrPassthroughPauseFB pausePassthrough=nullptr;
 PFN_xrPassthroughLayerResumeFB resumeLayer=nullptr;PFN_xrPassthroughLayerPauseFB pauseLayer=nullptr;
 bool has(const std::vector<XrExtensionProperties>& e,const char* name){for(const auto& p:e)if(!strcmp(p.extensionName,name))return true;return false;}
 XrPath path(const char* s){XrPath p;XR(xrStringToPath(instance,s,&p));return p;}
 void egl(){
  display=eglGetDisplay(EGL_DEFAULT_DISPLAY);if(!eglInitialize(display,nullptr,nullptr))throw std::runtime_error("eglInitialize");
  const EGLint attributes[]={EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};EGLint count=0;
  if(!eglChooseConfig(display,attributes,&config,1,&count)||!count)throw std::runtime_error("eglChooseConfig");
  const EGLint contextAttributes[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE},surfaceAttributes[]={EGL_WIDTH,16,EGL_HEIGHT,16,EGL_NONE};
  context=eglCreateContext(display,config,EGL_NO_CONTEXT,contextAttributes);surface=eglCreatePbufferSurface(display,config,surfaceAttributes);
  if(!eglMakeCurrent(display,surface,surface,context))throw std::runtime_error("eglMakeCurrent");
  LOG("Android GLES renderer: %s",glGetString(GL_RENDERER));
 }
 void initialize(android_app* value){
  app=value;JNIEnv* env;app->activity->vm->AttachCurrentThread(&env,nullptr);
  jclass activityClass=env->GetObjectClass(app->activity->clazz);jmethodID getDirectory=env->GetMethodID(activityClass,"getStateDirectory","()Ljava/lang/String;");
  auto str=static_cast<jstring>(env->CallObjectMethod(app->activity->clazz,getDirectory));const char* chars=env->GetStringUTFChars(str,nullptr);directory=chars;env->ReleaseStringUTFChars(str,chars);env->DeleteLocalRef(str);env->DeleteLocalRef(activityClass);
  int fd=open((directory+"/tracking").c_str(),O_CREAT|O_RDWR|O_CLOEXEC,0600);if(fd<0||ftruncate(fd,sizeof(QuestTracking)))throw std::runtime_error("tracking file");
  tracking=static_cast<QuestTracking*>(mmap(nullptr,sizeof(QuestTracking),PROT_READ|PROT_WRITE,MAP_SHARED,fd,0));close(fd);if(tracking==MAP_FAILED)throw std::runtime_error("tracking mmap");
  unlink((directory+"/dashboard.ready").c_str());
  memset(tracking,0,sizeof(*tracking));tracking->magic=QUEST_BRIDGE_MAGIC;tracking->version=QUEST_BRIDGE_VERSION;for(float& b:tracking->battery)b=-1;
  auto loader=function<PFN_xrInitializeLoaderKHR>(XR_NULL_HANDLE,"xrInitializeLoaderKHR");XrLoaderInitInfoAndroidKHR loaderInfo{XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};loaderInfo.applicationVM=app->activity->vm;loaderInfo.applicationContext=app->activity->clazz;XR(loader(reinterpret_cast<XrLoaderInitInfoBaseHeaderKHR*>(&loaderInfo)));
  uint32_t n=0;XR(xrEnumerateInstanceExtensionProperties(nullptr,0,&n,nullptr));std::vector<XrExtensionProperties> extensions(n,{XR_TYPE_EXTENSION_PROPERTIES});XR(xrEnumerateInstanceExtensionProperties(nullptr,n,&n,extensions.data()));
  std::vector<const char*> enabled={XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME};
  for(const auto& extension:extensions)LOG("OpenXR extension %s",extension.extensionName);
  for(const char* name:{XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME,XR_FB_PASSTHROUGH_EXTENSION_NAME,XR_FB_RENDER_MODEL_EXTENSION_NAME,XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME,XR_KHR_CONVERT_TIMESPEC_TIME_EXTENSION_NAME,XR_FB_COLOR_SPACE_EXTENSION_NAME})if(has(extensions,name))enabled.push_back(name);
  XrInstanceCreateInfoAndroidKHR androidInfo{XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};androidInfo.applicationVM=app->activity->vm;androidInfo.applicationActivity=app->activity->clazz;
  XrInstanceCreateInfo create{XR_TYPE_INSTANCE_CREATE_INFO};create.next=&androidInfo;strcpy(create.applicationInfo.applicationName,"Stearlight OS");create.applicationInfo.applicationVersion=2;strcpy(create.applicationInfo.engineName,"Stearlight Quest bridge");create.applicationInfo.apiVersion=XR_CURRENT_API_VERSION;create.enabledExtensionCount=enabled.size();create.enabledExtensionNames=enabled.data();XR(xrCreateInstance(&create,&instance));
  XrInstanceProperties properties{XR_TYPE_INSTANCE_PROPERTIES};XR(xrGetInstanceProperties(instance,&properties));LOG("OpenXR runtime %s",properties.runtimeName);
  if(has(extensions,XR_KHR_CONVERT_TIMESPEC_TIME_EXTENSION_NAME))timeToMonotonic=function<PFN_xrConvertTimeToTimespecTimeKHR>(instance,"xrConvertTimeToTimespecTimeKHR");
  XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};systemInfo.formFactor=XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;XR(xrGetSystem(instance,&systemInfo,&system));
  egl();auto requirements=function<PFN_xrGetOpenGLESGraphicsRequirementsKHR>(instance,"xrGetOpenGLESGraphicsRequirementsKHR");XrGraphicsRequirementsOpenGLESKHR graphicsRequirements{XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR};XR(requirements(instance,system,&graphicsRequirements));
  XrGraphicsBindingOpenGLESAndroidKHR binding{XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR};binding.display=display;binding.config=config;binding.context=context;
  XrSessionCreateInfo sessionInfo{XR_TYPE_SESSION_CREATE_INFO};sessionInfo.next=&binding;sessionInfo.systemId=system;XR(xrCreateSession(instance,&sessionInfo,&session));
  if(has(extensions,XR_FB_COLOR_SPACE_EXTENSION_NAME)){
   auto enumerate=function<PFN_xrEnumerateColorSpacesFB>(instance,"xrEnumerateColorSpacesFB");auto setColor=function<PFN_xrSetColorSpaceFB>(instance,"xrSetColorSpaceFB");uint32_t count=0;XR(enumerate(session,0,&count,nullptr));std::vector<XrColorSpaceFB> spaces(count);XR(enumerate(session,count,&count,spaces.data()));if(std::find(spaces.begin(),spaces.end(),XR_COLOR_SPACE_REC709_FB)!=spaces.end()){XR(setColor(session,XR_COLOR_SPACE_REC709_FB));LOG("OpenXR color space REC709");}
  }
  if(has(extensions,XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME))setPerformance=function<PFN_xrPerfSettingsSetPerformanceLevelEXT>(instance,"xrPerfSettingsSetPerformanceLevelEXT");
  hapticSocket=socket(AF_UNIX,SOCK_DGRAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);
  sockaddr_un hapticAddress{};hapticAddress.sun_family=AF_UNIX;auto hapticPath=directory+"/haptics.sock";
  if(hapticSocket<0||hapticPath.size()>=sizeof(hapticAddress.sun_path))throw std::runtime_error("Quest haptic socket");
  strcpy(hapticAddress.sun_path,hapticPath.c_str());unlink(hapticPath.c_str());
  if(bind(hapticSocket,reinterpret_cast<sockaddr*>(&hapticAddress),sizeof(hapticAddress)))throw std::runtime_error("Quest haptic bind");
  chmod(hapticPath.c_str(),0600);
  uint32_t spacesCount=0;XR(xrEnumerateReferenceSpaces(session,0,&spacesCount,nullptr));std::vector<XrReferenceSpaceType> spaces(spacesCount);XR(xrEnumerateReferenceSpaces(session,spacesCount,&spacesCount,spaces.data()));
  XrReferenceSpaceCreateInfo reference{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};reference.poseInReferenceSpace=identity();reference.referenceSpaceType=std::find(spaces.begin(),spaces.end(),XR_REFERENCE_SPACE_TYPE_STAGE)!=spaces.end()?XR_REFERENCE_SPACE_TYPE_STAGE:XR_REFERENCE_SPACE_TYPE_LOCAL;XR(xrCreateReferenceSpace(session,&reference,&stage));reference.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_VIEW;XR(xrCreateReferenceSpace(session,&reference,&head));
  actionsSetup();
  uint32_t viewsCount=0;XR(xrEnumerateViewConfigurationViews(instance,system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,0,&viewsCount,nullptr));if(viewsCount!=2)throw std::runtime_error("Expected stereo view configuration");
  std::vector<XrViewConfigurationView> views(viewsCount,{XR_TYPE_VIEW_CONFIGURATION_VIEW});XR(xrEnumerateViewConfigurationViews(instance,system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,viewsCount,&viewsCount,views.data()));
  uint32_t formatCount;XR(xrEnumerateSwapchainFormats(session,0,&formatCount,nullptr));std::vector<int64_t> formats(formatCount);XR(xrEnumerateSwapchainFormats(session,formatCount,&formatCount,formats.data()));int64_t format=GL_SRGB8_ALPHA8;if(std::find(formats.begin(),formats.end(),format)==formats.end())format=GL_RGBA8;
  for(int i=0;i<2;i++){
   Eye& eye=eyes[i];eye.width=std::min(1832u,views[i].maxImageRectWidth);eye.height=std::min(1920u,views[i].maxImageRectHeight);
   LOG("Eye %d requested %ux%u; recommended %ux%u; maximum %ux%u",i,eye.width,eye.height,views[i].recommendedImageRectWidth,views[i].recommendedImageRectHeight,views[i].maxImageRectWidth,views[i].maxImageRectHeight);
   XrSwapchainCreateInfo swap{XR_TYPE_SWAPCHAIN_CREATE_INFO};swap.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_SAMPLED_BIT;swap.format=format;swap.sampleCount=1;swap.width=eye.width;swap.height=eye.height;swap.faceCount=1;swap.arraySize=1;swap.mipCount=1;XR(xrCreateSwapchain(session,&swap,&eye.chain));uint32_t count=0;XR(xrEnumerateSwapchainImages(eye.chain,0,&count,nullptr));eye.images.resize(count,{XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR});XR(xrEnumerateSwapchainImages(eye.chain,count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(eye.images.data())));
  }
  tracking->width=eyes[0].width;tracking->height=eyes[0].height;glGenFramebuffers(1,&framebuffer);
  XrSwapchainCreateInfo statusSwap{XR_TYPE_SWAPCHAIN_CREATE_INFO};statusSwap.usageFlags=XR_SWAPCHAIN_USAGE_SAMPLED_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;statusSwap.format=GL_SRGB8_ALPHA8;statusSwap.sampleCount=1;statusSwap.width=450;statusSwap.height=450;statusSwap.faceCount=statusSwap.arraySize=statusSwap.mipCount=1;XR(xrCreateSwapchain(session,&statusSwap,&statusEye.chain));uint32_t statusCount=0;XR(xrEnumerateSwapchainImages(statusEye.chain,0,&statusCount,nullptr));statusEye.images.resize(statusCount,{XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR});XR(xrEnumerateSwapchainImages(statusEye.chain,statusCount,&statusCount,reinterpret_cast<XrSwapchainImageBaseHeader*>(statusEye.images.data())));
  gpu.initialize(display,directory,eyes[0].width*2,eyes[0].height);
  if(has(extensions,XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME)){
   getRate=function<PFN_xrGetDisplayRefreshRateFB>(instance,"xrGetDisplayRefreshRateFB");requestRate=function<PFN_xrRequestDisplayRefreshRateFB>(instance,"xrRequestDisplayRefreshRateFB");auto enumerate=function<PFN_xrEnumerateDisplayRefreshRatesFB>(instance,"xrEnumerateDisplayRefreshRatesFB");uint32_t count=0;XR(enumerate(session,0,&count,nullptr));std::vector<float> rates(count);XR(enumerate(session,count,&count,rates.data()));for(float rate:rates)LOG("Supported display refresh %.0f Hz",rate);if(std::find(rates.begin(),rates.end(),120.f)!=rates.end())XR(requestRate(session,120));XR(getRate(session,&tracking->refresh_hz));LOG("Current display refresh %.0f Hz",tracking->refresh_hz);
  }
  if(has(extensions,XR_FB_PASSTHROUGH_EXTENSION_NAME)){
   auto createPassthrough=function<PFN_xrCreatePassthroughFB>(instance,"xrCreatePassthroughFB");XrPassthroughCreateInfoFB info{XR_TYPE_PASSTHROUGH_CREATE_INFO_FB};XrResult result=createPassthrough(session,&info,&passthrough);
   if(XR_SUCCEEDED(result)){auto createLayer=function<PFN_xrCreatePassthroughLayerFB>(instance,"xrCreatePassthroughLayerFB");XrPassthroughLayerCreateInfoFB layer{XR_TYPE_PASSTHROUGH_LAYER_CREATE_INFO_FB};layer.passthrough=passthrough;layer.purpose=XR_PASSTHROUGH_LAYER_PURPOSE_RECONSTRUCTION_FB;XR(createLayer(session,&layer,&passthroughLayer));startPassthrough=function<PFN_xrPassthroughStartFB>(instance,"xrPassthroughStartFB");pausePassthrough=function<PFN_xrPassthroughPauseFB>(instance,"xrPassthroughPauseFB");resumeLayer=function<PFN_xrPassthroughLayerResumeFB>(instance,"xrPassthroughLayerResumeFB");pauseLayer=function<PFN_xrPassthroughLayerPauseFB>(instance,"xrPassthroughLayerPauseFB");LOG("Runtime passthrough layer created");}else LOG("Passthrough unavailable: %d",result);
  }
  if(has(extensions,XR_FB_RENDER_MODEL_EXTENSION_NAME))saveModels();
  LOG("OPENXR_READY");
 }
 XrAction action(const char* name,XrActionType type,bool sub=true){XrActionCreateInfo create{XR_TYPE_ACTION_CREATE_INFO};strncpy(create.actionName,name,sizeof(create.actionName)-1);strncpy(create.localizedActionName,name,sizeof(create.localizedActionName)-1);create.actionType=type;if(sub){create.countSubactionPaths=2;create.subactionPaths=hands;}XrAction a;XR(xrCreateAction(actions,&create,&a));return a;}
 void actionsSetup(){
  hands[0]=path("/user/hand/left");hands[1]=path("/user/hand/right");XrActionSetCreateInfo create{XR_TYPE_ACTION_SET_CREATE_INFO};strcpy(create.actionSetName,"quest");strcpy(create.localizedActionSetName,"Quest controllers");XR(xrCreateActionSet(instance,&create,&actions));
  gripAction=action("grip_pose",XR_ACTION_TYPE_POSE_INPUT);aimAction=action("aim_pose",XR_ACTION_TYPE_POSE_INPUT);triggerAction=action("trigger",XR_ACTION_TYPE_FLOAT_INPUT);squeezeAction=action("squeeze",XR_ACTION_TYPE_FLOAT_INPUT);stickAction=action("thumbstick",XR_ACTION_TYPE_VECTOR2F_INPUT);touchAction=action("trigger_touch",XR_ACTION_TYPE_BOOLEAN_INPUT);hapticAction=action("haptic",XR_ACTION_TYPE_VIBRATION_OUTPUT);
  const char* names[]={"a","b","x","y","stick_click","menu"};for(int i=0;i<6;i++)buttonActions[i]=action(names[i],XR_ACTION_TYPE_BOOLEAN_INPUT);
  selectAction=action("select",XR_ACTION_TYPE_BOOLEAN_INPUT);
  std::vector<XrActionSuggestedBinding> simple;for(int hand=0;hand<2;hand++){std::string base=hand?"/user/hand/right":"/user/hand/left";for(auto binding:std::vector<std::pair<XrAction,const char*>>{{gripAction,"/input/grip/pose"},{aimAction,"/input/aim/pose"},{selectAction,"/input/select/click"},{hapticAction,"/output/haptic"}})simple.push_back({binding.first,path((base+binding.second).c_str())});}
  XrInteractionProfileSuggestedBinding fallback{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};fallback.interactionProfile=path("/interaction_profiles/khr/simple_controller");fallback.countSuggestedBindings=simple.size();fallback.suggestedBindings=simple.data();XR(xrSuggestInteractionProfileBindings(instance,&fallback));
  std::vector<XrActionSuggestedBinding> bindings;for(int hand=0;hand<2;hand++){
   std::string base=hand?"/user/hand/right":"/user/hand/left";auto bind=[&](XrAction a,const char* suffix){bindings.push_back({a,path((base+suffix).c_str())});};
   bind(gripAction,"/input/grip/pose");bind(aimAction,"/input/aim/pose");bind(triggerAction,"/input/trigger/value");bind(selectAction,"/input/trigger/value");bind(squeezeAction,"/input/squeeze/value");bind(stickAction,"/input/thumbstick");bind(buttonActions[4],"/input/thumbstick/click");bind(touchAction,"/input/trigger/touch");bind(hapticAction,"/output/haptic");
   if(hand){bind(buttonActions[0],"/input/a/click");bind(buttonActions[1],"/input/b/click");}else{bind(buttonActions[2],"/input/x/click");bind(buttonActions[3],"/input/y/click");bind(buttonActions[5],"/input/menu/click");}
  }
  XrInteractionProfileSuggestedBinding suggested{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};suggested.interactionProfile=path("/interaction_profiles/oculus/touch_controller");suggested.countSuggestedBindings=bindings.size();suggested.suggestedBindings=bindings.data();XR(xrSuggestInteractionProfileBindings(instance,&suggested));
  XrSessionActionSetsAttachInfo attach{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};attach.countActionSets=1;attach.actionSets=&actions;XR(xrAttachSessionActionSets(session,&attach));
  for(int i=0;i<2;i++){XrActionSpaceCreateInfo space{XR_TYPE_ACTION_SPACE_CREATE_INFO};space.poseInActionSpace=identity();space.subactionPath=hands[i];space.action=gripAction;XR(xrCreateActionSpace(session,&space,&grip[i]));space.action=aimAction;XR(xrCreateActionSpace(session,&space,&aim[i]));}
 }
 void saveModels(){
  auto enumerate=function<PFN_xrEnumerateRenderModelPathsFB>(instance,"xrEnumerateRenderModelPathsFB");auto properties=function<PFN_xrGetRenderModelPropertiesFB>(instance,"xrGetRenderModelPropertiesFB");auto load=function<PFN_xrLoadRenderModelFB>(instance,"xrLoadRenderModelFB");uint32_t count=0;XrResult result=enumerate(session,0,&count,nullptr);if(XR_FAILED(result))return;std::vector<XrRenderModelPathInfoFB> paths(count,{XR_TYPE_RENDER_MODEL_PATH_INFO_FB});XR(enumerate(session,count,&count,paths.data()));
  for(auto p:paths){char name[XR_MAX_PATH_LENGTH];uint32_t length;XR(xrPathToString(instance,p.path,sizeof(name),&length,name));XrRenderModelPropertiesFB props{XR_TYPE_RENDER_MODEL_PROPERTIES_FB};result=properties(session,p.path,&props);if(XR_FAILED(result)||props.modelKey==XR_NULL_RENDER_MODEL_KEY_FB)continue;XrRenderModelLoadInfoFB info{XR_TYPE_RENDER_MODEL_LOAD_INFO_FB};info.modelKey=props.modelKey;XrRenderModelBufferFB buffer{XR_TYPE_RENDER_MODEL_BUFFER_FB};result=load(session,&info,&buffer);if(XR_FAILED(result))continue;std::vector<uint8_t> bytes(buffer.bufferCountOutput);buffer.bufferCapacityInput=bytes.size();buffer.buffer=bytes.data();result=load(session,&info,&buffer);if(XR_FAILED(result))continue;
   std::string file=directory+"/model-"+(strstr(name,"left")?std::string("left"):strstr(name,"right")?std::string("right"):std::to_string(p.path))+".glb";FILE* output=fopen(file.c_str(),"wb");if(output){fwrite(bytes.data(),1,bytes.size(),output);fclose(output);LOG("Runtime render model %s saved: %zu bytes, model %s",name,bytes.size(),props.modelName);}
  }
 }
 bool events(){
  XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};while(xrPollEvent(instance,&event)==XR_SUCCESS){if(event.type==XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED){auto& state=*reinterpret_cast<XrEventDataSessionStateChanged*>(&event);LOG("Session state %d",state.state);visible=state.state==XR_SESSION_STATE_VISIBLE||state.state==XR_SESSION_STATE_FOCUSED;focused=state.state==XR_SESSION_STATE_FOCUSED;
   if(state.state==XR_SESSION_STATE_READY&&!running){XrSessionBeginInfo begin{XR_TYPE_SESSION_BEGIN_INFO};begin.primaryViewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;XR(xrBeginSession(session,&begin));running=true;if(setPerformance){XR(setPerformance(session,XR_PERF_SETTINGS_DOMAIN_CPU_EXT,XR_PERF_SETTINGS_LEVEL_SUSTAINED_HIGH_EXT));XR(setPerformance(session,XR_PERF_SETTINGS_DOMAIN_GPU_EXT,XR_PERF_SETTINGS_LEVEL_SUSTAINED_HIGH_EXT));}if(requestRate)XR(requestRate(session,120));}
   if(state.state==XR_SESSION_STATE_FOCUSED&&requestRate){XR(requestRate(session,120));XR(getRate(session,&tracking->refresh_hz));LOG("Focused display refresh %.0f Hz",tracking->refresh_hz);}
   if(state.state==XR_SESSION_STATE_STOPPING&&running){XR(xrEndSession(session));running=false;}
   if(state.state==XR_SESSION_STATE_EXITING||state.state==XR_SESSION_STATE_LOSS_PENDING)return false;
  }else if(event.type==XR_TYPE_EVENT_DATA_DISPLAY_REFRESH_RATE_CHANGED_FB){auto& rate=*reinterpret_cast<XrEventDataDisplayRefreshRateChangedFB*>(&event);tracking->refresh_hz=rate.toDisplayRefreshRate;LOG("Display refresh changed to %.0f Hz",rate.toDisplayRefreshRate);}else if(event.type==XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED){for(int i=0;i<2;i++){XrInteractionProfileState profile{XR_TYPE_INTERACTION_PROFILE_STATE};XR(xrGetCurrentInteractionProfile(session,hands[i],&profile));char name[XR_MAX_PATH_LENGTH]="none";uint32_t size=0;if(profile.interactionProfile)XR(xrPathToString(instance,profile.interactionProfile,sizeof(name),&size,name));LOG("Hand %d interaction profile %s",i,name);}}event={XR_TYPE_EVENT_DATA_BUFFER};}return true;
 }
 float value(XrAction action,int i){XrActionStateGetInfo info{XR_TYPE_ACTION_STATE_GET_INFO};info.action=action;info.subactionPath=hands[i];XrActionStateFloat state{XR_TYPE_ACTION_STATE_FLOAT};XR(xrGetActionStateFloat(session,&info,&state));return state.isActive?state.currentState:0;}
 bool button(XrAction action,int i){XrActionStateGetInfo info{XR_TYPE_ACTION_STATE_GET_INFO};info.action=action;info.subactionPath=hands[i];XrActionStateBoolean state{XR_TYPE_ACTION_STATE_BOOLEAN};XR(xrGetActionStateBoolean(session,&info,&state));return state.isActive&&state.currentState;}
 void haptics(){
  for(int count=0;count<64;count++){
   QuestHaptic packet{};auto size=recv(hapticSocket,&packet,sizeof(packet),MSG_TRUNC);
   if(size<0)break;
   if(size!=sizeof(packet)||packet.magic!=QUEST_BRIDGE_MAGIC||packet.hand>1||!focused||!std::isfinite(packet.duration_seconds)||!std::isfinite(packet.frequency)||!std::isfinite(packet.amplitude))continue;
   XrHapticActionInfo info{XR_TYPE_HAPTIC_ACTION_INFO};info.action=hapticAction;info.subactionPath=hands[packet.hand];
   XrResult result;
   if(packet.duration_seconds<=0||packet.amplitude<=0)result=xrStopHapticFeedback(session,&info);
   else{XrHapticVibration vibration{XR_TYPE_HAPTIC_VIBRATION};vibration.duration=static_cast<XrDuration>(std::min(packet.duration_seconds,5.f)*1000000000.0);vibration.frequency=std::max(packet.frequency,0.f);vibration.amplitude=std::min(packet.amplitude,1.f);result=xrApplyHapticFeedback(session,&info,reinterpret_cast<const XrHapticBaseHeader*>(&vibration));}
   LOG("Controller %u haptic result=%d",packet.hand,result);
  }
 }
 void updateStatus(XrTime time){if(statusUpdated&&time-statusUpdated<16000000)return;statusUpdated=time;JNIEnv* env;app->activity->vm->GetEnv(reinterpret_cast<void**>(&env),JNI_VERSION_1_6);jclass type=env->GetObjectClass(app->activity->clazz);jmethodID method=env->GetMethodID(type,"getBootBitmap","()Landroid/graphics/Bitmap;");jobject bitmap=env->CallObjectMethod(app->activity->clazz,method);if(env->ExceptionCheck()){env->ExceptionDescribe();env->ExceptionClear();env->DeleteLocalRef(type);return;}AndroidBitmapInfo info{};void* pixels=nullptr;if(AndroidBitmap_getInfo(env,bitmap,&info)==ANDROID_BITMAP_RESULT_SUCCESS&&AndroidBitmap_lockPixels(env,bitmap,&pixels)==ANDROID_BITMAP_RESULT_SUCCESS){uint32_t index;XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};XR(xrAcquireSwapchainImage(statusEye.chain,&acquire,&index));XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};wait.timeout=XR_INFINITE_DURATION;XR(xrWaitSwapchainImage(statusEye.chain,&wait));glBindTexture(GL_TEXTURE_2D,statusEye.images[index].image);glPixelStorei(GL_UNPACK_ROW_LENGTH,info.stride/4);glTexSubImage2D(GL_TEXTURE_2D,0,0,0,info.width,info.height,GL_RGBA,GL_UNSIGNED_BYTE,pixels);glPixelStorei(GL_UNPACK_ROW_LENGTH,0);AndroidBitmap_unlockPixels(env,bitmap);glFlush();XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};XR(xrReleaseSwapchainImage(statusEye.chain,&release));}env->DeleteLocalRef(bitmap);env->DeleteLocalRef(type);}
 bool bootComplete(){JNIEnv* env;app->activity->vm->GetEnv(reinterpret_cast<void**>(&env),JNI_VERSION_1_6);jclass type=env->GetObjectClass(app->activity->clazz);jmethodID method=env->GetMethodID(type,"bootComplete","()Z");bool complete=env->CallBooleanMethod(app->activity->clazz,method);if(env->ExceptionCheck()){env->ExceptionDescribe();env->ExceptionClear();complete=false;}env->DeleteLocalRef(type);return complete;}
 void frame(){
  XrFrameWaitInfo wait{XR_TYPE_FRAME_WAIT_INFO};XrFrameState state{XR_TYPE_FRAME_STATE};XR(xrWaitFrame(session,&wait,&state));XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO};XR(xrBeginFrame(session,&begin));
  XrActiveActionSet active{actions,XR_NULL_PATH};XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO};sync.countActiveActionSets=1;sync.activeActionSets=&active;XrResult syncResult=xrSyncActions(session,&sync);if(syncResult!=XR_SESSION_NOT_FOCUSED)check(syncResult,"xrSyncActions");
  haptics();
  XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO};locate.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;locate.displayTime=state.predictedDisplayTime;locate.space=stage;XrViewState viewState{XR_TYPE_VIEW_STATE};XrView views[2]={{XR_TYPE_VIEW},{XR_TYPE_VIEW}};uint32_t count;XR(xrLocateViews(session,&locate,&viewState,2,&count,views));
  __atomic_add_fetch(&tracking->sequence,1,__ATOMIC_ACQ_REL);tracking->predicted_time_ns=state.predictedDisplayTime;if(timeToMonotonic){timespec predicted{};XR(timeToMonotonic(instance,state.predictedDisplayTime,&predicted));tracking->predicted_monotonic_ns=static_cast<uint64_t>(predicted.tv_sec)*1000000000+predicted.tv_nsec;}tracking->session_visible=visible;tracking->session_focused=focused;
  XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};XR(xrLocateSpace(head,stage,state.predictedDisplayTime,&location));tracking->headset=pose(location.pose,location.locationFlags);
  for(int i=0;i<2;i++){
   XrActionStateGetInfo poseGet{XR_TYPE_ACTION_STATE_GET_INFO};poseGet.action=gripAction;poseGet.subactionPath=hands[i];XrActionStatePose poseState{XR_TYPE_ACTION_STATE_POSE};XR(xrGetActionStatePose(session,&poseGet,&poseState));XR(xrLocateSpace(grip[i],stage,state.predictedDisplayTime,&location));tracking->grip[i]=pose(location.pose,poseState.isActive?location.locationFlags:0);XR(xrLocateSpace(aim[i],stage,state.predictedDisplayTime,&location));tracking->aim[i]=pose(location.pose,location.locationFlags);tracking->eyes[i]=pose(views[i].pose,viewState.viewStateFlags);tracking->fov[i][0]=views[i].fov.angleLeft;tracking->fov[i][1]=views[i].fov.angleRight;tracking->fov[i][2]=views[i].fov.angleUp;tracking->fov[i][3]=views[i].fov.angleDown;
   tracking->trigger[i]=value(triggerAction,i);if(!tracking->trigger[i]&&button(selectAction,i))tracking->trigger[i]=1;tracking->squeeze[i]=value(squeezeAction,i);tracking->buttons[i]=0;for(int b=0;b<6;b++)if(button(buttonActions[b],i))tracking->buttons[i]|=1u<<b;tracking->touches[i]=button(touchAction,i)?1:0;
   XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO};get.action=stickAction;get.subactionPath=hands[i];XrActionStateVector2f stick{XR_TYPE_ACTION_STATE_VECTOR2F};XR(xrGetActionStateVector2f(session,&get,&stick));tracking->thumbstick[i][0]=stick.isActive?stick.currentState.x:0;tracking->thumbstick[i][1]=stick.isActive?stick.currentState.y:0;
  }__atomic_add_fetch(&tracking->sequence,1,__ATOMIC_RELEASE);
  // Passthrough is a compositor layer; camera pixels never enter Linux.
  bool requested=access((directory+"/passthrough.enabled").c_str(),F_OK)==0;if(passthroughLayer&&requested!=passthroughActive){if(requested){XR(startPassthrough(passthrough));XR(resumeLayer(passthroughLayer));}else{XR(pauseLayer(passthroughLayer));XR(pausePassthrough(passthrough));}passthroughActive=requested;LOG("Passthrough %s",requested?"enabled":"disabled");}
  XrCompositionLayerProjectionView projectionViews[2]={{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
  gpu.poll();
  // Repeated submissions of the same pixels must keep the same layer pose.
  // Updating it every xrWaitFrame incorrectly head-locks a stalled Linux image.
  if(gpu.ready()||!haveFrameViews){
   const auto& input=gpu.inputPose();bool valid=gpu.ready()&&input.pose_sequence;
   for(const auto& eye:input.views){
    for(float value:eye.position)valid=valid&&std::isfinite(value);
    float norm=0;for(float value:eye.orientation){valid=valid&&std::isfinite(value);norm+=value*value;}
    valid=valid&&norm>.9f&&norm<1.1f;
    for(float value:eye.fov)valid=valid&&std::isfinite(value);
    valid=valid&&eye.fov[0]<eye.fov[1]&&eye.fov[3]<eye.fov[2];
   }
   for(int eye=0;eye<2;eye++){
    frameViews[eye]=views[eye];
    if(valid){const auto& sampled=input.views[eye];
     frameViews[eye].pose.position={sampled.position[0],sampled.position[1],sampled.position[2]};
     frameViews[eye].pose.orientation={sampled.orientation[0],sampled.orientation[1],sampled.orientation[2],sampled.orientation[3]};
     frameViews[eye].fov={sampled.fov[0],sampled.fov[1],sampled.fov[2],sampled.fov[3]};
    }
   }haveFrameViews=true;
  }
  bool loading=access((directory+"/dashboard.ready").c_str(),F_OK)!=0||!bootComplete();
  if(state.shouldRender)for(int i=0;i<2;i++){
   Eye& eye=eyes[i];uint32_t index;XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};XR(xrAcquireSwapchainImage(eye.chain,&acquire,&index));XrSwapchainImageWaitInfo imageWait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};imageWait.timeout=XR_INFINITE_DURATION;XR(xrWaitSwapchainImage(eye.chain,&imageWait));glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,eye.images[index].image,0);glViewport(0,0,eye.width,eye.height);glDisable(GL_SCISSOR_TEST);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);if(!passthroughActive&&!loading)gpu.draw(i);glBindFramebuffer(GL_FRAMEBUFFER,0);glFlush();XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};XR(xrReleaseSwapchainImage(eye.chain,&release));projectionViews[i].pose=frameViews[i].pose;projectionViews[i].fov=frameViews[i].fov;projectionViews[i].subImage={eye.chain,{{0,0},{static_cast<int32_t>(eye.width),static_cast<int32_t>(eye.height)}},0};
  }
  XrCompositionLayerProjection projection{XR_TYPE_COMPOSITION_LAYER_PROJECTION};projection.space=stage;projection.layerFlags=0;projection.viewCount=2;projection.views=projectionViews;
  XrCompositionLayerPassthroughFB pass{XR_TYPE_COMPOSITION_LAYER_PASSTHROUGH_FB};pass.layerHandle=passthroughLayer;XrCompositionLayerQuad status{XR_TYPE_COMPOSITION_LAYER_QUAD};status.space=head;status.eyeVisibility=XR_EYE_VISIBILITY_BOTH;status.subImage={statusEye.chain,{{0,0},{450,450}},0};status.pose=identity();status.pose.position.z=-1.7f;status.size={.8f,.8f};const XrCompositionLayerBaseHeader* layers[3];uint32_t layerCount=0;if(state.shouldRender){if(passthroughActive)layers[layerCount++]=reinterpret_cast<XrCompositionLayerBaseHeader*>(&pass);if(!passthroughActive)layers[layerCount++]=reinterpret_cast<XrCompositionLayerBaseHeader*>(&projection);if(loading){updateStatus(state.predictedDisplayTime);layers[layerCount++]=reinterpret_cast<XrCompositionLayerBaseHeader*>(&status);}}
  XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};end.displayTime=state.predictedDisplayTime;end.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;end.layerCount=layerCount;end.layers=layers;XR(xrEndFrame(session,&end));if(state.shouldRender)gpu.finish();
  static uint32_t frames=0;if(++frames%1200==0){if(getRate)XR(getRate(session,&tracking->refresh_hz));LOG("Tracking frames %u, rate=%.0f focused=%d sync=%d HMD=(%.3f %.3f %.3f) hand flags=(%u %u)",frames,tracking->refresh_hz,focused,syncResult,tracking->headset.position[0],tracking->headset.position[1],tracking->headset.position[2],tracking->grip[0].flags,tracking->grip[1].flags);for(int i=0;i<2;i++){XrInteractionProfileState profile{XR_TYPE_INTERACTION_PROFILE_STATE};XR(xrGetCurrentInteractionProfile(session,hands[i],&profile));char name[XR_MAX_PATH_LENGTH]="none";uint32_t size;if(profile.interactionProfile)XR(xrPathToString(instance,profile.interactionProfile,sizeof(name),&size,name));LOG("Hand %d current profile %s",i,name);}}
 }
 void shutdown(){gpu.shutdown();if(hapticSocket>=0){close(hapticSocket);unlink((directory+"/haptics.sock").c_str());}if(tracking&&tracking!=MAP_FAILED)munmap(tracking,sizeof(*tracking));if(session)xrDestroySession(session);if(instance)xrDestroyInstance(instance);if(display!=EGL_NO_DISPLAY){eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);if(surface!=EGL_NO_SURFACE)eglDestroySurface(display,surface);if(context!=EGL_NO_CONTEXT)eglDestroyContext(display,context);eglTerminate(display);}app->activity->vm->DetachCurrentThread();}
};
extern "C" void android_main(android_app* app){Runtime runtime;runtime.app=app;try{while(!app->destroyRequested&&(!app->window||app->activityState!=APP_CMD_RESUME)){int events;android_poll_source* source;if(ALooper_pollOnce(50,nullptr,&events,reinterpret_cast<void**>(&source))>=0&&source)source->process(app,source);}if(app->destroyRequested)return;LOG("Android VR activity resumed with a native window");runtime.initialize(app);while(!app->destroyRequested){int events;android_poll_source* source;while(ALooper_pollOnce(runtime.running?0:50,nullptr,&events,reinterpret_cast<void**>(&source))>=0){if(source)source->process(app,source);if(app->destroyRequested)break;}if(app->destroyRequested||!runtime.events())break;if(runtime.running)runtime.frame();}}catch(const std::exception& e){LOG("Native runtime stopped: %s",e.what());}runtime.shutdown();}
