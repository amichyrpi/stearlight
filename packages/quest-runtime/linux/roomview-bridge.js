// Platform adapter: Valve owns the dock button and room-view state.
// Camera frames stay in Quest's OpenXR compositor.
(() => {
  const vr = globalThis.VRHTML;
  if (vr?.VRSettings && vr.RegisterForSystemBehaviorFlags && !vr.__stearlightGamepadBridge) {
    vr.__stearlightGamepadBridge = true;
    const registerMode = vr.RegisterForSystemBehaviorFlags.bind(vr);
    let previous;
    vr.RegisterForSystemBehaviorFlags = callback => registerMode(flags => {
      const mode = flags & 2560;
      if (mode !== previous) {
        try {
          vr.VRSettings.Set("stearlight_quest", "gamepad_left", !!(flags & 512));
          vr.VRSettings.Set("stearlight_quest", "gamepad_right", !!(flags & 2048));
          previous = mode;
        } catch (error) {
          console.warn("Quest gamepad bridge:", error);
        }
      }
      return callback(flags);
    });
  }
  if (!vr?.VRSettings || !vr.RegisterForRoomViewChangedEvents ||
      vr.__stearlightRoomViewBridge) return;
  vr.__stearlightRoomViewBridge = true;
  const register = vr.RegisterForRoomViewChangedEvents.bind(vr);
  vr.RegisterForRoomViewChangedEvents = callback => register((showing, enabled) => {
    try {
      // Mirror native room view for the Quest compositor. Floor visibility
      // observes RoomViewStore without changing Valve's floor preferences.
      vr.VRSettings.Set("stearlight_quest", "passthrough_enabled", !!(showing || enabled));
    } catch (error) {
      console.warn("Quest room-view bridge:", error);
    }
    return callback(showing, enabled);
  });
})();
