// Platform adapter: Valve owns the dock button and room-view state.
// Camera frames stay in Quest's OpenXR compositor.
(() => {
  const vr = globalThis.VRHTML;
  if (!vr?.VRSettings || !vr.RegisterForRoomViewChangedEvents ||
      vr.__stearlightRoomViewBridge) return;
  vr.__stearlightRoomViewBridge = true;
  let roomView = false, savedFloor;
  const updateConstruct = () => {
    const construct = globalThis.ConstructStore;
    if (!construct) return false;
    if (roomView && savedFloor === undefined) savedFloor = construct.m_bShowFloor;
    if (roomView) construct.m_bShowFloor = false;
    else if (savedFloor !== undefined) {
      construct.m_bShowFloor = savedFloor;
      savedFloor = undefined;
    }
    return true;
  };
  const register = vr.RegisterForRoomViewChangedEvents.bind(vr);
  vr.RegisterForRoomViewChangedEvents = callback => register((showing, enabled) => {
    try {
      roomView = !!(showing || enabled);
      vr.VRSettings.Set("stearlight_quest", "passthrough_enabled", roomView);
      updateConstruct();
    } catch (error) {
      console.warn("Quest room-view bridge:", error);
    }
    return callback(showing, enabled);
  });
  // The native stores are initialized after this adapter is loaded.
  if (typeof setInterval === "function") {
    const pending = setInterval(() => {
      if (updateConstruct()) clearInterval(pending);
    }, 250);
  }
})();
