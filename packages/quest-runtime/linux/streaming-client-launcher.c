#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
// Scope the glibc decoder adapter to the player. Android's shell and other
// guest utilities must never load a library linked against libstdc++/glibc.
int main(int argc,char** argv){
 (void)argc;char path[PATH_MAX],preload[PATH_MAX];ssize_t n=readlink("/proc/self/exe",path,sizeof(path)-1);
 if(n<0||(size_t)n>=sizeof(path)-6){perror("streaming launcher path");return 127;}path[n]=0;strcat(path,".real");
 if(!strcmp(strrchr(path,'/')+1,"vrlink.real"))setenv("QUEST_VIDEO_STATEFUL","1",1);
 const char* inherited=getenv("LD_PRELOAD");int length=snprintf(preload,sizeof(preload),"/opt/quest/libquest_video.so%s%s",inherited&&*inherited?":":"",inherited?inherited:"");
 if(length<0||(size_t)length>=sizeof(preload)){fputs("streaming preload path too long\n",stderr);return 127;}
 setenv("LD_PRELOAD",preload,1);setenv("EGL_PLATFORM","surfaceless",1);
 // The headless allocator must never attempt to become the KMS master.
 if(!getenv("DISPLAY")&&!getenv("WAYLAND_DISPLAY"))setenv("DISPLAY",":0",1);
 execv(path,argv);perror("Steam streaming player");return 127;
}
