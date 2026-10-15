// Keep SteamVR connected while Steam installs its first-launch update.
#include <openvr.h>
#include <csignal>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <sys/wait.h>
#include <unistd.h>
#include <dirent.h>
#include <fstream>
#include <map>
#include <set>
#include <sys/stat.h>
#include <thread>
extern "C" int quest_services_run();
extern "C" void quest_services_stop();
class QuestServices {
    std::thread worker;
public:
    explicit QuestServices(bool enabled) {
        if (enabled) worker = std::thread([] { quest_services_run(); });
    }
    ~QuestServices() {
        quest_services_stop();
        if (worker.joinable()) worker.join();
    }
};
#define QUEST_WATCH_EMBEDDED
#include "store-browser-watch.c"
#include "local-vr-audio.h"

static volatile sig_atomic_t stopping = 0;
static void stop(int) { stopping = 1; }

// Own Steam's launcher directly: shell polling otherwise forks transient
// helpers on every check, exceeding Android's process budget during games.
class Session {
    bool desktopMode = false;
    void stopClient() {
        if (client <= 0) return;
        // PRoot can resume traced SIGSTOP targets. Close the owned Steam
        // launcher so its watchdog cannot recreate CEF during desktop mode.
        std::set<pid_t> clients{client};
        std::set<pid_t> browsers;
        DIR* directory=opendir("/proc");
        if(directory){
            while(auto* entry=readdir(directory)){
                pid_t pid=atoi(entry->d_name);if(pid<=0)continue;
                std::string path=std::string("/proc/")+entry->d_name;
                // Android hides proc inode ownership from app processes.
                // Read the real UID from status instead of stat(2).
                std::ifstream status(path+"/status");std::string line;bool ours=false;
                while(std::getline(status,line))if(line.rfind("Uid:",0)==0){ours=atoi(line.c_str()+4)==getuid();break;}
                if(!ours)continue;
                std::ifstream command(path+"/comm");std::string name;std::getline(command,name);
                if (name == "steam") clients.insert(pid);
                if (name == "steamwebhelper") browsers.insert(pid);
            }
            closedir(directory);
        }
        for (pid_t pid : clients) kill(pid, SIGKILL);
        kill(-client, SIGKILL);
        for (pid_t pid : browsers) kill(pid, SIGKILL);
        std::printf("QUEST_DESKTOP_PROCESS_BUDGET clients=%zu browsers=%zu uid=%u\n",clients.size(),browsers.size(),getuid());
        std::fflush(stdout);
        int status;waitpid(client,&status,0);client=-1;
    }
public:
    pid_t client = -1, startup = -1;
    unsigned restarts = 0;
    std::chrono::steady_clock::time_point started;
    bool enabled;
    explicit Session(bool own) : enabled(own) {}
    void stopVR() {
        // Remote Desktop does not need the headset compositor or its browser.
        // Marked descendants include Chromium helpers with generic names.
        DIR* directory=opendir("/proc");
        if(directory){
            while(auto* entry=readdir(directory)){
                pid_t pid=atoi(entry->d_name);if(pid<=0)continue;
                std::string path=std::string("/proc/")+entry->d_name;
                std::ifstream status(path+"/status");std::string line;bool ours=false;
                while(std::getline(status,line))if(line.rfind("Uid:",0)==0){ours=atoi(line.c_str()+4)==getuid();break;}
                if(!ours)continue;
                std::ifstream environment(path+"/environ",std::ios::binary);
                while(std::getline(environment,line,'\0'))if(line=="STEARLIGHT_DESKTOP_VR_RUNTIME=1"){
                    kill(pid,SIGKILL);break;
                }
            }
            closedir(directory);
        }
        if(startup>0){int status;waitpid(startup,&status,0);startup=-1;}
    }
    ~Session() {
        if (client > 0) kill(client, SIGTERM);
        if (startup > 0) kill(startup, SIGTERM);
    }
    bool startVR() {
        if (!enabled) return true;
        startup = fork();
        if (startup < 0) return false;
        if (!startup) {
            if (!std::freopen("/tmp/stearlight-steamvr-native.log", "w", stdout)) _exit(127);
            dup2(STDOUT_FILENO, STDERR_FILENO);
            setenv("STEARLIGHT_NATIVE_STEAMVR_WAIT_FOR_STEAM", "0", 1);
            setenv("STEARLIGHT_DESKTOP_VR_RUNTIME", "1", 1);
            setenv("NODEVICE_SELECT", "1", 1);
            setenv("ENABLE_GAMESCOPE_WSI", "", 1);
            setenv("DISABLE_GAMESCOPE_WSI", "1", 1);
            setenv("VK_ICD_FILENAMES", "/opt/quest/turnip_icd.json", 1);
            execl("/usr/local/bin/stearlight-steamvr-native", "stearlight-steamvr-native", "startup", nullptr);
            _exit(127);
        }
        return true;
    }
    bool startClient() {
        if (!enabled) return true;
        std::fflush(nullptr);
        started = std::chrono::steady_clock::now();
        client = fork();
        if (client < 0) return false;
        if (!client) {
            // Give the desktop handover ownership of the complete launcher,
            // including FEX's helper processes.
            setpgid(0, 0);
            if (!std::freopen("/home/stearlight/.local/share/Steam/logs/steam_output.log", "a", stdout)) _exit(127);
            dup2(STDOUT_FILENO, STDERR_FILENO);
            setenv("VK_ICD_FILENAMES", "/opt/quest/quest_icd.json", 1);
            if (chdir("/opt/steam") != 0) _exit(127);
            execl("/usr/bin/bash", "bash", "/opt/steam/RUNSTEAM.sh", nullptr);
            _exit(127);
        }
        return true;
    }
    bool poll(bool desktopAudioReady = true) {
        if (!enabled) return true;
        bool requested=access("/run/stearlight/quest/desktop-mode.active",F_OK)==0;
        if(requested){
            if(!desktopMode){
                desktopMode=true;
                std::ifstream mode("/run/stearlight/quest/desktop-mode.active");
                std::string kind;std::getline(mode,kind);
                if(kind=="remote")stopClient();
                std::puts("QUEST_DESKTOP_MODE_READY");
            }
            if(desktopAudioReady){FILE* ready=std::fopen("/run/stearlight/quest/desktop-mode.ready","w");if(ready)std::fclose(ready);}
            return true;
        }
        if(desktopMode){
            desktopMode=false;unlink("/run/stearlight/quest/desktop-mode.ready");
            std::puts("QUEST_DESKTOP_MODE_RETURN_TO_STEAM");
            int status;
            if(client>0 && waitpid(client,&status,WNOHANG)==client)client=-1;
            if(client<=0)return startClient();
        }
        int status;
        if (startup > 0 && waitpid(startup, &status, WNOHANG) == startup) startup = -1;
        if (client <= 0 || waitpid(client, &status, WNOHANG) != client) return true;
        client = -1;
        if (std::chrono::steady_clock::now() - started >= std::chrono::seconds(60)) restarts = 0;
        if (WIFEXITED(status) && (WEXITSTATUS(status) == 0 || WEXITSTATUS(status) == 42) && restarts++ < 5) {
            std::printf("QUEST_STEAM_CLIENT_RESTART %u\n", restarts);
            std::fflush(stdout);
            if (system("/bin/sh /opt/quest/prepare-steam-client.sh") == 0) return startClient();
        }
        std::fprintf(stderr, "Quest Steam client exited: %d\n", status);
        return false;
    }
};

int main(int argc, char** argv) {
    std::signal(SIGTERM, stop);
    std::signal(SIGINT, stop);
    Session session(argc == 2 && !std::strcmp(argv[1], "--launch-session"));
    LocalVRAudio audio(session.enabled);
    QuestServices services(session.enabled);
    if (!session.startVR()) return 1;
    vr::EVRInitError error = vr::VRInitError_Unknown;
    vr::IVRSystem* system = nullptr;
    for (unsigned attempt = 0; attempt < (session.enabled ? 120u : 1u) && !stopping; ++attempt) {
        system = vr::VR_Init(&error, vr::VRApplication_VRMonitor);
        if (error == vr::VRInitError_None) break;
        usleep(250000);
    }
    if (error != vr::VRInitError_None) {
        std::fprintf(stderr, "Quest runtime monitor: %s\n",
                     vr::VR_GetVRInitErrorAsEnglishDescription(error));
        return 1;
    }
    std::puts("QUEST_RUNTIME_MONITOR_CONNECTED");
    std::fflush(stdout);
    if (!session.startClient()) { vr::VR_Shutdown(); return 1; }
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
    bool failed = false, desktopShown = false, desktopWasActive = false, remoteMode = false;
    while (!stopping) {
        std::ifstream desktopRequest("/run/stearlight/quest/desktop-mode.active");
        std::string desktopKind;std::getline(desktopRequest,desktopKind);
        if(desktopKind=="remote"){
            if(!remoteMode){
                remoteMode=true;vr::VR_Shutdown();session.stopVR();
                unlink(ready);std::puts("QUEST_REMOTE_DESKTOP_VR_RELEASED");
            }
            audio.poll(true);
            if(!session.poll(access("/run/stearlight/quest/local-vr-audio.paused",F_OK)==0)){failed=true;break;}
            usleep(200000);continue;
        }
        if(remoteMode){
            // Valve's bootstrap IPC cannot be reused after this handover.
            // Ask the Android owner to start a fresh Linux session and IPC
            // directory, without adding a permanent supervisor process.
            std::puts("QUEST_REMOTE_DESKTOP_VR_RESTARTING");std::fflush(nullptr);
            return 75;
        }
        audio.poll(!desktopKind.empty());
        bool desktopAudioReady=desktopKind.empty() || access("/run/stearlight/quest/local-vr-audio.paused",F_OK)==0;
        if (!session.poll(desktopAudioReady) || quest_watch_step() < 0) { failed = true; break; }
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
        bool desktopMode = access("/run/stearlight/quest/desktop-mode.active", F_OK) == 0;
        if (!desktopMode && desktopWasActive) { desktopShown = false; steamRequested = false; }
        desktopWasActive = desktopMode;
        const char* desktopFocus = "/run/stearlight/quest/desktop-focus.request";
        if (desktopMode && (!desktopShown || access(desktopFocus, F_OK) == 0) && overlays) {
            vr::VROverlayHandle_t desktop;
            // Gamescope's master dashboard overlay owns connector surfaces;
            // the master itself need not have a texture size.
            if (overlays->FindOverlay("stearlight.plasma.desktop", &desktop) == vr::VROverlayError_None) {
                overlays->ShowDashboard("stearlight.plasma.desktop");
                desktopShown = true;
                unlink(desktopFocus);
            }
        }
        vr::VROverlayHandle_t steamOverlay;
        bool steamAvailable = false;
        if (overlays && overlays->FindOverlay("valve.steam.gamepadui.main", &steamOverlay) == vr::VROverlayError_None) {
            uint32_t width = 0, height = 0;
            bool hasTexture = overlays->GetOverlayTextureSize(steamOverlay, &width, &height) == vr::VROverlayError_None && width && height;
            if (!desktopMode && !steamRequested && dashboardRequested && hasTexture) {
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
        }
        if (overlays && overlays->FindOverlay("system.systemui", &overlay) == vr::VROverlayError_None) {
            if (!dashboardRequested) {
                overlays->ShowDashboard("");
                dashboardRequested = true;
                std::puts("QUEST_NATIVE_DASHBOARD_REQUESTED");
                std::fflush(stdout);
            }
            uint32_t width = 0, height = 0;
            // Frame embeds the Steam textures in its scene graph. Their direct
            // overlay visibility can be false while the panels are rendered.
            // Readiness tracks available textures so hiding the dashboard
            // during startup cannot leave Android's loading loop on top.
            if (!dashboardReady && steamAvailable &&
                overlays->GetOverlayTextureSize(overlay, &width, &height) == vr::VROverlayError_None && width && height) {
                if (FILE* marker = std::fopen(ready, "w")) {
                    std::fclose(marker);
                    dashboardReady = true;
                    // Confirm only after the compositor and both Steam
                    // panels have textures, so failed updates can recover.
                    const char* activated = "/home/stearlight/.cache/stearlight/os-update/activated.json";
                    const char* installed = "/home/stearlight/.cache/stearlight/os-update/installed.json";
                    if (access(activated, F_OK) == 0 && rename(activated, installed) != 0)
                        std::perror("Quest OS update confirmation");
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
    return failed ? 1 : 0;
}
