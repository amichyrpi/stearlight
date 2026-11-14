#include <openvr.h>
#include <cstdio>
#include <cstring>

int main(int argc,char **argv) {
    vr::EVRInitError error;
    if(!vr::VR_Init(&error,vr::VRApplication_Background)) {
        std::fprintf(stderr,"SteamVR: %s\n",vr::VR_GetVRInitErrorAsEnglishDescription(error));return 1;
    }
    auto *apps=vr::VRApplications();
    constexpr auto key="steam.overlay.2370570";
    if(!apps->IsApplicationInstalled(key)) {
        std::fprintf(stderr,"Install and launch Standable through Steam first\n");vr::VR_Shutdown();return 2;
    }
    if(argc==2&&!std::strcmp(argv[1],"--enable")) {
        auto result=apps->SetApplicationAutoLaunch(key,true);
        if(result!=vr::VRApplicationError_None) {
            std::fprintf(stderr,"Startup registration: %s\n",apps->GetApplicationsErrorNameFromEnum(result));
            vr::VR_Shutdown();return 3;
        }
        vr::VRSettings()->SetBool("driver_standable","enable",true);
    }
    std::printf("Standable SteamVR startup: %s\n",apps->GetApplicationAutoLaunch(key)?"enabled":"disabled");
    unsigned trackers=0;
    for(unsigned i=0;i<vr::k_unMaxTrackedDeviceCount;++i) {
        char name[256]{};
        vr::VRSystem()->GetStringTrackedDeviceProperty(i,vr::Prop_TrackingSystemName_String,name,sizeof(name));
        if(std::strstr(name,"standable")||std::strstr(name,"Standable"))++trackers;
    }
    std::printf("Standable devices registered: %u\n",trackers);
    vr::VR_Shutdown();return 0;
}
