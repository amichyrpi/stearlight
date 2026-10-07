// Keep SteamVR connected while Steam installs its first-launch update.
#include <openvr.h>
#include <csignal>
#include <cstdio>
#include <unistd.h>

static volatile sig_atomic_t stopping = 0;
static void stop(int) { stopping = 1; }

int main() {
    std::signal(SIGTERM, stop);
    std::signal(SIGINT, stop);
    vr::EVRInitError error;
    auto *system = vr::VR_Init(&error, vr::VRApplication_VRMonitor);
    if (error != vr::VRInitError_None) {
        std::fprintf(stderr, "Quest runtime monitor: %s\n",
                     vr::VR_GetVRInitErrorAsEnglishDescription(error));
        return 1;
    }
    std::puts("QUEST_RUNTIME_MONITOR_CONNECTED");
    std::fflush(stdout);
    bool passthroughRequested = false;
    const char* passthrough = "/run/stearlight/quest/passthrough.enabled";
    unlink(passthrough);
    auto* settings = vr::VRSettings();
    settings->SetBool("stearlight_quest", "passthrough_enabled", false);
    if (access("/run/stearlight/quest/passthrough.supported", F_OK) == 0) {
        settings->SetBool("camera", "enableCamera", true);
        settings->SetInt32("camera", "roomView", 2);
    }
    const char* ready = "/run/stearlight/quest/dashboard.ready";
    unlink(ready);
    bool dashboardRequested = false, dashboardReady = false, steamRequested = false;
    while (!stopping) {
        vr::EVRSettingsError settingError;
        bool requested = settings->GetBool("stearlight_quest", "passthrough_enabled", &settingError);
        if (settingError == vr::VRSettingsError_None && requested != passthroughRequested) {
            if (requested && access("/run/stearlight/quest/passthrough.supported", F_OK) == 0) {
                if (FILE* marker = std::fopen(passthrough, "w")) std::fclose(marker);
            } else unlink(passthrough);
            passthroughRequested = requested;
            std::printf("QUEST_NATIVE_ROOMVIEW_REQUESTED %d\n", requested);
            std::fflush(stdout);
        }
        vr::VREvent_t event;
        while (system->PollNextEvent(&event, sizeof(event))) {
            if (event.eventType == vr::VREvent_Quit) {
                system->AcknowledgeQuit_Exiting();
                stopping = 1;
            }
        }
        vr::VROverlayHandle_t overlay;
        auto* overlays = vr::VROverlay();
        vr::VROverlayHandle_t steamOverlay;
        bool steamReady = false, steamAvailable = false;
        if (overlays && overlays->FindOverlay("valve.steam.gamepadui.main", &steamOverlay) == vr::VROverlayError_None) {
            uint32_t width = 0, height = 0;
            bool hasTexture = overlays->GetOverlayTextureSize(steamOverlay, &width, &height) == vr::VROverlayError_None && width && height;
            if (!steamRequested && dashboardRequested && hasTexture) {
                overlays->ShowDashboard("valve.steam.gamepadui.main");
                steamRequested = true;
                std::puts("QUEST_NATIVE_STEAM_DASHBOARD_REQUESTED");
                std::fflush(stdout);
            }
            vr::VROverlayHandle_t bar;
            uint32_t barWidth = 0, barHeight = 0;
            steamAvailable = hasTexture &&
                overlays->FindOverlay("valve.steam.gamepadui.bar", &bar) == vr::VROverlayError_None &&
                overlays->GetOverlayTextureSize(bar, &barWidth, &barHeight) == vr::VROverlayError_None &&
                barWidth && barHeight;
            steamReady = steamAvailable && overlays->IsOverlayVisible(steamOverlay) && overlays->IsOverlayVisible(bar);
        }
        if (overlays && overlays->FindOverlay("system.systemui", &overlay) == vr::VROverlayError_None) {
            if (!dashboardRequested) {
                overlays->ShowDashboard("");
                dashboardRequested = true;
                std::puts("QUEST_NATIVE_DASHBOARD_REQUESTED");
                std::fflush(stdout);
            }
            uint32_t width = 0, height = 0;
            if (!dashboardReady && steamReady && overlays->IsDashboardVisible() && overlays->IsOverlayVisible(overlay) &&
                overlays->GetOverlayTextureSize(overlay, &width, &height) == vr::VROverlayError_None && width && height) {
                if (FILE* marker = std::fopen(ready, "w")) {
                    std::fclose(marker);
                    dashboardReady = true;
                    std::printf("QUEST_NATIVE_DASHBOARD_TEXTURE %ux%u\n", width, height);
                    std::fflush(stdout);
                }
            }
        }
        // Readiness belongs to this VR session, not a browser page. Valve
        // recreates overlays during navigation and handles client errors in
        // its own dashboard. Re-requesting Home here interrupts navigation;
        // removing readiness would also replay Android's startup animation.
        usleep(200000);
    }
    vr::VR_Shutdown();
    unlink(passthrough);
    unlink(ready);
    return 0;
}
