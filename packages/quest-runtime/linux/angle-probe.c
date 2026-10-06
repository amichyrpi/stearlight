#include <dlfcn.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
// EGL/ANGLE tokens from Khronos EGL and google/angle/include/EGL/eglext_angle.h.
int main(void){
 void* library=dlopen("/opt/steam/steamrtarm64/libEGL.so",RTLD_NOW|RTLD_LOCAL);
 if(!library){puts(dlerror());return 1;}
 void* (*getProc)(const char*)=dlsym(library,"eglGetProcAddress");
 void* (*display)(unsigned,void*,const int*)=getProc("eglGetPlatformDisplayEXT");
 unsigned (*initialize)(void*,int*,int*)=dlsym(library,"eglInitialize");
 unsigned (*error)(void)=dlsym(library,"eglGetError");
 unsigned (*choose)(void*,const int*,void**,int,int*)=dlsym(library,"eglChooseConfig");
 unsigned (*bindAPI)(unsigned)=dlsym(library,"eglBindAPI");
 void* (*context)(void*,void*,void*,const int*)=dlsym(library,"eglCreateContext");
 void* (*surface)(void*,void*,const int*)=dlsym(library,"eglCreatePbufferSurface");
 unsigned (*current)(void*,void*,void*,void*)=dlsym(library,"eglMakeCurrent");
 const int platform[]={0x3203,0x3450,0x348F,0x31DD,0x3038};
 void* d=display(0x3202,NULL,platform);int major,minor;
 if(!initialize(d,&major,&minor)){printf("ANGLE initialize failed EGL=0x%x\n",error());return 2;}
 const int attributes[]={0x3033,1,0x3040,0x40,0x3024,8,0x3023,8,0x3022,8,0x3038};
 void* config;int count;if(!choose(d,attributes,&config,1,&count)||!count||!bindAPI(0x30A0))return 3;
 const int ctx[]={0x3098,3,0x3038},pbuf[]={0x3057,1,0x3056,1,0x3038};
 void* c=context(d,config,NULL,ctx),*s=surface(d,config,pbuf);
 if(!c||!s||!current(d,s,s,c)){printf("ANGLE context failed EGL=0x%x\n",error());return 4;}
 const unsigned char* (*string)(unsigned)=getProc("glGetString");
 void (*color)(float,float,float,float)=getProc("glClearColor");
 void (*clear)(unsigned)=getProc("glClear");
 void (*read)(int,int,int,int,unsigned,unsigned,void*)=getProc("glReadPixels");
 const char* renderer=(const char*)string(0x1F01);printf("ANGLE_RENDERER=%s\n",renderer);
 void (*integer)(unsigned,int*)=getProc("glGetIntegerv");
 const unsigned char* (*stringi)(unsigned,unsigned)=getProc("glGetStringi");
 int extensions=0;integer(0x821D,&extensions);
 for(int i=0;i<extensions;i++){const char* extension=(const char*)stringi(0x1F03,i);if(extension&&(strstr(extension,"semaphore")||strstr(extension,"memory_object")))printf("ANGLE_INTEROP_EXTENSION=%s\n",extension);}
 unsigned char pixel[4]={0};color(1,0,0,1);clear(0x4000);read(0,0,1,1,0x1908,0x1401,pixel);
 if(!renderer||!strstr(renderer,"Adreno")||pixel[0]!=255||pixel[1]||pixel[2])return 5;
 puts("ANGLE_HARDWARE_VULKAN_VERIFIED");return 0;
}
