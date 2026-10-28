#pragma once
#include "../app/src/main/cpp/device_profile.h"
#include <fstream>
namespace quest {
inline Headset readHeadsetProfile(){
 std::ifstream input("/run/stearlight/quest/headset-profile");int version=0;std::string id;
 // Old installations without this file retain their Quest 2 identity.
 return input>>version>>id&&version==1?headsetFromName(id):Headset::Quest2;
}
inline std::string headsetSerial(Headset device,const char* suffix){
 std::string id=headsetId(device);for(char& c:id)c=char(std::toupper(static_cast<unsigned char>(c)));
 return "STEARLIGHT-"+id+"-"+suffix;
}
}
