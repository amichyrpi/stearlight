// Optional Standable provider: keep Wine startup and estimation off RunFrame.
#include "driver.h"
#include "rpc_core.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <thread>
#include <unistd.h>

void *QuestCreateRemoteProvider(const char *,int *);

namespace {
constexpr auto ready="/run/stearlight/quest/dashboard.ready";
constexpr auto status="/run/stearlight/quest/standable.status";

void report(const char *message) {
    const std::string temporary=std::string(status)+".next";
    std::ofstream output(temporary,std::ios::trunc);
    if(!output)return;
    output<<message<<'\n';output.close();
    std::rename(temporary.c_str(),status);
}

class StandableProvider final:public vr::IServerTrackedDeviceProvider {
    vr::IVRDriverContext *context=nullptr;
    std::thread worker;
    std::mutex mutex;
    std::condition_variable changed;
    bool stop=false,pending=false;
    std::atomic<bool> active{false};
    std::chrono::steady_clock::time_point next{};

    void run() {
        try {
            report("starting");
            int error=0;
            auto provider=static_cast<vr::IServerTrackedDeviceProvider *>(
                QuestCreateRemoteProvider(vr::IServerTrackedDeviceProvider_Version,&error));
            if(!provider){report("failed: Windows driver host unavailable");return;}
            if(provider->Init(context)!=vr::VRInitError_None){report("failed: driver initialization");return;}
            active=true;
            report("running: calibration and app authentication are managed by Standable");
            while(true) {
                {
                    std::unique_lock lock(mutex);
                    changed.wait(lock,[this]{return stop||pending;});
                    if(stop)break;
                    pending=false;
                }
                provider->RunFrame();
            }
        } catch(const std::exception &error) {
            std::fprintf(stderr,"Standable host stopped: %s\n",error.what());
            report("failed: driver communication");
        }
        active=false;
    }
public:
    vr::EVRInitError Init(vr::IVRDriverContext *value) override {
        context=value;
        report("disabled");
        return vr::VRInitError_None;
    }
    void Cleanup() override {
        {
            std::lock_guard lock(mutex);stop=true;
        }
        changed.notify_all();
        RpcSystem::CancelPendingCalls();
        if(worker.joinable())worker.join();
        RpcSystem::Shutdown();
        active=false;
        report("stopped");
    }
    const char *const *GetInterfaceVersions() override {return vr::k_InterfaceVersions;}
    void RunFrame() override {
        if(!worker.joinable()) {
            auto now=std::chrono::steady_clock::now();
            if(now<next)return;
            next=now+std::chrono::seconds(1);
            // SteamVR's Manage Add-ons switch controls whether Init is called.
            // Wait for the dashboard before adding Wine processes.
            if(std::getenv("STEARLIGHT_STANDABLE_PASSIVE")||access(ready,F_OK))return;
            try {
                worker=std::thread([this]{run();});
            } catch(const std::exception &) {
                report("failed: cannot start driver worker");
                return;
            }
        }
        if(active) {
            bool wake=false;
            {std::lock_guard lock(mutex);if(!pending){pending=true;wake=true;}}
            if(wake)changed.notify_one();
        }
    }
    bool ShouldBlockStandbyMode() override {return false;}
    void EnterStandby() override {}
    void LeaveStandby() override {}
};
StandableProvider provider;
}

HMD_DLL_EXPORT void *HmdDriverFactory(const char *name,int *error) {
    if(name&&!std::strcmp(name,vr::IServerTrackedDeviceProvider_Version)) {
        if(error)*error=vr::VRInitError_None;
        return &provider;
    }
    if(error)*error=vr::VRInitError_Init_InterfaceNotFound;
    return nullptr;
}
