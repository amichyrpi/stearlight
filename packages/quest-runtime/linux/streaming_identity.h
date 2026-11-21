#pragma once
#include "../../openvr/headers/openvr.h"
#include <atomic>
#include <cstring>
#include <cstdio>
#include <mutex>

namespace quest {
using namespace vr;
// The host enables unified Frame menus for a Frame-protocol HMD. Present that
// identity only to the native VR player, retaining the real Quest driver and
// all display, tracking, serial and controller properties. Forward through the
// public SDK interface; never mutate the runtime's internal interface object.
class StreamingIdentity:public vr::IVRSystem {
 std::atomic<vr::IVRSystem*> original{nullptr};
 vr::IVRSystem* target() const {return original.load(std::memory_order_acquire);}
 static StreamingIdentity& instance(){static StreamingIdentity value;return value;}
public:
 static void* wrap(const char* version,void* object){
  if(!object||!version||strcmp(version,vr::IVRSystem_Version))return object;
  auto& value=instance();if(object==&value)return object;
  value.original.store(static_cast<vr::IVRSystem*>(object),std::memory_order_release);
  return &value;
 }
 uint32_t GetStringTrackedDeviceProperty(vr::TrackedDeviceIndex_t device,vr::ETrackedDeviceProperty property,char* out,uint32_t size,vr::ETrackedPropertyError* error) override {
  auto* api=target();
  if(device==0&&property==vr::Prop_ModelNumber_String){
   char model[128]{};vr::ETrackedPropertyError status{};
   api->GetStringTrackedDeviceProperty(device,property,model,sizeof(model),&status);
   if(status==vr::TrackedProp_Success&&(!strcmp(model,"Oculus Quest2")||!strcmp(model,"Meta Quest 3")||!strcmp(model,"Meta Quest 3S"))){
    constexpr char frame[]="Steam Frame";
    static std::once_flag logged;std::call_once(logged,[]{fputs("Quest streaming: native Frame protocol identity\n",stderr);});
    if(out&&size>=sizeof(frame)){memcpy(out,frame,sizeof(frame));if(error)*error=vr::TrackedProp_Success;}
    else {if(out&&size)*out=0;if(error)*error=vr::TrackedProp_BufferTooSmall;}
    return sizeof(frame);
   }
  }
  return api->GetStringTrackedDeviceProperty(device,property,out,size,error);
 }
 void GetRecommendedRenderTargetSize(uint32_t *pnWidth, uint32_t *pnHeight) override {return target()->GetRecommendedRenderTargetSize(pnWidth,pnHeight);}
 HmdMatrix44_t GetProjectionMatrix(EVREye eEye, float fNearZ, float fFarZ) override {return target()->GetProjectionMatrix(eEye,fNearZ,fFarZ);}
 void GetProjectionRaw(EVREye eEye, float *pfLeft, float *pfRight, float *pfTop, float *pfBottom) override {return target()->GetProjectionRaw(eEye,pfLeft,pfRight,pfTop,pfBottom);}
 bool ComputeDistortion(EVREye eEye, float fU, float fV, DistortionCoordinates_t *pDistortionCoordinates) override {return target()->ComputeDistortion(eEye,fU,fV,pDistortionCoordinates);}
 bool ComputeDistortionSet(EVREye eEye, EVRDistortionChannel eChannel, bool bAsNormalizedDeviceCoordinates, uint32_t nNumCoordinates, const DistortionCoordinate_t *pInput, DistortionCoordinate_t *pOutput) override {return target()->ComputeDistortionSet(eEye,eChannel,bAsNormalizedDeviceCoordinates,nNumCoordinates,pInput,pOutput);}
 HmdMatrix34_t GetEyeToHeadTransform(EVREye eEye) override {return target()->GetEyeToHeadTransform(eEye);}
 bool GetTimeSinceLastVsync(float *pfSecondsSinceLastVsync, uint64_t *pulFrameCounter) override {return target()->GetTimeSinceLastVsync(pfSecondsSinceLastVsync,pulFrameCounter);}
 int32_t GetD3D9AdapterIndex() override {return target()->GetD3D9AdapterIndex();}
 void GetDXGIOutputInfo(int32_t *pnAdapterIndex) override {return target()->GetDXGIOutputInfo(pnAdapterIndex);}
 void GetOutputDevice(uint64_t *pnDevice, ETextureType textureType, VkInstance_T *pInstance) override {return target()->GetOutputDevice(pnDevice,textureType,pInstance);}
 bool IsDisplayOnDesktop() override {return target()->IsDisplayOnDesktop();}
 bool SetDisplayVisibility(bool bIsVisibleOnDesktop) override {return target()->SetDisplayVisibility(bIsVisibleOnDesktop);}
 void GetDeviceToAbsoluteTrackingPose(ETrackingUniverseOrigin eOrigin, float fPredictedSecondsToPhotonsFromNow, TrackedDevicePose_t *pTrackedDevicePoseArray, uint32_t unTrackedDevicePoseArrayCount) override {return target()->GetDeviceToAbsoluteTrackingPose(eOrigin,fPredictedSecondsToPhotonsFromNow,pTrackedDevicePoseArray,unTrackedDevicePoseArrayCount);}
 HmdMatrix34_t GetSeatedZeroPoseToStandingAbsoluteTrackingPose() override {return target()->GetSeatedZeroPoseToStandingAbsoluteTrackingPose();}
 HmdMatrix34_t GetRawZeroPoseToStandingAbsoluteTrackingPose() override {return target()->GetRawZeroPoseToStandingAbsoluteTrackingPose();}
 uint32_t GetSortedTrackedDeviceIndicesOfClass(ETrackedDeviceClass eTrackedDeviceClass, vr::TrackedDeviceIndex_t *punTrackedDeviceIndexArray, uint32_t unTrackedDeviceIndexArrayCount, vr::TrackedDeviceIndex_t unRelativeToTrackedDeviceIndex) override {return target()->GetSortedTrackedDeviceIndicesOfClass(eTrackedDeviceClass,punTrackedDeviceIndexArray,unTrackedDeviceIndexArrayCount,unRelativeToTrackedDeviceIndex);}
 EDeviceActivityLevel GetTrackedDeviceActivityLevel(vr::TrackedDeviceIndex_t unDeviceId) override {return target()->GetTrackedDeviceActivityLevel(unDeviceId);}
 void ApplyTransform(TrackedDevicePose_t *pOutputPose, const TrackedDevicePose_t *pTrackedDevicePose, const HmdMatrix34_t *pTransform) override {return target()->ApplyTransform(pOutputPose,pTrackedDevicePose,pTransform);}
 vr::TrackedDeviceIndex_t GetTrackedDeviceIndexForControllerRole(vr::ETrackedControllerRole unDeviceType) override {return target()->GetTrackedDeviceIndexForControllerRole(unDeviceType);}
 vr::ETrackedControllerRole GetControllerRoleForTrackedDeviceIndex(vr::TrackedDeviceIndex_t unDeviceIndex) override {return target()->GetControllerRoleForTrackedDeviceIndex(unDeviceIndex);}
 ETrackedDeviceClass GetTrackedDeviceClass(vr::TrackedDeviceIndex_t unDeviceIndex) override {return target()->GetTrackedDeviceClass(unDeviceIndex);}
 bool IsTrackedDeviceConnected(vr::TrackedDeviceIndex_t unDeviceIndex) override {return target()->IsTrackedDeviceConnected(unDeviceIndex);}
 bool GetBoolTrackedDeviceProperty(vr::TrackedDeviceIndex_t unDeviceIndex, ETrackedDeviceProperty prop, ETrackedPropertyError *pError) override {return target()->GetBoolTrackedDeviceProperty(unDeviceIndex,prop,pError);}
 float GetFloatTrackedDeviceProperty(vr::TrackedDeviceIndex_t unDeviceIndex, ETrackedDeviceProperty prop, ETrackedPropertyError *pError) override {return target()->GetFloatTrackedDeviceProperty(unDeviceIndex,prop,pError);}
 int32_t GetInt32TrackedDeviceProperty(vr::TrackedDeviceIndex_t unDeviceIndex, ETrackedDeviceProperty prop, ETrackedPropertyError *pError) override {return target()->GetInt32TrackedDeviceProperty(unDeviceIndex,prop,pError);}
 uint64_t GetUint64TrackedDeviceProperty(vr::TrackedDeviceIndex_t unDeviceIndex, ETrackedDeviceProperty prop, ETrackedPropertyError *pError) override {return target()->GetUint64TrackedDeviceProperty(unDeviceIndex,prop,pError);}
 HmdMatrix34_t GetMatrix34TrackedDeviceProperty(vr::TrackedDeviceIndex_t unDeviceIndex, ETrackedDeviceProperty prop, ETrackedPropertyError *pError) override {return target()->GetMatrix34TrackedDeviceProperty(unDeviceIndex,prop,pError);}
 uint32_t GetArrayTrackedDeviceProperty(vr::TrackedDeviceIndex_t unDeviceIndex, ETrackedDeviceProperty prop, PropertyTypeTag_t propType, void *pBuffer, uint32_t unBufferSize, ETrackedPropertyError *pError) override {return target()->GetArrayTrackedDeviceProperty(unDeviceIndex,prop,propType,pBuffer,unBufferSize,pError);}
 const char * GetPropErrorNameFromEnum(ETrackedPropertyError error) override {return target()->GetPropErrorNameFromEnum(error);}
 bool PollNextEvent(VREvent_t *pEvent, uint32_t uncbVREvent) override {return target()->PollNextEvent(pEvent,uncbVREvent);}
 bool PollNextEventWithPose(ETrackingUniverseOrigin eOrigin, VREvent_t *pEvent, uint32_t uncbVREvent, vr::TrackedDevicePose_t *pTrackedDevicePose) override {return target()->PollNextEventWithPose(eOrigin,pEvent,uncbVREvent,pTrackedDevicePose);}
 bool PollNextEventWithPoseAndOverlays(vr::ETrackingUniverseOrigin eOrigin, VREvent_t *pEvent, uint32_t uncbVREvent, TrackedDevicePose_t *pTrackedDevicePose, VROverlayHandle_t *pulOverlayHandle) override {return target()->PollNextEventWithPoseAndOverlays(eOrigin,pEvent,uncbVREvent,pTrackedDevicePose,pulOverlayHandle);}
 const char * GetEventTypeNameFromEnum(EVREventType eType) override {return target()->GetEventTypeNameFromEnum(eType);}
 HiddenAreaMesh_t GetHiddenAreaMesh(EVREye eEye, EHiddenAreaMeshType type) override {return target()->GetHiddenAreaMesh(eEye,type);}
 bool GetEyeTrackedFoveationCenter(HmdVector2_t *pNdcLeft, HmdVector2_t *pNdcRight) override {return target()->GetEyeTrackedFoveationCenter(pNdcLeft,pNdcRight);}
 bool GetEyeTrackedFoveationCenterForProjection(const HmdMatrix44_t *pProjMat, HmdVector2_t *pNdc) override {return target()->GetEyeTrackedFoveationCenterForProjection(pProjMat,pNdc);}
 bool GetControllerState(vr::TrackedDeviceIndex_t unControllerDeviceIndex, vr::VRControllerState_t *pControllerState, uint32_t unControllerStateSize) override {return target()->GetControllerState(unControllerDeviceIndex,pControllerState,unControllerStateSize);}
 bool GetControllerStateWithPose(ETrackingUniverseOrigin eOrigin, vr::TrackedDeviceIndex_t unControllerDeviceIndex, vr::VRControllerState_t *pControllerState, uint32_t unControllerStateSize, TrackedDevicePose_t *pTrackedDevicePose) override {return target()->GetControllerStateWithPose(eOrigin,unControllerDeviceIndex,pControllerState,unControllerStateSize,pTrackedDevicePose);}
 void TriggerHapticPulse(vr::TrackedDeviceIndex_t unControllerDeviceIndex, uint32_t unAxisId, unsigned short usDurationMicroSec) override {return target()->TriggerHapticPulse(unControllerDeviceIndex,unAxisId,usDurationMicroSec);}
 const char * GetButtonIdNameFromEnum(EVRButtonId eButtonId) override {return target()->GetButtonIdNameFromEnum(eButtonId);}
 const char * GetControllerAxisTypeNameFromEnum(EVRControllerAxisType eAxisType) override {return target()->GetControllerAxisTypeNameFromEnum(eAxisType);}
 bool IsInputAvailable() override {return target()->IsInputAvailable();}
 bool IsSteamVRDrawingControllers() override {return target()->IsSteamVRDrawingControllers();}
 bool ShouldApplicationPause() override {return target()->ShouldApplicationPause();}
 bool ShouldApplicationReduceRenderingWork() override {return target()->ShouldApplicationReduceRenderingWork();}
 vr::EVRFirmwareError PerformFirmwareUpdate(vr::TrackedDeviceIndex_t unDeviceIndex) override {return target()->PerformFirmwareUpdate(unDeviceIndex);}
 void AcknowledgeQuit_Exiting() override {return target()->AcknowledgeQuit_Exiting();}
 uint32_t GetAppContainerFilePaths(char *pchBuffer, uint32_t unBufferSize) override {return target()->GetAppContainerFilePaths(pchBuffer,unBufferSize);}
 const char * GetRuntimeVersion() override {return target()->GetRuntimeVersion();}
 vr::EVRInitError SetSDKVersion(uint32_t nVersionMajor, uint32_t nVersionMinor, uint32_t nVersionBuild) override {return target()->SetSDKVersion(nVersionMajor,nVersionMinor,nVersionBuild);}
};
}
