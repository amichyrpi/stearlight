// VM-only OpenVR server driver used when QEMU has no physical headset or
// controllers. This driver contributes a virtual HMD and two deterministic
// Steam Frame controller devices so
// vrserver, vrdashboard, input bindings, and the laser cursor use the same
// native Valve path as a real session. The display redirect remains available
// for driver-level tests when enabled explicitly.

#include <openvr_driver.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

namespace {

// QEMU has no physical headset. The fake HMD follows the same virtual-display
// contract as the production SVRT driver: Valve owns the compositor window,
// while the driver accepts the final backbuffer without claiming a hardware
// direct-mode display. This lets vrdashboard render its real laser and Steam
// Frame dock into the guest X11/VGA surface.
class FakeHmd final : public vr::ITrackedDeviceServerDriver,
                      public vr::IVRDisplayComponent {
 public:
  const char *serial() const { return "SVRT-FAKE-HMD"; }

  vr::EVRInitError Activate(uint32_t object_id) override {
    index_ = object_id;
    const auto properties =
        vr::VRProperties()->TrackedDeviceToPropertyContainer(object_id);
    vr::VRProperties()->SetStringProperty(
        properties, vr::Prop_TrackingSystemName_String, "stearlight-fake");
    vr::VRProperties()->SetStringProperty(
        properties, vr::Prop_ModelNumber_String, "Stearlight Virtual HMD");
    vr::VRProperties()->SetStringProperty(
        properties, vr::Prop_ManufacturerName_String, "Stearlight");
    vr::VRProperties()->SetStringProperty(
        properties, vr::Prop_SerialNumber_String, serial());
    vr::VRProperties()->SetStringProperty(
        properties, vr::Prop_RenderModelName_String, "generic_hmd");
    vr::VRProperties()->SetStringProperty(
        properties, vr::Prop_ResourceRoot_String, "svrt-fake");
    vr::VRProperties()->SetStringProperty(
        properties, vr::Prop_RegisteredDeviceType_String,
        "stearlight-fake/SVRT-FAKE-HMD");
    vr::VRProperties()->SetUint64Property(
        properties, vr::Prop_CurrentUniverseId_Uint64, 1);
    vr::VRProperties()->SetInt32Property(
        properties, vr::Prop_ExpectedControllerCount_Int32, 2);
    vr::VRProperties()->SetFloatProperty(
        properties, vr::Prop_UserIpdMeters_Float, 0.063f);
    vr::VRProperties()->SetFloatProperty(
        properties, vr::Prop_DisplayFrequency_Float, 60.0f);
    vr::VRProperties()->SetFloatProperty(
        properties, vr::Prop_SecondsFromVsyncToPhotons_Float, 0.011111f);
    // This HMD exposes the ordinary display contract, but explicitly marks
    // the panel as fictional. SteamVR can therefore use the real compositor
    // and dashboard while presenting the result in its Linux mirror window;
    // it must not attempt a DRM/RandR lease for a panel QEMU does not have.
    vr::VRProperties()->SetBoolProperty(
        properties, vr::Prop_IsOnDesktop_Bool, true);
    vr::VRProperties()->SetBoolProperty(
        properties, vr::Prop_HasDisplayComponent_Bool, true);
    vr::VRProperties()->SetBoolProperty(
        properties, vr::Prop_HasDriverDirectModeComponent_Bool, false);
    vr::VRProperties()->SetBoolProperty(
        properties, vr::Prop_HasVirtualDisplayComponent_Bool, false);
    vr::VRProperties()->SetBoolProperty(
        properties, vr::Prop_DisplayDebugMode_Bool, false);
    vr::VRProperties()->SetBoolProperty(
        properties, vr::Prop_DriverDirectModeSendsVsyncEvents_Bool, false);
    vr::VRProperties()->SetBoolProperty(
        properties, vr::Prop_ReportsTimeSinceVSync_Bool, true);
    vr::VRProperties()->SetBoolProperty(
        properties, vr::Prop_DeviceIsWireless_Bool, true);
    vr::VRProperties()->SetBoolProperty(
        properties, vr::Prop_DeviceCanPowerOff_Bool, false);
    vr::VRProperties()->SetBoolProperty(
        properties, vr::Prop_NeverTracked_Bool, false);
    return vr::VRInitError_None;
  }

  void Deactivate() override {
    index_ = vr::k_unTrackedDeviceIndexInvalid;
  }

  void EnterStandby() override {}

  void *GetComponent(const char *version) override {
    if (vr::VRDriverLog()) {
      char message[160];
      std::snprintf(message, sizeof(message),
                    "svrt-fake: HMD GetComponent(%s)",
                    version ? version : "<null>");
      vr::VRDriverLog()->Log(message);
    }
    if (version && !std::strcmp(version, vr::IVRDisplayComponent_Version))
      return static_cast<vr::IVRDisplayComponent *>(this);
    return nullptr;
  }

  void GetWindowBounds(int32_t *x, int32_t *y, uint32_t *width,
                       uint32_t *height) override {
    if (x) *x = 0;
    if (y) *y = 0;
    if (width) *width = 2880;
    if (height) *height = 1600;
  }

  bool IsDisplayOnDesktop() override {
    if (vr::VRDriverLog())
      vr::VRDriverLog()->Log("svrt-fake: HMD IsDisplayOnDesktop=true");
    return true;
  }

  bool IsDisplayRealDisplay() override {
    if (vr::VRDriverLog())
      vr::VRDriverLog()->Log("svrt-fake: HMD IsDisplayRealDisplay=false");
    return false;
  }

  void GetRecommendedRenderTargetSize(uint32_t *width,
                                      uint32_t *height) override {
    if (width) *width = 2880;
    if (height) *height = 1600;
  }

  void GetEyeOutputViewport(vr::EVREye eye, uint32_t *x, uint32_t *y,
                            uint32_t *width, uint32_t *height) override {
    if (x) *x = eye == vr::Eye_Left ? 0 : 1440;
    if (y) *y = 0;
    if (width) *width = 1440;
    if (height) *height = 1600;
  }

  void GetProjectionRaw(vr::EVREye, float *left, float *right, float *top,
                        float *bottom) override {
    if (left) *left = -1.0f;
    if (right) *right = 1.0f;
    if (top) *top = -1.0f;
    if (bottom) *bottom = 1.0f;
  }

  vr::DistortionCoordinates_t ComputeDistortion(vr::EVREye, float u,
                                                 float v) override {
    vr::DistortionCoordinates_t result{};
    result.rfRed[0] = result.rfGreen[0] = result.rfBlue[0] = u;
    result.rfRed[1] = result.rfGreen[1] = result.rfBlue[1] = v;
    return result;
  }

  bool ComputeInverseDistortion(vr::HmdVector2_t *result, vr::EVREye,
                                uint32_t, float u, float v) override {
    if (!result) return false;
    result->v[0] = u;
    result->v[1] = v;
    return true;
  }

  void DebugRequest(const char *, char *response, uint32_t size) override {
    if (response && size) response[0] = '\0';
  }

  vr::DriverPose_t GetPose() override {
    vr::DriverPose_t pose{};
    pose.qWorldFromDriverRotation.w = 1.0;
    pose.qDriverFromHeadRotation.w = 1.0;
    pose.qRotation.w = 1.0;
    pose.vecPosition[1] = 1.6;
    pose.poseIsValid = true;
    pose.deviceIsConnected = true;
    pose.result = vr::TrackingResult_Running_OK;
    return pose;
  }

  void RunFrame() {
    if (index_ == vr::k_unTrackedDeviceIndexInvalid) return;
    vr::VRServerDriverHost()->TrackedDevicePoseUpdated(
        index_, GetPose(), sizeof(vr::DriverPose_t));
  }

 private:
  vr::TrackedDeviceIndex_t index_ = vr::k_unTrackedDeviceIndexInvalid;
};

// SteamVR selects IVRVirtualDisplay from a DisplayRedirect device, not from
// the HMD's ordinary display component. Keep the fake HMD above for the
// tracking/display description and register this companion device with the
// exact class required by Valve's virtual-display contract. The compositor
// still owns the guest X11 surface; this device only receives the final
// composited frame and supplies deterministic timing.
class FakeDisplayRedirect final : public vr::ITrackedDeviceServerDriver,
                                  public vr::IVRVirtualDisplay {
 public:
  const char *serial() const { return "SVRT-FAKE-DISPLAY-REDIRECT"; }

  vr::EVRInitError Activate(uint32_t object_id) override {
    index_ = object_id;
    const auto properties =
        vr::VRProperties()->TrackedDeviceToPropertyContainer(object_id);
    vr::VRProperties()->SetStringProperty(
        properties, vr::Prop_TrackingSystemName_String, "stearlight-fake");
    vr::VRProperties()->SetStringProperty(
        properties, vr::Prop_ModelNumber_String, "Stearlight Virtual Display");
    vr::VRProperties()->SetStringProperty(
        properties, vr::Prop_ManufacturerName_String, "Stearlight");
    vr::VRProperties()->SetStringProperty(
        properties, vr::Prop_SerialNumber_String, serial());
    vr::VRProperties()->SetBoolProperty(
        properties, vr::Prop_HasVirtualDisplayComponent_Bool, true);
    vr::VRProperties()->SetBoolProperty(
        properties, vr::Prop_DeviceIsWireless_Bool, true);
    return vr::VRInitError_None;
  }

  void Deactivate() override {
    index_ = vr::k_unTrackedDeviceIndexInvalid;
  }

  void EnterStandby() override {}

  void *GetComponent(const char *version) override {
    if (version && !std::strcmp(version, vr::IVRVirtualDisplay_Version))
      return static_cast<vr::IVRVirtualDisplay *>(this);
    return nullptr;
  }

  void DebugRequest(const char *, char *response, uint32_t size) override {
    if (response && size) response[0] = '\0';
  }

  vr::DriverPose_t GetPose() override {
    vr::DriverPose_t pose{};
    pose.qWorldFromDriverRotation.w = 1.0;
    pose.qDriverFromHeadRotation.w = 1.0;
    pose.qRotation.w = 1.0;
    pose.poseIsValid = true;
    pose.deviceIsConnected = true;
    pose.result = vr::TrackingResult_Running_OK;
    return pose;
  }

  void Present(const vr::PresentInfo_t *, uint32_t) override {
    ++frame_counter_;
  }

  void WaitForPresent() override {}

  bool GetTimeSinceLastVsync(float *seconds, uint64_t *frame) override {
    if (!seconds || !frame) return false;
    *seconds = 0.0f;
    *frame = frame_counter_;
    return true;
  }

 private:
  vr::TrackedDeviceIndex_t index_ = vr::k_unTrackedDeviceIndexInvalid;
  uint64_t frame_counter_ = 0;
};

enum InputComponent {
  kSystem,
  // These names match Valve's resource-only frame_controller profile.
  kMenu,
  kView,
  kA,
  kB,
  kX,
  kY,
  kDpadUp,
  kDpadRight,
  kDpadDown,
  kDpadLeft,
  kBumper,
  kTriggerValue,
  kTriggerClick,
  kGripValue,
  kGripClick,
  kThumbstickX,
  kThumbstickY,
  kThumbstickClick,
  kThumbrestTouch,
  // Retain the old path as an alias for existing test bindings.
  kApplicationMenu,
  // Retain the old trackpad paths as aliases for existing test bindings.
  kTrackpadX,
  kTrackpadY,
  kTrackpadClick,
  kHaptic,
  kInputCount,
};

class FakeController final : public vr::ITrackedDeviceServerDriver {
 public:
  explicit FakeController(vr::ETrackedControllerRole role)
      : role_(role),
        serial_(role == vr::TrackedControllerRole_LeftHand
                    ? "SVRT-FAKE-CONTROLLER-L"
                    : "SVRT-FAKE-CONTROLLER-R"),
        icon_prefix_(role == vr::TrackedControllerRole_LeftHand ? "left_"
                                                                 : "right_") {}

  const char *serial() const { return serial_.c_str(); }

  vr::EVRInitError Activate(uint32_t object_id) override {
    index_ = object_id;
    const auto properties =
        vr::VRProperties()->TrackedDeviceToPropertyContainer(object_id);

    vr::VRProperties()->SetStringProperty(
        properties, vr::Prop_TrackingSystemName_String, "stearlight-fake");
    vr::VRProperties()->SetStringProperty(
        properties, vr::Prop_ModelNumber_String,
        "Stearlight Virtual Controller");
    vr::VRProperties()->SetStringProperty(
        properties, vr::Prop_ManufacturerName_String, "Stearlight");
    vr::VRProperties()->SetStringProperty(properties,
                                          vr::Prop_SerialNumber_String,
                                          serial_.c_str());
    vr::VRProperties()->SetStringProperty(properties,
                                          vr::Prop_RenderModelName_String,
                                          "frame_controller");
    vr::VRProperties()->SetStringProperty(properties,
                                          vr::Prop_ControllerType_String,
                                          "frame_controller");
    vr::VRProperties()->SetStringProperty(properties,
                                          vr::Prop_ResourceRoot_String,
                                          "frame_controller");
    vr::VRProperties()->SetInt32Property(
        properties, vr::Prop_ControllerRoleHint_Int32, role_);
    vr::VRProperties()->SetStringProperty(
        properties, vr::Prop_InputProfilePath_String,
        "{frame_controller}/input/frame_controller_profile.json");
    vr::VRProperties()->SetStringProperty(
        properties, vr::Prop_NamedIconPathDeviceOff_String,
        ("{frame_controller}/icons/" + icon_prefix_ +
         "frame_controller_off.png")
            .c_str());
    vr::VRProperties()->SetStringProperty(
        properties, vr::Prop_NamedIconPathDeviceSearching_String,
        ("{frame_controller}/icons/" + icon_prefix_ +
         "frame_controller_seaching.gif")
            .c_str());
    vr::VRProperties()->SetStringProperty(
        properties, vr::Prop_NamedIconPathDeviceReady_String,
        ("{frame_controller}/icons/" + icon_prefix_ +
         "frame_controller_ready.png")
            .c_str());
    vr::VRProperties()->SetStringProperty(
        properties, vr::Prop_NamedIconPathDeviceNotReady_String,
        ("{frame_controller}/icons/" + icon_prefix_ +
         "frame_controller_error.png")
            .c_str());
    vr::VRProperties()->SetBoolProperty(properties,
                                        vr::Prop_DeviceIsWireless_Bool, true);
    vr::VRProperties()->SetBoolProperty(
        properties, vr::Prop_DeviceProvidesBatteryStatus_Bool, true);
    vr::VRProperties()->SetFloatProperty(
        properties, vr::Prop_DeviceBatteryPercentage_Float, 0.91f);
    vr::VRProperties()->SetBoolProperty(properties,
                                        vr::Prop_DeviceIsCharging_Bool, false);
    vr::VRProperties()->SetBoolProperty(
        properties, vr::Prop_DeviceCanPowerOff_Bool, false);
    vr::VRProperties()->SetBoolProperty(properties, vr::Prop_NeverTracked_Bool,
                                        false);

    auto *input = vr::VRDriverInput();
    input->CreateBooleanComponent(properties, "/input/system/click",
                                  &inputs_[kSystem]);
    input->CreateBooleanComponent(properties, "/input/menu/click",
                                  &inputs_[kMenu]);
    input->CreateBooleanComponent(properties, "/input/view/click",
                                  &inputs_[kView]);
    input->CreateBooleanComponent(properties, "/input/a/click",
                                  &inputs_[kA]);
    input->CreateBooleanComponent(properties, "/input/b/click",
                                  &inputs_[kB]);
    input->CreateBooleanComponent(properties, "/input/x/click",
                                  &inputs_[kX]);
    input->CreateBooleanComponent(properties, "/input/y/click",
                                  &inputs_[kY]);
    input->CreateBooleanComponent(properties, "/input/dpad_up/click",
                                  &inputs_[kDpadUp]);
    input->CreateBooleanComponent(properties, "/input/dpad_right/click",
                                  &inputs_[kDpadRight]);
    input->CreateBooleanComponent(properties, "/input/dpad_down/click",
                                  &inputs_[kDpadDown]);
    input->CreateBooleanComponent(properties, "/input/dpad_left/click",
                                  &inputs_[kDpadLeft]);
    input->CreateBooleanComponent(properties, "/input/bumper/click",
                                  &inputs_[kBumper]);
    input->CreateScalarComponent(properties, "/input/trigger/value",
                                 &inputs_[kTriggerValue],
                                 vr::VRScalarType_Absolute,
                                 vr::VRScalarUnits_NormalizedOneSided);
    input->CreateBooleanComponent(properties, "/input/trigger/click",
                                  &inputs_[kTriggerClick]);
    input->CreateScalarComponent(properties, "/input/grip/value",
                                 &inputs_[kGripValue],
                                 vr::VRScalarType_Absolute,
                                 vr::VRScalarUnits_NormalizedOneSided);
    input->CreateBooleanComponent(properties, "/input/grip/click",
                                  &inputs_[kGripClick]);
    input->CreateScalarComponent(properties, "/input/thumbstick/x",
                                 &inputs_[kThumbstickX],
                                 vr::VRScalarType_Absolute,
                                 vr::VRScalarUnits_NormalizedTwoSided);
    input->CreateScalarComponent(properties, "/input/thumbstick/y",
                                 &inputs_[kThumbstickY],
                                 vr::VRScalarType_Absolute,
                                 vr::VRScalarUnits_NormalizedTwoSided);
    input->CreateBooleanComponent(properties, "/input/thumbstick/click",
                                  &inputs_[kThumbstickClick]);
    input->CreateBooleanComponent(properties, "/input/thumbrest/touch",
                                  &inputs_[kThumbrestTouch]);
    // Compatibility aliases for the older local profile.
    input->CreateBooleanComponent(properties, "/input/application_menu/click",
                                  &inputs_[kApplicationMenu]);
    input->CreateScalarComponent(properties, "/input/trackpad/x",
                                 &inputs_[kTrackpadX],
                                 vr::VRScalarType_Absolute,
                                 vr::VRScalarUnits_NormalizedTwoSided);
    input->CreateScalarComponent(properties, "/input/trackpad/y",
                                 &inputs_[kTrackpadY],
                                 vr::VRScalarType_Absolute,
                                 vr::VRScalarUnits_NormalizedTwoSided);
    input->CreateBooleanComponent(properties, "/input/trackpad/click",
                                  &inputs_[kTrackpadClick]);
    input->CreateHapticComponent(properties, "/output/haptic",
                                 &inputs_[kHaptic]);
    // The VM has no physical controller with which to dismiss Steam Frame's
    // first-run page.  Keep this opt-in so normal fake-controller operation
    // remains passive; vm-console enables it only for the QEMU appliance.
    auto_select_ = std::getenv("STEARLIGHT_FAKE_AUTO_SELECT") != nullptr;
    if (vr::VRDriverLog()) {
      char message[128];
      std::snprintf(message, sizeof(message),
                    "SVRT fake controller %s auto_select=%d", serial_.c_str(),
                    auto_select_ ? 1 : 0);
      vr::VRDriverLog()->Log(message);
      std::snprintf(message, sizeof(message),
                    "SVRT fake controller %s system_handle=%llu a_handle=%llu",
                    serial_.c_str(),
                    static_cast<unsigned long long>(inputs_[kSystem]),
                    static_cast<unsigned long long>(inputs_[kA]));
      vr::VRDriverLog()->Log(message);
    }
    frame_count_ = 0;
    active_ = true;
    return vr::VRInitError_None;
  }

  void Deactivate() override {
    active_ = false;
    index_ = vr::k_unTrackedDeviceIndexInvalid;
  }

  void EnterStandby() override {}

  void *GetComponent(const char *) override { return nullptr; }

  void DebugRequest(const char *, char *response, uint32_t size) override {
    if (response && size) response[0] = '\0';
  }

  vr::DriverPose_t GetPose() override {
    vr::DriverPose_t pose{};
    pose.qWorldFromDriverRotation.w = 1.0;
    pose.qDriverFromHeadRotation.w = 1.0;
    pose.qRotation.w = 1.0;
    pose.poseIsValid = true;
    pose.deviceIsConnected = true;
    pose.result = vr::TrackingResult_Running_OK;
    // Keep the controllers in the user's forward view. SteamVR's driver
    // coordinate system is +X right, +Y up and -Z forward.
    pose.vecPosition[0] = role_ == vr::TrackedControllerRole_LeftHand
                               ? -0.24
                               : 0.24;
    pose.vecPosition[1] = 1.22;
    pose.vecPosition[2] = -0.55;
    pose.shouldApplyHeadModel = false;
    pose.willDriftInYaw = false;
    return pose;
  }

  void RunFrame() {
    if (!active_ || index_ == vr::k_unTrackedDeviceIndexInvalid) return;
    ++frame_count_;
    vr::VRServerDriverHost()->TrackedDevicePoseUpdated(index_, GetPose(),
                                                        sizeof(vr::DriverPose_t));
    auto *input = vr::VRDriverInput();
    // Open the real SteamVR dashboard once in the VM.  This is a controller
    // system-button edge, so vrdashboard owns the actual Steam Frame dock and
    // laser; no SDL overlay is involved.  Only the right controller pulses so
    // the two mirrored system bindings cannot toggle the dashboard twice.
    const bool vm_right_pulse = auto_select_ &&
                                 role_ == vr::TrackedControllerRole_RightHand;
    const bool dashboard_toggle = vm_right_pulse && frame_count_ >= 180 &&
                                  frame_count_ < 240;
    const vr::EVRInputError system_error = input->UpdateBooleanComponent(
        inputs_[kSystem], dashboard_toggle, 0.0);
    input->UpdateBooleanComponent(inputs_[kMenu], false, 0.0);
    input->UpdateBooleanComponent(inputs_[kView], false, 0.0);
    input->UpdateBooleanComponent(inputs_[kApplicationMenu], false, 0.0);
    // Steam Frame's onboarding page is a native dashboard surface and only
    // advances on a controller select. Startup can take more than a minute
    // in the VM while Steam's WebHelper is created, so pulse after the
    // surface is likely to exist and repeat at a bounded cadence. This is
    // enabled only by the VM supervisor; production fake-controller sessions
    // remain passive.
    const bool auto_select =
        vm_right_pulse && frame_count_ >= 600 &&
        ((frame_count_ - 600) % 600) < 60;
    const float trigger_value = auto_select ? 1.0f : 0.0f;
    const vr::EVRInputError a_error =
        input->UpdateBooleanComponent(inputs_[kA], auto_select, 0.0);
    if (dashboard_toggle && frame_count_ == 180 && vr::VRDriverLog()) {
      char message[160];
      std::snprintf(message, sizeof(message),
                    "SVRT fake controller pulsing dashboard system button system_error=%d a_error=%d",
                    static_cast<int>(system_error), static_cast<int>(a_error));
      vr::VRDriverLog()->Log(message);
    }
    input->UpdateBooleanComponent(inputs_[kB], false, 0.0);
    input->UpdateBooleanComponent(inputs_[kX], false, 0.0);
    input->UpdateBooleanComponent(inputs_[kY], false, 0.0);
    input->UpdateBooleanComponent(inputs_[kDpadUp], false, 0.0);
    input->UpdateBooleanComponent(inputs_[kDpadRight], false, 0.0);
    input->UpdateBooleanComponent(inputs_[kDpadDown], false, 0.0);
    input->UpdateBooleanComponent(inputs_[kDpadLeft], false, 0.0);
    input->UpdateBooleanComponent(inputs_[kBumper], false, 0.0);
    input->UpdateScalarComponent(inputs_[kTriggerValue], trigger_value, 0.0);
    input->UpdateBooleanComponent(inputs_[kTriggerClick], auto_select, 0.0);
    input->UpdateScalarComponent(inputs_[kGripValue], 0.0f, 0.0);
    input->UpdateBooleanComponent(inputs_[kGripClick], false, 0.0);
    input->UpdateScalarComponent(inputs_[kThumbstickX], 0.0f, 0.0);
    input->UpdateScalarComponent(inputs_[kThumbstickY], 0.0f, 0.0);
    input->UpdateBooleanComponent(inputs_[kThumbstickClick], false, 0.0);
    input->UpdateBooleanComponent(inputs_[kThumbrestTouch], false, 0.0);
    input->UpdateScalarComponent(inputs_[kTrackpadX], 0.0f, 0.0);
    input->UpdateScalarComponent(inputs_[kTrackpadY], 0.0f, 0.0);
    input->UpdateBooleanComponent(inputs_[kTrackpadClick], false, 0.0);
  }

 private:
  vr::ETrackedControllerRole role_;
  std::string serial_;
  std::string icon_prefix_;
  vr::TrackedDeviceIndex_t index_ = vr::k_unTrackedDeviceIndexInvalid;
  bool active_ = false;
  std::array<vr::VRInputComponentHandle_t, kInputCount> inputs_{};
  bool auto_select_ = false;
  uint32_t frame_count_ = 0;
};

class FakeProvider final : public vr::IServerTrackedDeviceProvider {
 public:
  vr::EVRInitError Init(vr::IVRDriverContext *context) override {
    VR_INIT_SERVER_DRIVER_CONTEXT(context);
    const char *controllers_only =
        std::getenv("STEARLIGHT_FAKE_CONTROLLERS_ONLY");
    controllers_only_ = controllers_only && std::atoi(controllers_only) != 0;
    const char *redirect = std::getenv("STEARLIGHT_FAKE_DISPLAY_REDIRECT");
    redirect_enabled_ = redirect && std::atoi(redirect) != 0;
    char init_message[256];
    std::snprintf(init_message, sizeof(init_message),
                  "svrt-fake: Init controllers_only=%d redirect=%d env=%s",
                  controllers_only_ ? 1 : 0,
                  redirect_enabled_ ? 1 : 0,
                  controllers_only ? controllers_only : "<unset>");
    if (vr::VRDriverLog()) vr::VRDriverLog()->Log(init_message);
    if (!controllers_only_) {
      hmd_ = std::make_unique<FakeHmd>();
      if (redirect_enabled_) redirect_ = std::make_unique<FakeDisplayRedirect>();
    }
    left_ = std::make_unique<FakeController>(
        vr::TrackedControllerRole_LeftHand);
    right_ = std::make_unique<FakeController>(
        vr::TrackedControllerRole_RightHand);
    const bool hmd_registered = controllers_only_ ||
        vr::VRServerDriverHost()->TrackedDeviceAdded(
            hmd_->serial(), vr::TrackedDeviceClass_HMD, hmd_.get());
    const bool redirect_registered = controllers_only_ || !redirect_enabled_ ||
        (redirect_ && vr::VRServerDriverHost()->TrackedDeviceAdded(
            redirect_->serial(), vr::TrackedDeviceClass_DisplayRedirect,
            redirect_.get()));
    const bool hmd_added = hmd_registered && redirect_registered;
    char registration_message[256];
    std::snprintf(registration_message, sizeof(registration_message),
                  "svrt-fake: registration hmd=%d redirect=%d",
                  hmd_registered ? 1 : 0, redirect_registered ? 1 : 0);
    if (vr::VRDriverLog()) vr::VRDriverLog()->Log(registration_message);
    if (!hmd_added || !vr::VRServerDriverHost()->TrackedDeviceAdded(
            left_->serial(), vr::TrackedDeviceClass_Controller, left_.get()) ||
        !vr::VRServerDriverHost()->TrackedDeviceAdded(
            right_->serial(), vr::TrackedDeviceClass_Controller, right_.get())) {
      hmd_.reset();
      redirect_.reset();
      left_.reset();
      right_.reset();
      VR_CLEANUP_SERVER_DRIVER_CONTEXT();
      return vr::VRInitError_Driver_Unknown;
    }
    return vr::VRInitError_None;
  }

  void Cleanup() override {
    if (hmd_) hmd_->Deactivate();
    if (redirect_) redirect_->Deactivate();
    if (left_) left_->Deactivate();
    if (right_) right_->Deactivate();
    hmd_.reset();
    redirect_.reset();
    left_.reset();
    right_.reset();
    VR_CLEANUP_SERVER_DRIVER_CONTEXT();
  }

  const char *const *GetInterfaceVersions() override {
    return vr::k_InterfaceVersions;
  }

  void RunFrame() override {
    if (hmd_) hmd_->RunFrame();
    if (left_) left_->RunFrame();
    if (right_) right_->RunFrame();
  }

  bool ShouldBlockStandbyMode() override { return false; }
  void EnterStandby() override {}
  void LeaveStandby() override {}

 private:
  std::unique_ptr<FakeHmd> hmd_;
  std::unique_ptr<FakeDisplayRedirect> redirect_;
  std::unique_ptr<FakeController> left_;
  std::unique_ptr<FakeController> right_;
  bool controllers_only_ = false;
  bool redirect_enabled_ = false;
};

FakeProvider provider;

}  // namespace

extern "C" __attribute__((visibility("default"))) void *HmdDriverFactory(
    const char *name, int *error) {
  if (name && !std::strcmp(name, vr::IServerTrackedDeviceProvider_Version))
    return &provider;
  if (error) *error = vr::VRInitError_Init_InterfaceNotFound;
  return nullptr;
}
