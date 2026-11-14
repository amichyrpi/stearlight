// Manual ARM64 shim/RPC probe. It never registers a SteamVR driver.
#include <openvr_driver.h>
#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <unistd.h>

class MissingServer final:public vr::IVRDriverContext {
public:
    void *GetGenericInterface(const char *name,vr::EVRInitError *error) override {
        std::printf("Remote driver requested: %s\n",name);
        if(error)*error=vr::VRInitError_Init_InterfaceNotFound;
        return nullptr;
    }
    vr::DriverHandle_t GetDriverHandle() override {return vr::k_ulInvalidPropertyContainer;}
};
int main(int argc,char **argv) {
    std::setvbuf(stdout,nullptr,_IONBF,0);
    if(argc!=2&&argc!=3)return 2;
    const bool passive=argc==3&&!std::strcmp(argv[2],"--passive");
    if(passive)setenv("STEARLIGHT_STANDABLE_PASSIVE","1",1);
    std::puts("Opening native bridge");
    void *library=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
    if(!library){std::fprintf(stderr,"Load failed: %s\n",dlerror());return 3;}
    std::puts("Native bridge loaded");
    using Factory=void *(*)(const char *,int *);
    auto factory=reinterpret_cast<Factory>(dlsym(library,"HmdDriverFactory"));
    if(!factory)return 4;
    int error=0;
    std::puts("Requesting remote provider");
    auto provider=static_cast<vr::IServerTrackedDeviceProvider *>(
        factory(vr::IServerTrackedDeviceProvider_Version,&error));
    if(!provider){std::printf("RPC provider unavailable: %d\n",error);_exit(5);}
    auto versions=provider->GetInterfaceVersions();
    if(!versions)_exit(6);
    for(unsigned i=0;i<64&&versions[i];++i)std::printf("Remote interface: %s\n",versions[i]);
    MissingServer context;
    auto status=provider->Init(&context);
    if(passive) {
        if(status!=vr::VRInitError_None)return 7;
        const auto start=std::chrono::steady_clock::now();
        for(int i=0;i<1000;++i)provider->RunFrame();
        provider->Cleanup();
        const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now()-start).count();
        std::printf("PASS: disabled provider runs 1000 frames and shuts down in %lld ms\n",
                    static_cast<long long>(elapsed));
        return elapsed<100?0:8;
    }
    std::printf("Remote initialization without services: %d\n",status);
    if(status!=vr::VRInitError_Init_InterfaceNotFound)_exit(7);
    std::puts("PASS: native ARM64 shim, Windows provider RPC and explicit missing-service failure");
    // The PRoot diagnostic session owns and stops the spawned Wine children.
    // No runtime driver has been registered or successfully initialized.
    _exit(0);
}
