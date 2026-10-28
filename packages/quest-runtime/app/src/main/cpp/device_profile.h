#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>

namespace quest {
enum class Headset { Unknown, Quest2, Quest3, Quest3S };
inline Headset headsetFromName(const std::string& name) {
 std::string key;
 for(unsigned char c:name)if(std::isalnum(c))key+=char(std::tolower(c));
 if(key=="quest3s"||key=="metaquest3s"||key=="oculusquest3s")return Headset::Quest3S;
 if(key=="quest3"||key=="metaquest3"||key=="oculusquest3")return Headset::Quest3;
 if(key=="quest2"||key=="metaquest2"||key=="oculusquest2")return Headset::Quest2;
 return Headset::Unknown;
}
inline const char* headsetId(Headset device) {
 switch(device){case Headset::Quest2:return "quest2";case Headset::Quest3:return "quest3";case Headset::Quest3S:return "quest3s";default:return "quest";}
}
inline const char* headsetName(Headset device) {
 switch(device){case Headset::Quest2:return "Oculus Quest2";case Headset::Quest3:return "Meta Quest 3";case Headset::Quest3S:return "Meta Quest 3S";default:return "Meta Quest";}
}
inline bool touchPlus(Headset device){return device==Headset::Quest3||device==Headset::Quest3S;}
inline const char* controllerModel(bool plus,int hand){return plus?(hand?"oculus_quest_plus_controller_right":"oculus_quest_plus_controller_left"):(hand?"oculus_quest2_controller_right":"oculus_quest2_controller_left");}
struct EyeSize {uint32_t width,height;};
inline EyeSize eyeSize(Headset device,uint32_t recommendedWidth,uint32_t recommendedHeight,uint32_t maxWidth,uint32_t maxHeight){
 uint32_t width=recommendedWidth,height=recommendedHeight;
 if(device==Headset::Quest3){width=2064;height=2208;}
 else if(device==Headset::Quest2||device==Headset::Quest3S){width=1832;height=1920;}
 return {std::min(width,maxWidth),std::min(height,maxHeight)};
}
}
