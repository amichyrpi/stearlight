const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');

async function main() {
  let nativeCallback, nativeUpdates = 0;
  const requests = [], received = [];
  let state = { advanced: true, overlay: 3, filter: 2, scaler: 3, sharpness: 2 };
  class State {
    static M() { return { fields: { current_game_id: {}, limits: {} } }; }
    static fromObject(value) { return { serializeBase64String: () => JSON.stringify(value) }; }
  }
  class Update {
    static M() { return { fields: { settings_delta: {}, gameid: {} } }; }
    static deserializeBinary(value) { return { toObject: () => JSON.parse(value) }; }
  }
  const require = () => ({ State, Update });
  require.m = { 1: function CMsgSystemPerfState() {} };
  const chunks = [];
  chunks.push = chunk => chunk[2]?.(require);
  const perf = {
    RegisterForStateChanges(callback) { nativeCallback = callback; return { unregister() {} }; },
    UpdateSettings() { nativeUpdates++; },
  };
  const context = {
    webpackChunksteamui: chunks, SteamClient: { System: { Perf: perf } },
    setTimeout: callback => callback(), console,
    fetch: async (_, options) => {
      const request = JSON.parse(options.body);
      requests.push(request);
      for (const key of ['advanced', 'overlay', 'filter', 'scaler', 'sharpness'])
        if (request[key] !== undefined) state[key] = request[key];
      return { ok: true, json: async () => ({ ok: true, value: { ...state } }) };
    },
  };
  const source = fs.readFileSync(path.join(__dirname, '../linux/frame-menu-bridge.js'), 'utf8')
    .replace('__STEARLIGHT_DESKTOP_TOKEN__', '"test-token"');
  vm.runInNewContext(source, context);
  context.__stearlightFrameMenusStatus(state);
  const registration = perf.RegisterForStateChanges(raw => received.push(JSON.parse(raw)));
  nativeCallback('generic Linux state must not replace Quest limits');
  assert.equal(received.at(-1).limits.tdp_limit_max, 0);
  assert.equal(received.at(-1).settings.per_app.split_scaling_filter, 2);
  await perf.UpdateSettings(JSON.stringify({ settings_delta: {
    global: { perf_overlay_level: 0 }, per_app: { split_scaling_scaler: 5 },
  } }));
  assert.deepEqual(requests.at(-1), { operation: 'frame-settings', overlay: 0, scaler: 5 });
  nativeCallback('another daemon event');
  assert.equal(received.at(-1).settings.global.perf_overlay_level, 0);
  assert.equal(received.at(-1).settings.per_app.split_scaling_scaler, 5);
  assert.equal(nativeUpdates, 0);
  await perf.UpdateSettings(JSON.stringify({ settings_delta: { per_app: { is_fps_limit_enabled: true } } }));
  assert.equal(nativeUpdates, 1);
  registration.unregister();
  console.log('Frame menus: native daemon events preserve Quest limits; overlay Off and scaling changes reach the backend.');
}
main().catch(error => { console.error(error); process.exitCode = 1; });
