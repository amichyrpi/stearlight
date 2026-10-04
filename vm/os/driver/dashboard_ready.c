#define _POSIX_C_SOURCE 200809L
#include <dlfcn.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <openvr_capi.h>

/* Wait for Valve's rendered Steam surfaces, then select its dashboard once.
 * This replaces a controller pulse sent before the browser has initialized. */
int main(int argc, char **argv) {
    unsigned seconds = argc > 1 ? (unsigned)atoi(argv[1]) : 1200;
    if (!seconds || seconds > 3600) return 2;
    void *library = dlopen("/opt/steamvr/bin/linuxarm64/libopenvr_api.so", RTLD_NOW);
    if (!library) { fprintf(stderr, "%s\n", dlerror()); return 1; }
    uint32_t (*init)(int *, int) = dlsym(library, "VR_InitInternal");
    void *(*get)(const char *, int *) = dlsym(library, "VR_GetGenericInterface");
    void (*shutdown)(void) = dlsym(library, "VR_ShutdownInternal");
    if (!init || !get || !shutdown) return 1;
    int error = 0;
    init(&error, 2); /* VRApplication_Overlay */
    if (error) { fprintf(stderr, "Dashboard init: %d\n", error); return 1; }
    struct VR_IVROverlay_FnTable *overlay = get("FnTable:IVROverlay_028", &error);
    if (!overlay || error) { shutdown(); return 1; }
    struct timespec start, now, pause = {1, 0};
    clock_gettime(CLOCK_MONOTONIC, &start);
    unsigned stable = 0;
    for (;;) {
        VROverlayHandle_t main_panel = 0, bar = 0;
        uint32_t main_w = 0, main_h = 0, bar_w = 0, bar_h = 0;
        bool ready = !overlay->FindOverlay("valve.steam.gamepadui.main", &main_panel) &&
            !overlay->FindOverlay("valve.steam.gamepadui.bar", &bar) &&
            !overlay->GetOverlayTextureSize(main_panel, &main_w, &main_h) &&
            !overlay->GetOverlayTextureSize(bar, &bar_w, &bar_h) &&
            main_w && main_h && bar_w && bar_h;
        stable = ready ? stable + 1 : 0;
        if (stable == 3) {
            overlay->ShowDashboard("valve.steam.gamepadui.main");
            printf("STEARLIGHT ARM64 STEAM SURFACES READY main=%ux%u bar=%ux%u\n",
                   main_w, main_h, bar_w, bar_h);
            shutdown(); dlclose(library); return 0;
        }
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (now.tv_sec - start.tv_sec >= seconds) break;
        nanosleep(&pause, NULL);
    }
    fprintf(stderr, "Steam dashboard surfaces did not initialize within %u seconds\n", seconds);
    shutdown(); dlclose(library); return 1;
}
