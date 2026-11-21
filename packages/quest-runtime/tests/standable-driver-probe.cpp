// Manual Windows-driver ABI probe. Does not register a driver or create trackers.
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <openvr_driver.h>

class ProbeContext final:public vr::IVRDriverContext {
public:
    void *GetGenericInterface(const char *name,vr::EVRInitError *error) override {
        std::printf("Requested service: %s\n",name);
        if(error)*error=vr::VRInitError_Init_InterfaceNotFound;
        return nullptr;
    }
    vr::DriverHandle_t GetDriverHandle() override {return vr::k_ulInvalidPropertyContainer;}
};

int main(int argc,char **argv) {
    if(argc>=3&&!std::freopen(argv[2],"w",stdout))return 7;
    std::setvbuf(stdout,nullptr,_IONBF,0);
    if(argc<2||argc>4){std::fprintf(stderr,"Expected DLL, optional result log and --discover-context\n");return 2;}
    std::puts("Loading Windows driver");
    HMODULE module=LoadLibraryExA(argv[1],nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);
    if(!module){std::printf("Driver load failed: Windows error %lu\n",GetLastError());return 3;}
    using Factory=void *(__cdecl *)(const char *,int *);
    auto factory=reinterpret_cast<Factory>(GetProcAddress(module,"HmdDriverFactory"));
    if(!factory){std::puts("HmdDriverFactory is missing");FreeLibrary(module);return 4;}
    int error=0;
    auto provider=static_cast<vr::IServerTrackedDeviceProvider *>(
        factory(vr::IServerTrackedDeviceProvider_Version,&error));
    if(!provider){std::printf("Provider unavailable: %d\n",error);FreeLibrary(module);return 5;}
    std::printf("Provider: %s\n",vr::IServerTrackedDeviceProvider_Version);
    const char *const *interfaces=provider->GetInterfaceVersions();
    if(!interfaces){FreeLibrary(module);return 6;}
    for(unsigned i=0;i<64&&interfaces[i];++i)std::printf("Interface: %s\n",interfaces[i]);
    if(argc==4) {
        if(std::strcmp(argv[3],"--discover-context")){FreeLibrary(module);return 2;}
        ProbeContext context;
        auto status=provider->Init(&context);
        std::printf("Initialization without server services returned: %d\n",status);
        if(status==vr::VRInitError_None){provider->Cleanup();FreeLibrary(module);return 8;}
        FreeLibrary(module);
        return 0;
    }
    std::puts("PASS: Windows driver factory and interface discovery; initialization untested");
    FreeLibrary(module);
    return 0;
}
