// KWin owns the real Wayland/Xwayland window list. Emit only on changes;
// the existing desktop manager reads its own workspace log without a daemon.
let previous = '';
function publish() {
    const windows = workspace.stackingOrder.filter(w => w.normalWindow && !w.skipTaskbar)
        .map(w => ({ id: String(w.internalId), title: w.caption }));
    const value = JSON.stringify(windows);
    if (value !== previous) { previous = value; print('STEARLIGHT_WINDOWS ' + value); }
}
function watch(window) { window.captionChanged.connect(publish); publish(); }
workspace.windowAdded.connect(watch);
workspace.windowRemoved.connect(publish);
for (const window of workspace.stackingOrder) watch(window);
publish();
