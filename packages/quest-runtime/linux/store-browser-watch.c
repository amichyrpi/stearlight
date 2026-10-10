// Reapply the Quest browser adapters after Steam installs runtime/UI updates.
#include <sys/inotify.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void prepare(void) {
    if (system("/bin/sh /opt/quest/prepare-steam-client.sh") != 0)
        fputs("Quest browser adaptation failed\n", stderr);
}

static void prepare_cef(void) {
    if (system("/bin/sh /opt/quest/prepare-cef.sh") != 0)
        fputs("Quest udev guard unavailable for this CEF build\n", stderr);
}

static int quest_watch_step(void) {
    static int descriptor = -1;
    if (descriptor < 0) descriptor = inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
    if (descriptor < 0) { perror("Store UI watch"); return -1; }
    const char *directories[] = {"/opt/steam/steamui", "/opt/steam/steamrtarm64", "/opt/steam",
        "/opt/steam/steamapps/common", "/opt/steam/steamapps/common/FEX-Emu"};
    static int watches[] = {-1, -1, -1, -1, -1};
    char events[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
    {
        int missing = 0, added = 0;
        for (int i = 0; i < 5; ++i) {
            if (watches[i] >= 0) continue;
            watches[i] = inotify_add_watch(descriptor, directories[i],
                IN_CLOSE_WRITE | IN_MOVED_TO | IN_DELETE_SELF | IN_MOVE_SELF);
            if (watches[i] < 0) { if (i < 3) missing = 1; }
            else added = 1;
        }
        if (added) { prepare(); prepare_cef(); }
        if (missing) return 0;
        ssize_t count = read(descriptor, events, sizeof(events));
        if (count < 0) {
            if (errno == EINTR || errno == EAGAIN) return 0;
            perror("Store UI events"); return -1;
        }
        for (char *cursor = events; cursor < events + count;) {
            struct inotify_event *event = (struct inotify_event *)cursor;
            if (event->mask & (IN_IGNORED | IN_DELETE_SELF | IN_MOVE_SELF)) {
                for (int i = 0; i < 5; ++i) {
                    if (watches[i] != event->wd) continue;
                    inotify_rm_watch(descriptor, watches[i]);
                    watches[i] = -1;
                }
            } else if (event->len && !strcmp(event->name, "libcef.so")) prepare_cef();
            else if (event->len &&
                (!strcmp(event->name, "library.js") ||
                 !strcmp(event->name, "chromehtml.so") ||
                 !strcmp(event->name, "steamwebhelper.sh") ||
                 !strcmp(event->name, "streaming_client") ||
                 !strcmp(event->name, "FEX-Emu") ||
                 !strcmp(event->name, "fex-compat-tool") ||
                 !strcmp(event->name, "RUNSTEAM.sh"))) prepare();
            cursor += sizeof(*event) + event->len;
        }
    }
    return 0;
}
#ifndef QUEST_WATCH_EMBEDDED
int main(void) {
    while (quest_watch_step() == 0) usleep(250000);
    return 1;
}
#endif
