#pragma once
#include "gpu_bridge.h"
#include <cmath>
#include <cstring>
#include <initializer_list>

namespace quest {
// Frame VS_GEOM constants: model, world-to-eye, projection (row-major).
// Decode only a rigid camera and a conventional asymmetric perspective.
inline bool renderView(const float* constants,QuestGpuView& view){
 const float* v=constants+16;const float* p=constants+32;
 for(int i=0;i<48;i++)if(!std::isfinite(constants[i]))return false;
 if(std::fabs(v[12])+std::fabs(v[13])+std::fabs(v[14])+std::fabs(v[15]-1)>.001f)return false;
 for(int i=0;i<3;i++)for(int j=0;j<3;j++){
  float dot=0;for(int k=0;k<3;k++)dot+=v[i*4+k]*v[j*4+k];
  if(std::fabs(dot-(i==j?1.f:0.f))>.001f)return false;
 }
 float determinant=v[0]*(v[5]*v[10]-v[6]*v[9])-v[1]*(v[4]*v[10]-v[6]*v[8])+v[2]*(v[4]*v[9]-v[5]*v[8]);
 if(std::fabs(determinant-1)>.001f||p[0]<=0||p[5]<=0||std::fabs(p[14]+1)>.001f||std::fabs(p[15])>.001f)return false;
 for(int i:{1,3,4,7,8,9,12,13})if(std::fabs(p[i])>.001f)return false;
 QuestGpuView result{};
 for(int i=0;i<3;i++)result.position[i]=-(v[i]*v[3]+v[4+i]*v[7]+v[8+i]*v[11]);
 // Camera rotation is the transpose of the world-to-eye rotation.
 float r[3][3];for(int i=0;i<3;i++)for(int j=0;j<3;j++)r[i][j]=v[j*4+i];
 float trace=r[0][0]+r[1][1]+r[2][2];auto* q=result.orientation;
 if(trace>0){float s=2*std::sqrt(trace+1);q[3]=s*.25f;q[0]=(r[2][1]-r[1][2])/s;q[1]=(r[0][2]-r[2][0])/s;q[2]=(r[1][0]-r[0][1])/s;}
 else{
  int i=r[1][1]>r[0][0]?1:0;if(r[2][2]>r[i][i])i=2;int j=(i+1)%3,k=(i+2)%3;
  float s=2*std::sqrt(1+r[i][i]-r[j][j]-r[k][k]);q[i]=s*.25f;q[j]=(r[j][i]+r[i][j])/s;q[k]=(r[k][i]+r[i][k])/s;q[3]=(r[k][j]-r[j][k])/s;
 }
 result.fov[0]=std::atan((p[2]-1)/p[0]);result.fov[1]=std::atan((p[2]+1)/p[0]);
 result.fov[2]=std::atan((p[6]+1)/p[5]);result.fov[3]=std::atan((p[6]-1)/p[5]);
 view=result;return true;
}
}
