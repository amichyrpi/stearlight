#pragma once
#include <cmath>

namespace quest {
struct EyeProjection {float left,right,top,bottom;};
inline bool validEyeFov(const float* fov){
 for(int i=0;i<4;i++)if(!std::isfinite(fov[i])||std::fabs(fov[i])>=1.5707963f)return false;
 return fov[0]<fov[1]&&fov[3]<fov[2];
}
inline EyeProjection eyeProjection(const float* fov){
 // OpenVR names the -Y edge "top" and the +Y edge "bottom". These
 // are signed tangents, not the negated OpenXR up/down angles. Negating
 // them happens to work for symmetric FOVs but moves an asymmetric frustum.
 return {std::tan(fov[0]),std::tan(fov[1]),std::tan(fov[3]),std::tan(fov[2])};
}
}
