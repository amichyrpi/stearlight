// Optional Standable provider: keep Wine startup and estimation off RunFrame.
#include "driver.h"
#include "rpc_core.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <thread>
#include <unistd.h>

void *QuestCreateRemoteProvider(const char *,int *);
void QuestFlushTrackedPoses();
void QuestSetFramePeriod(uint32_t);

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
    bool stop=false,pending=false,workerDone=false;
    std::atomic<bool> active{false};
    std::atomic<uint32_t> framePeriod{8333333};
    std::chrono::steady_clock::time_point next{};
    std::chrono::steady_clock::time_point nextFrame{};

    void run() {
        vr::IServerTrackedDeviceProvider *initialized=nullptr;
        try {
            report("starting");
            int error=0;
            auto provider=static_cast<vr::IServerTrackedDeviceProvider *>(
                QuestCreateRemoteProvider(vr::IServerTrackedDeviceProvider_Version,&error));
            if(!provider){report("failed: Windows driver host unavailable");return;}
            if(provider->Init(context)!=vr::VRInitError_None){report("failed: driver initialization");return;}
            initialized=provider;
            active=true;
            report("running: calibration and app authentication are managed by Standable");
            std::chrono::steady_clock::time_point nextRefresh{};
            while(true) {
                {
                    std::unique_lock lock(mutex);
                    changed.wait(lock,[this]{return stop||pending;});
                    if(stop)break;
                    pending=false;
                }
                const auto now=std::chrono::steady_clock::now();
                if(now>=nextRefresh) {
                    vr::ETrackedPropertyError error=vr::TrackedProp_Success;
                    auto *properties=vr::VRProperties();
                    const auto hmd=properties->TrackedDeviceToPropertyContainer(vr::k_unTrackedDeviceIndex_Hmd);
                    const float hz=properties->GetFloatProperty(hmd,
                        vr::Prop_DisplayFrequency_Float,&error);
                    if(error==vr::TrackedProp_Success&&std::isfinite(hz)&&hz>=30&&hz<=120) {
                        const auto period=static_cast<uint32_t>(1000000000.0/hz+0.5);
                        framePeriod.store(period,std::memory_order_relaxed);
                        QuestSetFramePeriod(period);
                    }
                    nextRefresh=now+std::chrono::seconds(1);
                }
                provider->RunFrame();
                QuestFlushTrackedPoses();
            }
        } catch(const std::exception &error) {
            std::fprintf(stderr,"Standable host stopped: %s\n",error.what());
            report("failed: driver communication");
        }
        active=false;
        if(initialized) {
            try {
                initialized->Cleanup();
            } catch(const std::exception &error) {
                std::fprintf(stderr,"Standable cleanup RPC failed: %s\n",error.what());
            }
        }
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
        if(worker.joinable()) {
            std::unique_lock lock(mutex);
            const bool completed=changed.wait_for(lock,std::chrono::seconds(3),[this]{return workerDone;});
            lock.unlock();
            if(!completed)RpcSystem::CancelPendingCalls();
            worker.join();
        }
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
                worker=std::thread([this]{
                    run();
                    {std::lock_guard lock(mutex);workerDone=true;}
                    changed.notify_all();
                });
            } catch(const std::exception &) {
                report("failed: cannot start driver worker");
                return;
            }
        }
        if(active) {
            // vrserver can call providers much faster than the display. The
            // Windows driver's independent pose/estimation threads keep their
            // own cadence; forwarding empty frame calls must not busy-poll Wine.
            const auto now=std::chrono::steady_clock::now();
            if(now<nextFrame)return;
            nextFrame=now+std::chrono::nanoseconds(framePeriod.load(std::memory_order_relaxed));
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
