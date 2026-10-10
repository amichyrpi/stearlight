// STEARLIGHT_QUEST_UPDATE_CAPABILITIES
// STEARLIGHT_QUEST_UPDATE_CAPABILITIES_V3
// Native Steam checks client updates as well as the SteamOS disk-image feed.
// The Frame runtime updates through the signed SteamOS image feed. Quest
// firmware belongs to Android. Preserve client and OS download failures.
(() => {
  let attempts = 0;
  const attach = () => {
    const updater = globalThis.UpdateStore?.m_updater;
    if (!updater?.SetUpdateState) return false;
    if (updater.stearlightUpdateCapabilities) return true;
    // Native update types: 1 = Steam client, 2 = OS image, 3 = BIOS.
    const supported = result => result.type !== 3;
    const set = updater.SetUpdateState;
    updater.SetUpdateState = function(state) {
      if (state) state = {
        ...state,
        update_check_results: state.update_check_results?.filter(supported),
        update_apply_results: state.update_apply_results?.filter(supported),
      };
      return set.call(this, state);
    };
    if (updater.GetRecheckAndInstallUpdateTypes) {
      const types = updater.GetRecheckAndInstallUpdateTypes;
      updater.GetRecheckAndInstallUpdateTypes = function() {
        return types.call(this).filter(type => type !== 3);
      };
    }
    updater.stearlightUpdateCapabilities = true;
    if (updater.m_updateState) updater.SetUpdateState(updater.m_updateState);
    return true;
  };
  if (!attach()) {
    const timer = setInterval(() => {
      if (attach() || ++attempts >= 120) clearInterval(timer);
    }, 250);
  }
})();
