// Reapply the Quest browser adapters after Steam installs runtime/UI updates.
#include <sys/inotify.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void prepare(void) {
    if (system("/bin/sh /opt/quest/prepare-store-browser.sh") != 0)
        fputs("Quest browser adaptation failed\n", stderr);
    if (system("/bin/sh /opt/quest/frame-steam-ipc") != 0)
        fputs("Quest IPC adaptation unavailable for this Steam build\n", stderr);
}

static void prepare_cef(void) {
    if (system("/bin/sh /opt/quest/prepare-cef.sh") != 0)
        fputs("Quest udev guard unavailable for this CEF build\n", stderr);
}

int main(void) {
    int descriptor = inotify_init1(IN_CLOEXEC);
    if (descriptor < 0) { perror("Store UI watch"); return 1; }
    const char *directories[] = {"/opt/steam/steamui", "/opt/steam/steamrtarm64"};
    int watches[] = {-1, -1};
    char events[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
    for (;;) {
        int missing = 0, added = 0;
        for (int i = 0; i < 2; ++i) {
            if (watches[i] >= 0) continue;
            watches[i] = inotify_add_watch(descriptor, directories[i],
                IN_CLOSE_WRITE | IN_MOVED_TO | IN_DELETE_SELF | IN_MOVE_SELF);
            if (watches[i] < 0) missing = 1;
            else added = 1;
        }
        if (added) { prepare(); prepare_cef(); }
        if (missing) { usleep(250000); continue; }
        ssize_t count = read(descriptor, events, sizeof(events));
        if (count < 0) {
            if (errno == EINTR) continue;
            perror("Store UI events"); return 1;
        }
        for (char *cursor = events; cursor < events + count;) {
            struct inotify_event *event = (struct inotify_event *)cursor;
            if (event->mask & (IN_IGNORED | IN_DELETE_SELF | IN_MOVE_SELF)) {
                for (int i = 0; i < 2; ++i) {
                    if (watches[i] != event->wd) continue;
                    inotify_rm_watch(descriptor, watches[i]);
                    watches[i] = -1;
                }
            } else if (event->len && !strcmp(event->name, "libcef.so")) prepare_cef();
            else if (event->len &&
                (!strcmp(event->name, "library.js") ||
                 !strcmp(event->name, "chromehtml.so"))) prepare();
            cursor += sizeof(*event) + event->len;
        }
    }
}
