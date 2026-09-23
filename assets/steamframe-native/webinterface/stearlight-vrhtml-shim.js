/*
 * Valve's dashboard bundle is authored for vrwebhelper, where VRHTML is
 * injected by the native SteamVR host.  Stearlight supplies the same host
 * contract from the shell/browser bridge.  This file is deliberately a
 * compatibility layer only: it does not draw dashboard controls or replace
 * Valve's React components.
 */
(function () {
  "use strict";

  if (window.VRHTML) return;

  const emptyHandle = { unregister: function () {} };
  const noOp = function () { return undefined; };
  const noOpPromise = function () { return Promise.resolve(undefined); };

  const properties = {
    GetBoolProperty: function () { return false; },
    GetFloatProperty: function () { return 0; },
    GetInt32Property: function () { return 0; },
    GetStringProperty: function () { return ""; },
    GetUint64Property: function () { return 0; }
  };

  const settings = {
    Get: function () { return undefined; },
    Set: noOp,
    Sync: noOp,
    GetString: function () { return ""; },
    SetString: noOp,
    GetBool: function () { return false; },
    SetBool: noOp,
    GetInt32: function () { return 0; },
    SetInt32: noOp,
    GetFloat: function () { return 0; },
    SetFloat: noOp
  };

  const tracked = {
    GetTrackedDeviceClasses: function () { return []; },
    GetTrackedDeviceClass: function () { return 0; },
    GetSortedTrackedDeviceIndicesOfClass: function () { return []; },
    GetRecommendedRenderTargetSize: function () { return { width: 1280, height: 800 }; },
    GetTrackedDeviceIndexForControllerRole: function () { return -1; },
    IsDevicePathValid: function () { return false; },
    StartTrackingRecording: noOp,
    StopTrackingRecording: noOp,
    SetDebugCameraViewEnabled: noOp
  };

  const overlay = {
    ThisOverlayKey: function () { return "stearlight.dashboard"; },
    ThisOverlayHandle: function () { return 1; },
    SetFlag: noOp,
    SetInputMethod: noOp,
    GetPrimaryDashboardDevice: function () { return -1; },
    HasOverlayHandle: function () { return false; },
    IsOverlayVisible: function () { return false; },
    SetOverlayWidthInMeters: noOp,
    SetOverlayTransformAbsolute: noOp,
    SetOverlayTransformTrackedDeviceRelative: noOp,
    SetOverlayTransformOverlayRelative: noOp,
    SetWidthInMeters: noOp,
    GetOverlayMouseScale: function () { return 1; },
    GetVisibleTimeInSeconds: function () { return 0; },
    TriggerOverlayHapticEffect: noOp,
    ShowKeyboardForOverlay: noOp,
    HideKeyboard: noOp,
    SetKeyboardPositionForOverlay: noOp,
    ShowOverlay: noOp,
    HideOverlay: noOp,
    CloseKeyboard: noOp
  };

  const pathProperties = {
    GetBoolPathProperty: function () { return false; },
    GetFloatPathProperty: function () { return 0; },
    GetInt32PathProperty: function () { return 0; },
    GetStringPathProperty: function () { return ""; },
    GetDoublePathProperty: function () { return 0; },
    SetBoolPathProperty: noOp,
    SetFloatPathProperty: noOp,
    SetInt32PathProperty: noOp,
    SetStringPathProperty: noOp,
    SetDoublePathProperty: noOp,
    RegisterForBoolPathPropertyChangedEvents: function () { return emptyHandle; },
    RegisterForFloatPathPropertyChangedEvents: function () { return emptyHandle; },
    RegisterForInt32PathPropertyChangedEvents: function () { return emptyHandle; },
    RegisterForStringPathPropertyChangedEvents: function () { return emptyHandle; }
  };

  const applications = {
    GetCurrentSceneProcessId: function () { return 0; },
    GetSceneApplicationKey: function () { return ""; },
    GetSceneApplicationState: function () { return 0; },
    GetStartingApplicationKey: function () { return ""; },
    GetApplicationKeyByProcessId: function () { return ""; },
    LaunchApplication: noOp,
    QuitApplication: noOp
  };

  const audioDevices = {
    GetMasterMicrophoneVolume: function () { return 1; },
    GetMasterSoundVolume: function () { return 1; },
    GetMirrorSoundVolume: function () { return 1; },
    IsMasterMicrophoneMute: function () { return false; },
    IsMasterSoundVolumeMute: function () { return false; },
    IsMirrorSoundVolumeMute: function () { return false; },
    SetMasterMicrophoneMute: noOp,
    SetMasterMicrophoneVolume: noOp,
    SetMasterSoundVolume: noOp,
    SetMasterSoundVolumeMute: noOp,
    SetMirrorSoundVolume: noOp,
    SetMirrorSoundVolumeMute: noOp
  };

  const dashboardManager = {
    HasMessageOverlay: function () { return false; },
    CloseKeyboard: noOp,
    GetDashboardScale: function () { return 1; },
    GetNightModeEnabled: function () { return false; },
    SetNightModeEnabled: noOp,
    ShouldShowTrackerUI: function () { return false; },
    GetOverlayImage: function () { return ""; },
    HideDashboard: noOp,
    SendOverlayClosed: noOp,
    GetKeyboardBuffer: function () { return ""; },
    SendKeyboardDone: noOp,
    SetKeyboardOverlayToThis: noOp
  };

  const client = {
    GetExternalDashboardDisabled: function () { return false; },
    GetGraphicsHardwareVendorInfo: function () { return "generic"; },
    ShowDashboardOverlay: noOp,
    PostDashboardActivatedEvent: noOp
  };

  const chaperone = {
    ForceBoundsVisible: noOp,
    GetPlayAreaSize: function () { return { x: 0, z: 0 }; },
    ResetZeroPose: noOp
  };

  const recursive = new Proxy(noOp, {
    get: function (_target, key) {
      if (key === "then") return undefined;
      if (key === "length") return 0;
      if (key === "unregister") return noOp;
      return recursive;
    },
    apply: function () { return undefined; }
  });

  /* Valve's bundle grows optional VRHTML calls between SteamVR releases.
     Preserve the real methods above, while making an unknown method a safe
     no-op instead of crashing the React tree when a host feature is absent. */
  const withFallback = function (object) {
    return new Proxy(object, {
      get: function (target, key) {
        if (key in target) return target[key];
        if (key === "then") return undefined;
        return recursive;
      }
    });
  };

  /* The standalone preview has no vrwebhelper websocket endpoint.  Keep the
     Valve bundle mounted and interactive without reconnecting forever; the
     real SteamVR host never sets this flag and therefore keeps its native
     websocket implementation. */
  if (window.STEARLIGHT_DOCK_PREVIEW && window.WebSocket) {
    const PreviewWebSocket = function () {
      this.readyState = 3;
      this.bufferedAmount = 0;
      this.protocol = "";
      this.extensions = "";
      this.url = "";
      this.onopen = null;
      this.onclose = null;
      this.onerror = null;
      this.onmessage = null;
    };
    PreviewWebSocket.CONNECTING = 0;
    PreviewWebSocket.OPEN = 1;
    PreviewWebSocket.CLOSING = 2;
    PreviewWebSocket.CLOSED = 3;
    PreviewWebSocket.prototype.addEventListener = function () {};
    PreviewWebSocket.prototype.removeEventListener = function () {};
    PreviewWebSocket.prototype.send = noOp;
    PreviewWebSocket.prototype.close = noOp;
    window.WebSocket = PreviewWebSocket;
  }

  window.VRHTML = new Proxy({
    Environment: function () { return 2; }, // VRHTML.Environment.Overlay
    GetSteamLanguage: function () { return "english"; },
    GetSystemLocale: function () { return "en_US"; },
    SteamVRVersion: function () { return "stearlight"; },
    HasHMD: function () { return false; },
    IsSteamFrame: function () { return true; },
    BSupportsMultitaskingView: function () { return false; },
    SceneAppRunning: function () { return false; },
    GetTrackedDeviceClasses: tracked.GetTrackedDeviceClasses,
    RegisterForDeviceEvents: function () { return emptyHandle; },
    RegisterForProgressIndicatorEvents: function () { return emptyHandle; },
    RegisterForDevicePoseEvents: function () { return emptyHandle; },
    RegisterForInputFocusEvents: function () { return emptyHandle; },
    VRSystem: withFallback(tracked),
    VRProperties: withFallback(properties),
    VRSettings: withFallback(settings),
    VRPathProperties: withFallback(pathProperties),
    VROverlay: withFallback(overlay),
    VROverlayInternal: withFallback({
      HasCameraRoomViewCapability: function () { return false; },
      GetPrimaryDashboardDevice: function () { return -1; }
    }),
    VRApplications: withFallback(applications),
    VRAudioDevices: withFallback(audioDevices),
    VRClient: withFallback(client),
    VRChaperone: withFallback(chaperone),
    VRDashboardManager: withFallback(dashboardManager),
    VRCompositor: recursive,
    VRInput: recursive,
    VRRenderModels: recursive,
    VRRenderModelsInternal: recursive,
    VRChaperoneSetup: recursive,
    MonitorController: recursive,
    VRLink: recursive,
    VRMonitor: recursive,
    VRKeyboard: recursive,
    VRUtil: recursive,
    VRTrackedCameraInternal: recursive,
    XRApiLayerManager: recursive,
    XRRuntimeManager: recursive,
    Mailbox: recursive,
    TurnOffVRController: noOp,
    QuitSceneApp: noOp,
    QuitSteamVR: noOp,
    SystemShutdownMachine: noOp,
    OpenDashboardOverlay: noOp,
    SendDashboardShownEvent: noOp,
    SendDashboardHiddenEvent: noOp,
    GetWebHelperURL: noOpPromise
  }, {
    get: function (target, key) {
      if (key in target) return target[key];
      return recursive;
    }
  });
})();
