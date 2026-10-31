#include "../linux/render_view.h"
#include <cassert>
#include <cstdio>
#include <limits>
static void near(float a,float b){assert(std::fabs(a-b)<1e-5f);}
int main(){
 for(int axis=0;axis<3;axis++)for(float angle:{0.f,.7f,3.14159265f})for(int eye=0;eye<2;eye++){
  float c=std::cos(angle),s=std::sin(angle),r[3][3]{};for(int i=0;i<3;i++)r[i][i]=1;
  int a=(axis+1)%3,b=(axis+2)%3;r[a][a]=r[b][b]=c;r[a][b]=-s;r[b][a]=s;
  float data[48]{};for(int i=0;i<4;i++)data[i*5]=1;
  auto* v=data+16;auto* p=data+32;float pos[]={eye?.23f:.17f,1.6f,-.8f};
  for(int i=0;i<3;i++){for(int j=0;j<3;j++)v[i*4+j]=r[j][i];for(int j=0;j<3;j++)v[i*4+3]-=v[i*4+j]*pos[j];}v[15]=1;
  float l=eye?-1.f:-1.15f,rr=eye?1.15f:1.f,u=1.11f,d=-1.19f;
  p[0]=2/(rr-l);p[2]=(rr+l)/(rr-l);p[5]=2/(u-d);p[6]=(u+d)/(u-d);p[10]=-1.001f;p[11]=-.1f;p[14]=-1;
  QuestGpuView out{};assert(quest::renderView(data,out));
  for(int i=0;i<3;i++)near(out.position[i],pos[i]);
  near(std::fabs(out.orientation[axis]),std::fabs(std::sin(angle/2)));near(std::fabs(out.orientation[3]),std::fabs(std::cos(angle/2)));
  near(std::tan(out.fov[0]),l);near(std::tan(out.fov[1]),rr);near(std::tan(out.fov[2]),u);near(std::tan(out.fov[3]),d);
  auto original=out;data[16]=std::numeric_limits<float>::quiet_NaN();assert(!quest::renderView(data,out));assert(!memcmp(&out,&original,sizeof(out)));
 }
 puts("Rendered camera matrices: stereo origins, asymmetric FOV, all quaternion branches and invalid-input preservation pass");
}
