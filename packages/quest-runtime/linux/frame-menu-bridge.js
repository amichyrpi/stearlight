// Feed Valve's existing Frame menus from actual desktop/runtime capabilities.
(() => {
  const token = __STEARLIGHT_DESKTOP_TOKEN__;
  const chunks = globalThis.webpackChunksteamui = globalThis.webpackChunksteamui || [];
  const push = chunks.push.bind(chunks), listeners = new Set();
  let state, State, Update, HMD, Windows, lastWindows = '', publishing = false;
  async function call(operation, fields = {}) {
    const response = await fetch('http://127.0.0.1:27771/', { method: 'POST',
      headers: { 'Content-Type': 'application/json', Authorization: 'Bearer ' + token },
      body: JSON.stringify({ operation, ...fields }) });
    if (!response.ok) throw new Error('Frame service unavailable');
    const result = await response.json();
    if (!result.ok) throw new Error(result.error || 'Frame operation failed');
    return result.value;
  }
  function emit(callback) {
    if (!State || !state) return;
    // Kernel clock/TDP/VRR controls are unavailable to the Android app.
    const value = State.fromObject({ limits: { disable_refresh_rate_management: true,
      cpu_governor_manual_min_mhz: 0, cpu_governor_manual_max_mhz: 0, tdp_limit_min: 0, tdp_limit_max: 0,
      is_manual_display_refresh_rate_available: false, is_vrr_supported: false,
      is_dynamic_refresh_rate_in_steam_supported: false,
      perf_overlay_is_standalone: true, fsr_sharpness_min: 0, fsr_sharpness_max: 20,
      split_scaling_filters_available: [1, 2, 3], split_scaling_scalers_available: [1, 2, 3, 4, 5] },
      current_game_id: '0', active_profile_game_id: '0',
      settings: { global: { is_advanced_settings_enabled: state.advanced,
        perf_overlay_level: state.overlay, perf_overlay_service_state: state.overlay ? 2 : 1 }, per_app: { split_scaling_filter: state.filter,
        split_scaling_scaler: state.scaler, fsr_sharpness: state.sharpness } } }).serializeBase64String();
    for (const fn of callback ? [callback] : listeners) fn(value);
    const store = globalThis.SystemPerfStore;
    if (!callback && store?.OnStateChanged && !listeners.has(store.OnStateChanged)) store.OnStateChanged(value);
  }
  async function publishPaths() {
    const api = globalThis.SteamClient?.OpenVR?.PathProperties;
    if (!api || publishing || !state) return;
    publishing = true;
    try {
      if (HMD && state.refresh_hz) {
        const path = HMD.getVRPathPropertyKey();
        const value = HMD.deserializeBinary(await api.GetStringPathProperty(path)).toObject();
        if (value.display_refresh_rate_value !== state.refresh_hz) {
          value.display_refresh_rate_value = state.refresh_hz;
          await api.SetStringPathProperty(path, HMD.fromObject(value).serializeBase64String());
        }
      }
      if (Windows) {
        const key = JSON.stringify(state.windows || []);
        if (key !== lastWindows) {
          const path = Windows.getVRPathPropertyKey();
          const value = Windows.deserializeBinary(await api.GetStringPathProperty(path)).toObject();
          value.windows = [...(value.windows || []).filter(w => w.window_id < 2100000000), ...(state.windows || [])];
          await api.SetStringPathProperty(path, Windows.fromObject(value).serializeBase64String());
          lastWindows = key;
        }
      }
    } catch (error) { console.warn('Frame menu state:', error); }
    finally { publishing = false; }
  }
  globalThis.__stearlightFrameMenusStatus = value => {
    if (!value) return;
    const changed = ['advanced', 'overlay', 'filter', 'scaler', 'sharpness'].some(key => state?.[key] !== value[key]);
    state = value;
    if (changed) emit();
    publishPaths();
    const profile = globalThis.settingsStore?.clientSettings?.steamos_platform_performance_profile;
    if ((profile === 'Performance' || profile === 'Powersave') && profile !== state.profile)
      call('frame-settings', { profile }).catch(console.warn);
  };
  const perf = globalThis.SteamClient?.System?.Perf;
  if (perf) {
    const register = perf.RegisterForStateChanges.bind(perf), update = perf.UpdateSettings.bind(perf);
    perf.RegisterForStateChanges = callback => {
      // The generic Linux daemon also emits placeholder PC limits. Keep those
      // events from replacing the Quest's supported controls and saved values.
      const native = register(() => emit(callback)); listeners.add(callback); emit(callback);
      return { unregister() { listeners.delete(callback); native?.unregister(); } };
    };
    perf.UpdateSettings = value => {
      if (!Update) return update(value);
      const delta = Update.deserializeBinary(value).toObject();
      const fields = {}, global = delta.settings_delta?.global || {}, app = delta.settings_delta?.per_app || {};
      for (const [source, key] of [['is_advanced_settings_enabled', 'advanced'], ['perf_overlay_level', 'overlay']])
        if (global[source] !== undefined) fields[key] = global[source];
      if (global.perf_overlay_service_state !== undefined)
        fields.overlay = global.perf_overlay_service_state === 2 ? state?.overlay || 4 : 0;
      for (const [source, key] of [['split_scaling_filter', 'filter'], ['split_scaling_scaler', 'scaler'], ['fsr_sharpness', 'sharpness']])
        if (app[source] !== undefined) fields[key] = app[source];
      if (delta.reset_to_default) fields.reset = true;
      if (!Object.keys(fields).length) return update(value);
      return call('frame-settings', fields)
        .then(value => { state = value; emit(); });
    };
  }
  const apps = globalThis.SteamClient?.Apps;
  if (apps) {
    const scan = apps.ScanForInstalledNonSteamApps.bind(apps), launch = apps.LaunchNonSteamApp.bind(apps);
    apps.ScanForInstalledNonSteamApps = async onlyVR => {
      const native = await scan(onlyVR);
      if (!onlyVR) return native;
      try {
        if (!state) state = (await call('status')).frame;
        return [...native, ...(state?.apps || []).map(app => ({ bIsApplication: true,
          strAppName: app.name, strExePath: 'stearlight-desktop-app', strArguments: '',
          strCmdline: 'stearlight-desktop-app:' + app.id }))];
      } catch { return native; }
    };
    apps.LaunchNonSteamApp = command => command.startsWith('stearlight-desktop-app:') ?
      call('launch-app', { id: command.slice('stearlight-desktop-app:'.length) }) : launch(command);
  }
  function installVR(api) {
    if (api.__stearlightWindows || !api.DashboardDesktopWindowClicked) return;
    api.__stearlightWindows = true;
    const original = api.DashboardDesktopWindowClicked;
    const wrapped = async fields => {
      const value = fields?.toObject ? fields.toObject() : fields;
      if (!state?.windows?.some(w => w.window_id === value?.window_id)) return original(fields);
      await call('focus-window', { window_id: value.window_id });
      // The dashboard awaits completion before closing its popup; no native
      // capture request is sent for a window owned by the nested compositor.
    };
    api.DashboardDesktopWindowClicked = api.SendMsgDashboardDesktopWindowClicked = wrapped;
  }
  function capture(exports) {
    for (const value of Object.values(exports || {})) {
      if (value?.DashboardDesktopWindowClicked) installVR(value);
      if (value?.SteamVR) installVR(value.SteamVR);
      if (typeof value !== 'function' || !value.M) continue;
      const fields = value.M().fields;
      if (fields.current_game_id && fields.limits) State = value;
      if (fields.settings_delta && fields.gameid) Update = value;
      if (value.getVRPathPropertyKey?.() === '/vrgamepadui/steamvr/hmd_settings') HMD = value;
      if (value.getVRPathPropertyKey?.() === '/vrgamepadui/steamvr/desktop_windows') Windows = value;
    }
  }
  function relevant(text) {
    return text.includes('CMsgSystemPerfState') || text.includes('CVRGamepadUIShared_PathProperty_HMDSettings') ||
      text.includes('m_SteamVR_ClientMethods');
  }
  chunks.push = function(chunk) {
    for (const [id, factory] of Object.entries(chunk[1] || {})) {
      if (typeof factory !== 'function') continue;
      const text = factory.toString();
      if (!relevant(text)) continue;
      chunk[1][id] = function(module, exports, require) {
        factory(module, exports, require);
        capture(module.exports);
        emit(); publishPaths();
      };
    }
    return push(chunk);
  };
  // The initial sp bundle owns some modules directly rather than adding a
  // chunk. Resolve only the three data/API modules once its bootstrap ends.
  setTimeout(() => chunks.push([['stearlight-frame-menus'], {}, require => {
    for (const [id, factory] of Object.entries(require.m))
      if (relevant(factory.toString())) capture(require(id));
    emit(); publishPaths();
  }]), 0);
})();
