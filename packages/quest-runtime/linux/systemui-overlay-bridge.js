// CEF runs in one process on Quest. Its native overlay accessor can return the
// last browser's identity. Keep systemui textures and scene references scoped
// to Valve's actual system overlay, including move/resize and curved panels.
(() => {
  const overlay = globalThis.VRHTML?.VROverlay;
  if (globalThis.document?.title !== "systemui" || !overlay?.FindOverlay) return;
  overlay.ThisOverlayKey = () => "system.systemui";
  overlay.ThisOverlayHandle = () => overlay.FindOverlay("system.systemui");
})();
