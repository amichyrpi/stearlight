const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const source = fs.readFileSync(`${__dirname}/../linux/steam-update-bridge.js`, 'utf8');
let received;
const updater = {
  m_updateState: {supports_os_updates: true, update_check_results: [{type: 1, eresult: 1}, {type: 3, eresult: 2}]},
  SetUpdateState(state) { received = state; this.m_updateState = state; return 42; },
  GetRecheckAndInstallUpdateTypes() {return [1, 2, 3, 5];},
};
const context = vm.createContext({UpdateStore: {m_updater: updater}});
vm.runInContext(source, context);
// Steam uses this native capability for the channel selector as well.
assert.equal(received.supports_os_updates, true);
assert.deepEqual(updater.GetRecheckAndInstallUpdateTypes(), [1, 2, 5]);
assert.deepEqual(received.update_check_results.map(r => r.type), [1]);
const nativeFailure = {type: 1, eresult: 2, message: 'Actual download failure'};
assert.equal(updater.SetUpdateState({state: 2, other: 'preserved', update_check_results: [nativeFailure, {type: 2, eresult: 2}, {type: 3, eresult: 2}], update_apply_results: [{type: 1, eresult: 1}, {type: 3, eresult: 1}]}), 42);
assert.equal(received.update_check_results[0], nativeFailure);
assert.equal(received.update_check_results[1].type, 2);
assert.equal(received.other, 'preserved');
assert.deepEqual(received.update_apply_results.map(r => r.type), [1]);
const wrapped = updater.SetUpdateState;
vm.runInContext(source, context);
assert.equal(updater.SetUpdateState, wrapped);
let tick, cleared = false;
const delayed = vm.createContext({setInterval(fn) {tick = fn; return 1;}, clearInterval() {cleared = true;}});
vm.runInContext(source, delayed);
delayed.UpdateStore = {m_updater: {SetUpdateState() {}}};
tick();
assert.equal(cleared, true);
assert.equal(delayed.UpdateStore.m_updater.stearlightUpdateCapabilities, true);
console.log('Steam update capability checks passed');
