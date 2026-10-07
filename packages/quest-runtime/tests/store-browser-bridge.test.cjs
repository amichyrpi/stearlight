const { test } = require('node:test');
const assert = require('node:assert/strict');
const { readFileSync } = require('node:fs');
const { runInNewContext } = require('node:vm');
const source = readFileSync(`${__dirname}/../linux/store-browser-bridge.js`, 'utf8');

test('the VR Store starts on its real origin with all native options retained', () => {
  const result = {}, calls = [];
  const views = { Create(options) { calls.push({ receiver: this, options }); return result; } };
  runInNewContext(source, { SteamClient: { BrowserView: views } });
  const options = { strName: 'MainBrowser', strInitialURL: 'data:text/html,<body></body>',
    strVROverlayKey: 'valve.steam.browserview_MainBrowser_2', parentPopupBrowserID: 2,
    bPreventCloseFromJavascript: true };
  assert.equal(views.Create(options), result);
  assert.equal(calls[0].receiver, views);
  assert.equal(calls[0].options.strInitialURL, 'https://store.steampowered.com/');
  assert.equal(calls[0].options.parentPopupBrowserID, 2);
  assert.equal(calls[0].options.bPreventCloseFromJavascript, true);
  assert.equal(options.strInitialURL, 'data:text/html,<body></body>');
});

test('desktop, external, and authentication browser options pass through unchanged', () => {
  const views = { Create: options => options };
  runInNewContext(source, { SteamClient: { BrowserView: views } });
  for (const options of [{ strName: 'MainBrowser' }, { strName: 'ExternalWeb' },
    { strName: 'Microtransaction', strVROverlayKey: 'valve.steam.browserview_Microtransaction_1' }]) {
    assert.equal(views.Create(options), options);
  }
  assert.doesNotThrow(() => runInNewContext(source, {}));
});
