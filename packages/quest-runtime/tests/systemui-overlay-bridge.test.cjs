const { test } = require('node:test');
const assert = require('node:assert/strict');
const { readFileSync } = require('node:fs');
const { runInNewContext } = require('node:vm');
const source = readFileSync(`${__dirname}/../linux/systemui-overlay-bridge.js`, 'utf8');

test('systemui uses its own texture handle and scene reference namespace', () => {
  let currentHandle = 42;
  const overlay = {
    ThisOverlayKey: () => 'system.vrwebhelper.controllerbinding',
    ThisOverlayHandle: () => 7,
    FindOverlay(key) { assert.equal(key, 'system.systemui'); return currentHandle; },
  };
  runInNewContext(source, { document: { title: 'systemui' }, VRHTML: { VROverlay: overlay } });
  assert.equal(overlay.ThisOverlayKey(), 'system.systemui');
  assert.equal(overlay.ThisOverlayHandle(), 42);
  currentHandle = 84;
  assert.equal(overlay.ThisOverlayHandle(), 84);
});

test('other browser pages retain their own native identity', () => {
  const native = () => 'controllerbinding';
  const overlay = { ThisOverlayKey: native, FindOverlay() {} };
  runInNewContext(source, { document: { title: 'controllerbinding' }, VRHTML: { VROverlay: overlay } });
  assert.equal(overlay.ThisOverlayKey, native);
  assert.doesNotThrow(() => runInNewContext(source, {}));
});
