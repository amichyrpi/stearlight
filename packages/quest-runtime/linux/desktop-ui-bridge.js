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
  // Frame's facet dashboard switches through SteamClient rather than the
  // legacy IVROverlay ShowDashboard path. Also raise an existing desktop
  // when its native launcher is selected again from the + menu.
  let desktopFocus = '';
  let checkingFocus = false;
  setInterval(async () => {
    const overlay = globalThis.SteamClient?.OpenVR?.VROverlay;
    if (!overlay || checkingFocus) return;
    checkingFocus = true;
    try {
      const result = await call('status');
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
      if (typeof factory !== "function" || !factory.toString().includes("SteamOSManager.SetUserPassword#1")) continue;
      chunk[1][id] = function (module, exports, require) {
        factory(module, exports, require);
        for (const api of Object.values(module.exports || {})) {
          if (api && typeof api === "object" && api.GetState && api.SetUserPassword && api.SwitchToDesktop) install(api);
        }
      };
    }
    return push(chunk);
  };
})();
