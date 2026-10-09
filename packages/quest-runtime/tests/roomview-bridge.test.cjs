const { test } = require('node:test');
const assert = require('node:assert/strict');
const { readFileSync } = require('node:fs');
const { runInNewContext } = require('node:vm');
const source = readFileSync(`${__dirname}/../linux/roomview-bridge.js`, 'utf8');

test('native room-view events reach the platform and retain Valve callbacks', () => {
  let event;
  const writes = [], calls = [];
  const VRHTML = {
    VRSettings: { Set: (...args) => writes.push(args) },
    RegisterForRoomViewChangedEvents(callback) { event = callback; return 42; },
  };
  const context = { VRHTML, console };
  runInNewContext(source, context);
  assert.equal(VRHTML.RegisterForRoomViewChangedEvents((...args) => {
    calls.push(args); return 7;
  }), 42);
  for (const args of [[true, true], [false, true], [true, false], [false, false]]) {
    assert.equal(event(...args), 7);
  }
  assert.deepEqual(calls, [[true, true], [false, true], [true, false], [false, false]]);
  assert.deepEqual(writes.map(args => args[2]), [true, true, true, false]);
  assert.ok(writes.every(args => args[0] === 'stearlight_quest' && args[1] === 'passthrough_enabled'));
  const register = VRHTML.RegisterForRoomViewChangedEvents;
  runInNewContext(source, context);
  assert.equal(VRHTML.RegisterForRoomViewChangedEvents, register);
});

test('a platform settings failure still delivers Valve’s event', () => {
  let event, delivered = false;
  const VRHTML = {
    VRSettings: { Set() { throw Error('unavailable'); } },
    RegisterForRoomViewChangedEvents(callback) { event = callback; },
  };
  runInNewContext(source, { VRHTML, console: { warn() {} } });
  VRHTML.RegisterForRoomViewChangedEvents(() => { delivered = true; });
  event(true, true);
  assert.equal(delivered, true);
});

test('ordinary browser contexts require no VR bridge', () => {
  assert.doesNotThrow(() => runInNewContext(source, {}));
});

test('room view retains native floor preferences across toggles', () => {
  let event;
  const ConstructStore = { m_bShowFloor: true, m_bShowFloorFar: false };
  const VRHTML = {
    VRSettings: { Set() {} },
    RegisterForRoomViewChangedEvents(callback) { event = callback; },
  };
  runInNewContext(source, { VRHTML, ConstructStore });
  VRHTML.RegisterForRoomViewChangedEvents(() => {});
  for (const args of [[true, true], [false, true], [false, false]]) {
    event(...args);
    assert.deepEqual(ConstructStore, { m_bShowFloor: true, m_bShowFloorFar: false });
  }
});
