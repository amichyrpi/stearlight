#pragma once
#include <openxr/openxr.h>
#include <array>
#include <cmath>
namespace quest {
using Matrix=std::array<float,16>;
inline Matrix multiply(const Matrix& a,const Matrix& b){Matrix r{};for(int c=0;c<4;c++)for(int row=0;row<4;row++)for(int k=0;k<4;k++)r[c*4+row]+=a[k*4+row]*b[c*4+k];return r;}
inline Matrix poseMatrix(XrPosef p){
 auto q=p.orientation;float x=q.x,y=q.y,z=q.z,w=q.w;
 return {1-2*(y*y+z*z),2*(x*y+z*w),2*(x*z-y*w),0,2*(x*y-z*w),1-2*(x*x+z*z),2*(y*z+x*w),0,2*(x*z+y*w),2*(y*z-x*w),1-2*(x*x+y*y),0,p.position.x,p.position.y,p.position.z,1};
}
inline Matrix inversePoseMatrix(XrPosef p){
 auto m=poseMatrix(p);Matrix r{m[0],m[4],m[8],0,m[1],m[5],m[9],0,m[2],m[6],m[10],0,0,0,0,1};
 for(int row=0;row<3;row++)r[12+row]=-(r[row]*p.position.x+r[4+row]*p.position.y+r[8+row]*p.position.z);
 return r;
}
inline Matrix viewProjection(const XrView& view){
 float l=std::tan(view.fov.angleLeft),r=std::tan(view.fov.angleRight),u=std::tan(view.fov.angleUp),d=std::tan(view.fov.angleDown);
 constexpr float near=.01f,far=20;
 Matrix p{2/(r-l),0,0,0,0,2/(u-d),0,0,(r+l)/(r-l),(u+d)/(u-d),-(far+near)/(far-near),-1,0,0,-2*far*near/(far-near),0};
 return multiply(p,inversePoseMatrix(view.pose));
}
inline Matrix handSkinMatrix(XrPosef joint,XrPosef bind,float scale){
 auto inverse=inversePoseMatrix(bind);for(int i=0;i<16;i++)if(i%4!=3)inverse[i]*=scale;
 return multiply(poseMatrix(joint),inverse);
}
}
