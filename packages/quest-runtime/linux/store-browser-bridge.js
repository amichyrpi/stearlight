// STEARLIGHT_QUEST_STORE_ORIGIN_BRIDGE
// CEF LoadRequest rejects an initiator origin the browser has never visited.
// Start Valve's VR Store browser on its real origin rather than a data page.
(() => {
  const views = globalThis.SteamClient?.BrowserView;
  if (!views?.Create) return;
  const create = views.Create;
  views.Create = function(options) {
    if (options?.strName === "MainBrowser" &&
        options.strVROverlayKey?.startsWith("valve.steam.browserview_MainBrowser_")) {
      options = { ...options, strInitialURL: "https://store.steampowered.com/" };
    }
    return create.call(this, options);
  };
})();
