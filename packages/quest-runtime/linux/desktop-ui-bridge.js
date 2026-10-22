// Stearlight supplies the desktop operations through Valve's existing controls.
// This code is injected only into Steam's bundled UI, with an app-private token.
(() => {
  const token = __STEARLIGHT_DESKTOP_TOKEN__;
  const chunks = globalThis.webpackChunksteamui = globalThis.webpackChunksteamui || [];
  const push = chunks.push.bind(chunks);
  async function call(operation, fields = {}) {
    const response = await fetch("http://127.0.0.1:27771/", {
      method: "POST",
      headers: { "Content-Type": "application/json", "Authorization": "Bearer " + token },
      body: JSON.stringify({ operation, ...fields }),
    });
    if (!response.ok) throw new Error("Desktop service unavailable");
    return response.json();
  }
  // Notify the existing owner before expensive UI work. The native owner
  // shares a bounded boost budget with game and Plasma startup.
  if (!globalThis.__stearlightInteractionBoost) {
    globalThis.__stearlightInteractionBoost = true;
    let nextBoost = 0;
    const boost = () => {
      if (document.hidden || performance.now() < nextBoost) return;
      nextBoost = performance.now() + 750;
      call('performance-boost').catch(() => {});
    };
    for (const name of ['click', 'keydown', 'focusin']) document.addEventListener(name, boost, true);
    for (const name of ['popstate', 'hashchange']) globalThis.addEventListener(name, boost);
    document.addEventListener('visibilitychange', boost);
    for (const name of ['pushState', 'replaceState']) {
      const original = history[name];
      history[name] = function (...args) {
        if (args[2] != null && new URL(args[2], location.href).href !== location.href) boost();
        return original.apply(this, args);
      };
    }
  }
  // Frame's facet dashboard switches through SteamClient rather than the
  // legacy IVROverlay ShowDashboard path. Also raise an existing desktop
  // when its native launcher is selected again from the + menu.
  let desktopFocus = '';
  let checkingFocus = false;
  let updateStatus;
  let systemInfoStore, reportedFrameVersion;
  const stableID = 2147483600, betaID = 2147483601;
  function synchronizeUpdates(state) {
    if (!state) return;
    updateStatus = state;
    const settings = globalThis.settingsStore?.settings;
    const client = globalThis.SteamClient;
    if (!settings || !client?.Settings) return;
    // A recovery snapshot can lack account-provided beta entries altogether.
    // These local IDs map only to our public-feed selector, never to Valve IDs.
    const choices = settings.vecAvailableClientBetas || [];
    if (!choices.some(x => /deckard steam|steam frame|local/i.test(x.strName) &&
        x.nBetaID !== stableID && x.nBetaID !== betaID)) {
      if (!choices.some(x => x.nBetaID === stableID)) settings.vecAvailableClientBetas = [
        ...choices,
        { nBetaID: stableID, strName: 'Steam Frame Hotfix' },
        { nBetaID: betaID, strName: 'Steam Frame Hotfix Preview' },
      ];
      settings.nAvailableBetas = settings.vecAvailableClientBetas.length;
      settings.bChangeBetaEnabled = true;
      settings.nSelectedBetaID = state.client_channel === 'beta' ? betaID : stableID;
      settings.bIsInClientBeta = state.client_channel === 'beta';
      settings.strCurrentBetaName = state.client_channel === 'beta' ? 'Steam Frame Hotfix Preview' : 'Steam Frame Hotfix';
    }
    if (!client.Settings.__stearlightChannels) {
      client.Settings.__stearlightChannels = true;
      const select = client.Settings.SelectClientBeta.bind(client.Settings);
      client.Settings.SelectClientBeta = async id => {
        if (id !== stableID && id !== betaID) return select(id);
        const result = await call('client-channel', { channel: id === betaID ? 'beta' : 'rel' });
        if (!result.ok) throw new Error('Cannot change Steam client channel');
        synchronizeUpdates(result.value);
        client.User.StartRestart(false);
      };
    }
    if (client.System?.GetSystemInfo && !client.System.__stearlightFrameVersion) {
      client.System.__stearlightFrameVersion = true;
      const info = client.System.GetSystemInfo.bind(client.System);
      client.System.GetSystemInfo = async (...args) => {
        const result = await info(...args);
        if (!updateStatus?.frame_version) return result;
        return { ...result, sOSName: 'Stearlight OS (Steam Frame runtime)',
          sOSVersionId: updateStatus.frame_version, sOSBuildId: updateStatus.frame_build,
          sOSCodename: 'holo', sOSVariantId: 'vr' };
      };
    }
    const version = state.frame_build;
    if (version && systemInfoStore && version !== reportedFrameVersion) {
      reportedFrameVersion = version;
      systemInfoStore.UpdateSystemInfo().catch(() => { reportedFrameVersion = undefined; });
    }
  }
  setInterval(async () => {
    const overlay = globalThis.SteamClient?.OpenVR?.VROverlay;
    if (!overlay || checkingFocus) return;
    checkingFocus = true;
    try {
      const result = await call('status');
      if (result.ok) synchronizeUpdates(result.value.updates);
      if (result.ok) globalThis.__stearlightFrameMenusStatus?.(result.value.frame);
      if (result.ok && result.value.desktop_running && result.value.focus_id &&
          result.value.focus_id !== desktopFocus) {
        desktopFocus = result.value.focus_id;
        setTimeout(() => overlay.ShowDashboard('stearlight.plasma.desktop'), 300);
      }
    } catch {} finally { checkingFocus = false; }
  }, 1000);
  function reply(type, body, success) {
    const value = type.fromObject(body);
    return { Body: () => value, BSuccess: () => success, GetEResult: () => success ? 1 : 2 };
  }
  function install(api) {
    if (api.__stearlightDesktop) return;
    api.__stearlightDesktop = true;
    const originalState = api.GetState;
    api.GetState = api.SendMsgGetState = async (...args) => {
      const native = await originalState(...args);
      let status;
      try { status = await call("status"); } catch { return native; }
      if (!status.ok) return native;
      const body = native.Body().toObject();
      Object.assign(body.state, {
        is_password_change_supported: true,
        is_session_management_supported: status.value.desktop_available,
        desktop_sessions_available: status.value.desktop_available ? ["plasma"] : [],
        default_desktop_session: "plasma",
      });
      if (status.value.updates?.frame_version) body.state.os_version = status.value.updates.frame_version;
      const value = api.GetStateHandler.response.fromObject(body);
      native.Body = () => value;
      return native;
    };
    for (const [name, operation, body] of [
      ["GetUserHasPassword", "status", result => ({ has_password: result.value?.has_password || false })],
      ["SetUserPassword", "password", () => ({})],
      ["SwitchToDesktop", "start", () => ({})],
      ["SetDefaultDesktopSession", "session", () => ({})],
    ]) {
      const wrapped = async fields => {
        const args = fields?.toObject ? fields.toObject() : fields || {};
        let result;
        try { result = await call(operation, args); } catch { result = { ok: false }; }
        return reply(api[name + "Handler"].response, body(result), result.ok);
      };
      api[name] = api["SendMsg" + name] = wrapped;
    }
    const settings = globalThis.SteamClient?.Settings;
    if (settings && !settings.__stearlightDesktop) {
      settings.__stearlightDesktop = true;
      const set = settings.SetSetting.bind(settings);
      const synchronize = () => {
        const enabled = globalThis.settingsStore?.clientSettings?.developer_mode_enabled;
        if (typeof enabled === "boolean") call("developer", { enabled }).catch(() => {});
      };
      settings.SetSetting = (...args) => {
        const result = set(...args);
        setTimeout(synchronize, 200);
        return result;
      };
      setTimeout(synchronize, 3000);
    }
  }
  chunks.push = function (chunk) {
    for (const [id, factory] of Object.entries(chunk[1] || {})) {
      if (typeof factory !== "function") continue;
      const text = factory.toString();
      const desktopAPI = text.includes("SteamOSManager.SetUserPassword#1");
      const systemInfo = text.includes("Updated Steam Version Info:");
      if (!desktopAPI && !systemInfo) continue;
      chunk[1][id] = function (module, exports, require) {
        factory(module, exports, require);
        for (const api of Object.values(module.exports || {})) {
          if (desktopAPI && api && typeof api === "object" && api.GetState && api.SetUserPassword && api.SwitchToDesktop) install(api);
          if (systemInfo && api && typeof api === "object" && api.UpdateSystemInfo) {
            systemInfoStore = api;
            if (updateStatus) synchronizeUpdates(updateStatus);
          }
        }
      };
    }
    return push(chunk);
  };
})();
