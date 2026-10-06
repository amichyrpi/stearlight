// VM-only OpenVR server driver for QEMU, which has no physical headset or
// controllers. It supplies a virtual HMD and two deterministic Steam Frame
// controllers so vrserver, vrdashboard, input bindings, and the laser cursor
// use Valve's native runtime path.

#include <openvr_driver.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace {

// QEMU has no physical headset. SteamVR owns one stereo desktop backbuffer;
// this driver supplies geometry and poses, with no independent eye renderers.
uint32_t EyeWidth() {
  const char *low = std::getenv("STEARLIGHT_VM_LOW_RESOLUTION");
  return low && !std::strcmp(low, "1") ? 720 : 1440;
}
uint32_t EyeHeight() { return EyeWidth() * 10 / 9; }
constexpr float kIpdMeters = 0.063f;
// The pinned Frame dashboard sits about 1.13 m in front of this HMD.
// A 2 m aiming plane compresses the mouse's reachable area inside the panel.
constexpr double kPointerPlaneMeters = 1.15;

// Read QEMU's test devices without grabbing them. SteamVR still owns laser
// rendering, hit testing, click stabilization, and every dashboard action.
class VmInput {
 public:
  void Open() {
    Close();
    for (unsigned i = 0; i < devices_.size(); ++i) {
      char path[64], name[128]{};
      std::snprintf(path, sizeof(path), "/dev/input/event%u", i);
      int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
      if (fd < 0) continue;
      if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) < 0 ||
          !std::strstr(name, "QEMU")) {
        close(fd);
        continue;
      }
      devices_[i].fd = fd;
      ioctl(fd, EVIOCGABS(ABS_X), &devices_[i].x);
      ioctl(fd, EVIOCGABS(ABS_Y), &devices_[i].y);
    }
  }

  void Close() {
    for (auto &device : devices_) {
      if (device.fd >= 0) close(device.fd);
      device.fd = -1;
    }
    keys_.fill(false);
  }

  void Read() {
    for (auto &device : devices_) {
      if (device.fd < 0) continue;
      input_event events[32];
      ssize_t bytes;
      while ((bytes = read(device.fd, events, sizeof(events))) > 0) {
        for (size_t i = 0; i < size_t(bytes) / sizeof(input_event); ++i) {
          const auto &event = events[i];
          if (event.type == EV_SYN && event.code == SYN_DROPPED) {
            keys_.fill(false);
          } else if (event.type == EV_KEY && event.code < keys_.size()) {
            keys_[event.code] = event.value != 0;
          } else if (event.type == EV_ABS) {
            const auto &range = event.code == ABS_X ? device.x : device.y;
            if ((event.code == ABS_X || event.code == ABS_Y) &&
                range.maximum > range.minimum) {
              double value = double(event.value - range.minimum) /
                             (range.maximum - range.minimum);
              value = std::fmax(0.0, std::fmin(value, 1.0));
              if (event.code == ABS_X) pointer_x = value;
              else pointer_y = value;
            }
          }
        }
      }
    }
  }

  bool Down(unsigned code) const { return code < keys_.size() && keys_[code]; }
  std::atomic<double> pointer_x{0.25}, pointer_y{0.5};

 private:
  struct Device {
    int fd = -1;
    input_absinfo x{}, y{};
  };
  std::array<Device, 32> devices_{};
  std::array<bool, KEY_CNT> keys_{};
};

VmInput vm_input;

class FakeHmd final : public vr::ITrackedDeviceServerDriver,
                      public vr::IVRDisplayComponent {
 public:
  const char *serial() const { return "STEARLIGHT-FAKE-HMD"; }

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
        properties, vr::Prop_ResourceRoot_String, "stearlight-fake");
    vr::VRProperties()->SetStringProperty(
        properties, vr::Prop_AdditionalDeviceSettingsPath_String,
        "{stearlight-fake}/frame_hmd_additional.vrsettings");
    vr::VRProperties()->SetStringProperty(
        properties, vr::Prop_RegisteredDeviceType_String,
        "stearlight-fake/STEARLIGHT-FAKE-HMD");
    vr::VRProperties()->SetUint64Property(
        properties, vr::Prop_CurrentUniverseId_Uint64, 1);
    vr::VRProperties()->SetInt32Property(
        properties, vr::Prop_ExpectedControllerCount_Int32, 2);
    vr::VRProperties()->SetFloatProperty(
        properties, vr::Prop_UserIpdMeters_Float, kIpdMeters);
    vr::VRProperties()->SetFloatProperty(
        properties, vr::Prop_DisplayFrequency_Float, 60.0f);
    vr::VRProperties()->SetFloatProperty(
        properties, vr::Prop_SecondsFromVsyncToPhotons_Float, 0.011111f);
    // Match the VM scanout at 60 Hz. The physical Frame's panel calibration
    // and refresh rate cannot be supplied by an emulated desktop display.
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
        properties, vr::Prop_ReportsTimeSinceVSync_Bool, false);
    vr::VRProperties()->SetBoolProperty(
        properties, vr::Prop_DeviceIsWireless_Bool, true);
    vr::VRProperties()->SetBoolProperty(
        properties, vr::Prop_DeviceCanPowerOff_Bool, false);
    vr::VRProperties()->SetBoolProperty(
        properties, vr::Prop_NeverTracked_Bool, false);
    // The stationary test HMD represents a worn headset. Without proximity,
    // SteamVR enters standby during slow startup and hides Steam's panels.
    vr::VRProperties()->SetBoolProperty(
        properties, vr::Prop_ContainsProximitySensor_Bool, true);
    if (vr::VRDriverInput()->CreateBooleanComponent(
            properties, "/proximity", &proximity_) != vr::VRInputError_None)
      return vr::VRInitError_Driver_Failed;
    vr::VRDriverInput()->UpdateBooleanComponent(proximity_, true, 0.0);
    vr::HmdMatrix34_t left{};
    vr::HmdMatrix34_t right{};
    for (int axis = 0; axis < 3; ++axis) {
      left.m[axis][axis] = right.m[axis][axis] = 1.0f;
    }
    left.m[0][3] = -kIpdMeters / 2.0f;
    right.m[0][3] = kIpdMeters / 2.0f;
    vr::VRServerDriverHost()->SetDisplayEyeToHead(object_id, left, right);
    char geometry[160];
    std::snprintf(geometry, sizeof(geometry),
                  "stearlight-fake: stereo output %ux%u, eyes %ux%u, IPD 0.063m; "
                  "presentation timing owned by compositor",
                  2 * EyeWidth(), EyeHeight(), EyeWidth(), EyeHeight());
    vr::VRDriverLog()->Log(geometry);
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
                    "stearlight-fake: HMD GetComponent(%s)",
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
    if (width) *width = 2 * EyeWidth();
    if (height) *height = EyeHeight();
  }

  bool IsDisplayOnDesktop() override { return true; }

  bool IsDisplayRealDisplay() override { return false; }

  void GetRecommendedRenderTargetSize(uint32_t *width,
                                      uint32_t *height) override {
    if (width) *width = EyeWidth();
    if (height) *height = EyeHeight();
  }

  void GetEyeOutputViewport(vr::EVREye eye, uint32_t *x, uint32_t *y,
                            uint32_t *width, uint32_t *height) override {
    if (x) *x = eye == vr::Eye_Left ? 0 : EyeWidth();
    if (y) *y = 0;
    if (width) *width = EyeWidth();
    if (height) *height = EyeHeight();
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
    vr::VRDriverInput()->UpdateBooleanComponent(proximity_, true, 0.0);
    vr::VRServerDriverHost()->TrackedDevicePoseUpdated(
        index_, GetPose(), sizeof(vr::DriverPose_t));
  }

 private:
  vr::TrackedDeviceIndex_t index_ = vr::k_unTrackedDeviceIndexInvalid;
  vr::VRInputComponentHandle_t proximity_ = vr::k_ulInvalidInputComponentHandle;
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
                    ? "STEARLIGHT-FAKE-CONTROLLER-L"
                    : "STEARLIGHT-FAKE-CONTROLLER-R"),
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
    // first-run page. Keep automated selections opt-in: repeated clicks can
    // dismiss the dashboard or open login popups while Steam is initializing.
    const char *auto_select = std::getenv("STEARLIGHT_FAKE_AUTO_SELECT");
    auto_select_ = auto_select && std::atoi(auto_select) != 0;
    const char *open_dashboard = std::getenv("STEARLIGHT_FAKE_OPEN_DASHBOARD");
    open_dashboard_ = auto_select_ ||
                      (open_dashboard && std::atoi(open_dashboard) != 0);
    if (vr::VRDriverLog()) {
      char message[128];
      std::snprintf(message, sizeof(message),
                    "stearlight fake controller %s auto_select=%d open_dashboard=%d",
                    serial_.c_str(), auto_select_ ? 1 : 0,
                    open_dashboard_ ? 1 : 0);
      vr::VRDriverLog()->Log(message);
      std::snprintf(message, sizeof(message),
                    "stearlight fake controller %s system_handle=%llu a_handle=%llu",
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
    if (role_ == vr::TrackedControllerRole_RightHand) {
      // Either stereo eye can be used to aim. Convert its desktop position
      // to the HMD's 90-degree view and aim the virtual hand along that ray.
      double eye_x = vm_input.pointer_x * 2.0;
      if (eye_x >= 1.0) eye_x -= 1.0;
      double dx = (eye_x * 2.0 - 1.0) * kPointerPlaneMeters - pose.vecPosition[0];
      double dy = 1.6 + (1.0 - vm_input.pointer_y * 2.0) * kPointerPlaneMeters -
                  pose.vecPosition[1];
      double dz = -kPointerPlaneMeters - pose.vecPosition[2];
      double length = std::sqrt(dx * dx + dy * dy + dz * dz);
      dx /= length;
      dy /= length;
      dz /= length;
      double norm = std::sqrt(dy * dy + dx * dx + (1.0 - dz) * (1.0 - dz));
      pose.qRotation = {(1.0 - dz) / norm, dy / norm, -dx / norm, 0.0};
    }
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
    // laser. Only the right controller pulses so the two mirrored system
    // bindings cannot toggle the dashboard twice.
    const bool right_controller = role_ == vr::TrackedControllerRole_RightHand;
    const bool dashboard_toggle = right_controller &&
        (vm_input.Down(KEY_F1) || (open_dashboard_ &&
         frame_count_ >= 180 && frame_count_ < 240));
    const vr::EVRInputError system_error = input->UpdateBooleanComponent(
        inputs_[kSystem], dashboard_toggle, 0.0);
    input->UpdateBooleanComponent(inputs_[kMenu], false, 0.0);
    input->UpdateBooleanComponent(inputs_[kView], false, 0.0);
    input->UpdateBooleanComponent(inputs_[kApplicationMenu], false, 0.0);
    // Steam Frame's onboarding page is a native dashboard surface and only
    // advances on a controller select. Startup can take more than a minute
    // in the VM while Steam's WebHelper is created, so pulse after the
    // surface is likely to exist and repeat at a bounded cadence. This is
    // enabled explicitly for onboarding diagnostics; ordinary VM sessions
    // open the dashboard once without generating selections.
    const bool auto_select =
        auto_select_ && right_controller && frame_count_ >= 600 &&
        ((frame_count_ - 600) % 600) < 60;
    const bool trigger = auto_select ||
                         (right_controller && vm_input.Down(BTN_LEFT));
    const bool grip = right_controller && vm_input.Down(KEY_LEFTSHIFT);
    const float trigger_value = trigger ? 1.0f : 0.0f;
    const vr::EVRInputError a_error =
        input->UpdateBooleanComponent(inputs_[kA], auto_select ||
            (right_controller && vm_input.Down(KEY_ENTER)), 0.0);
    if (dashboard_toggle && frame_count_ == 180 && vr::VRDriverLog()) {
      char message[160];
      std::snprintf(message, sizeof(message),
                    "stearlight fake controller pulsing dashboard system button system_error=%d a_error=%d",
                    static_cast<int>(system_error), static_cast<int>(a_error));
      vr::VRDriverLog()->Log(message);
    }
    input->UpdateBooleanComponent(inputs_[kB], right_controller &&
        (vm_input.Down(BTN_RIGHT) || vm_input.Down(KEY_ESC)), 0.0);
    input->UpdateBooleanComponent(inputs_[kX], false, 0.0);
    input->UpdateBooleanComponent(inputs_[kY], false, 0.0);
    input->UpdateBooleanComponent(inputs_[kDpadUp], right_controller && vm_input.Down(KEY_UP), 0.0);
    input->UpdateBooleanComponent(inputs_[kDpadRight], right_controller && vm_input.Down(KEY_RIGHT), 0.0);
    input->UpdateBooleanComponent(inputs_[kDpadDown], right_controller && vm_input.Down(KEY_DOWN), 0.0);
    input->UpdateBooleanComponent(inputs_[kDpadLeft], right_controller && vm_input.Down(KEY_LEFT), 0.0);
    input->UpdateBooleanComponent(inputs_[kBumper], false, 0.0);
    input->UpdateScalarComponent(inputs_[kTriggerValue], trigger_value, 0.0);
    input->UpdateBooleanComponent(inputs_[kTriggerClick], trigger, 0.0);
    input->UpdateScalarComponent(inputs_[kGripValue], grip ? 1.0f : 0.0f, 0.0);
    input->UpdateBooleanComponent(inputs_[kGripClick], grip, 0.0);
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
  bool open_dashboard_ = false;
  uint32_t frame_count_ = 0;
};

class FakeProvider final : public vr::IServerTrackedDeviceProvider {
 public:
  vr::EVRInitError Init(vr::IVRDriverContext *context) override {
    VR_INIT_SERVER_DRIVER_CONTEXT(context);
    vm_input.Open();
    hmd_ = std::make_unique<FakeHmd>();
    left_ = std::make_unique<FakeController>(
        vr::TrackedControllerRole_LeftHand);
    right_ = std::make_unique<FakeController>(
        vr::TrackedControllerRole_RightHand);

    const bool hmd_added = vr::VRServerDriverHost()->TrackedDeviceAdded(
        hmd_->serial(), vr::TrackedDeviceClass_HMD, hmd_.get());
    if (vr::VRDriverLog()) {
      char message[128];
      std::snprintf(message, sizeof(message),
                    "stearlight-fake: registration hmd=%d",
                    hmd_added ? 1 : 0);
      vr::VRDriverLog()->Log(message);
    }
    if (!hmd_added ||
        !vr::VRServerDriverHost()->TrackedDeviceAdded(
            left_->serial(), vr::TrackedDeviceClass_Controller, left_.get()) ||
        !vr::VRServerDriverHost()->TrackedDeviceAdded(
            right_->serial(), vr::TrackedDeviceClass_Controller, right_.get())) {
      hmd_.reset();
      left_.reset();
      right_.reset();
      vm_input.Close();
      VR_CLEANUP_SERVER_DRIVER_CONTEXT();
      return vr::VRInitError_Driver_Unknown;
    }
    return vr::VRInitError_None;
  }

  void Cleanup() override {
    if (hmd_) hmd_->Deactivate();
    if (left_) left_->Deactivate();
    if (right_) right_->Deactivate();
    hmd_.reset();
    left_.reset();
    right_.reset();
    vm_input.Close();
    VR_CLEANUP_SERVER_DRIVER_CONTEXT();
  }

  const char *const *GetInterfaceVersions() override {
    return vr::k_InterfaceVersions;
  }

  void RunFrame() override {
    vm_input.Read();
    if (hmd_) hmd_->RunFrame();
    if (left_) left_->RunFrame();
    if (right_) right_->RunFrame();
  }

  bool ShouldBlockStandbyMode() override { return false; }
  void EnterStandby() override {}
  void LeaveStandby() override {}

 private:
  std::unique_ptr<FakeHmd> hmd_;
  std::unique_ptr<FakeController> left_;
  std::unique_ptr<FakeController> right_;
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
