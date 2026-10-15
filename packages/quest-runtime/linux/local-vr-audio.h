#pragma once
#include <cerrno>
#include <cstdlib>
#include <cstdio>
#include <csignal>
#include <chrono>
#include <sys/wait.h>
#include <unistd.h>

// Local Wine VR uses PulseAudio. Release the two streaming-only processes
// while it runs, then restore them for PCVR without another supervisor.
class LocalVRAudio {
    const char* request = "/run/stearlight/quest/local-vr.pid";
    const char* paused = "/run/stearlight/quest/local-vr-audio.paused";
    const char* supported = "/run/stearlight/quest/local-vr-audio.supported";
    pid_t services[2]{-1, -1};
    bool enabled, quiescing = false;
    std::chrono::steady_clock::time_point stopping;
    pid_t start(unsigned index) {
        pid_t pid = fork();
        if (pid != 0) return pid;
        if (!std::freopen(index ? "/tmp/quest-wireplumber.log" : "/tmp/quest-pipewire.log", "a", stdout)) _exit(127);
        dup2(STDOUT_FILENO, STDERR_FILENO);
        if (index) execl("/lib/ld-linux-aarch64.so.1", "ld-linux-aarch64.so.1", "--library-path", "/usr/lib/aarch64-linux-gnu", "/usr/bin/wireplumber", nullptr);
        else execl("/lib/ld-linux-aarch64.so.1", "ld-linux-aarch64.so.1", "--library-path", "/usr/lib/aarch64-linux-gnu", "/usr/bin/pipewire", "-c", "/opt/quest/pipewire.conf", nullptr);
        _exit(127);
    }
public:
    explicit LocalVRAudio(bool own) : enabled(own) {
        if (!enabled) return;
        const char* names[]{"QUEST_PIPEWIRE_PID", "QUEST_WIREPLUMBER_PID"};
        for (unsigned i = 0; i < 2; ++i) {
            const char* value = std::getenv(names[i]);
            long pid = value ? std::strtol(value, nullptr, 10) : -1;
            services[i] = pid > 1 ? static_cast<pid_t>(pid) : -1;
        }
        unlink(request); unlink(paused);
        if (FILE* file = std::fopen(supported, "w")) std::fclose(file);
    }
    ~LocalVRAudio() {
        if (!enabled) return;
        unlink(supported); unlink(paused); unlink(request);
        for (pid_t pid : services) if (pid > 1) kill(pid, SIGTERM);
    }
    void poll(bool desktop = false) {
        if (!enabled) return;
        long game = -1;
        if (FILE* file = std::fopen(request, "r")) {
            if (std::fscanf(file, "%ld", &game) != 1) game = -1;
            std::fclose(file);
        }
        bool local = desktop || (game > 1 && kill(static_cast<pid_t>(game), 0) == 0);
        if (local && !quiescing) {
            quiescing = true;
            stopping = std::chrono::steady_clock::now();
            for (pid_t pid : services) if (pid > 1) kill(pid, SIGTERM);
        }
        for (auto& pid : services) if (pid > 1) {
            int status;
            pid_t result = waitpid(pid, &status, WNOHANG);
            if (result == pid || (result < 0 && errno == ECHILD)) pid = -1;
            else if (quiescing && std::chrono::steady_clock::now() - stopping > std::chrono::seconds(2)) kill(pid, SIGKILL);
        }
        if (quiescing) {
            if (services[0] > 1 || services[1] > 1) return;
            if (local) {
                if (access(paused, F_OK) != 0) {
                    if (FILE* file = std::fopen(paused, "w")) std::fclose(file);
                    std::puts("QUEST_LOCAL_VR_STREAMING_AUDIO_PAUSED"); std::fflush(stdout);
                }
                return;
            }
            unlink(request); unlink(paused); quiescing = false;
            std::puts("QUEST_LOCAL_VR_STREAMING_AUDIO_RESTORING"); std::fflush(stdout);
        }
        if (services[0] < 0) services[0] = start(0);
        const char* runtime = std::getenv("PIPEWIRE_RUNTIME_DIR");
        char socket[512];
        if (runtime && std::snprintf(socket, sizeof(socket), "%s/pipewire-0", runtime) < static_cast<int>(sizeof(socket)) &&
            services[0] > 1 && services[1] < 0 && access(socket, F_OK) == 0) services[1] = start(1);
    }
};
