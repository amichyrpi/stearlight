// Run on Quest while the dashboard is visible. Exercises the real capture hook.
#include <X11/Xlib.h>
#include <X11/extensions/XShm.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "../linux/share-mirror.h"
extern char* program_invocation_short_name;
int main(void){
 void* x=dlopen("/usr/lib/aarch64-linux-gnu/libX11.so.6",RTLD_NOW);
 void* adapter=dlopen("/opt/quest/libquest_socket.so",RTLD_NOW);
 if(!x||!adapter){puts(dlerror());return 1;}
 void* gl=dlopen("/usr/lib/aarch64-linux-gnu/libGL.so.1",RTLD_NOW);if(!gl)return 8;
 void*(*proc)(const unsigned char*)=dlsym(adapter,"glXGetProcAddress");if(!proc)return 9;
 program_invocation_short_name="mirror-probe";
 if(!proc((const unsigned char*)"glXBindTexImageEXT"))return 10;
 program_invocation_short_name="steam";
 if(proc((const unsigned char*)"glXBindTexImageEXT")||!proc((const unsigned char*)"glGetString"))return 11;
 Display*(*open_display)(const char*)=dlsym(x,"XOpenDisplay");
 Window(*root)(Display*)=dlsym(x,"XDefaultRootWindow");
 Bool(*capture)(Display*,Drawable,XImage*,int,int,unsigned long)=dlsym(adapter,"XShmGetImage");
 Display* display=open_display(":0");if(!display||!capture)return 2;
 program_invocation_short_name="steam";
 XImage image={0};image.width=QUEST_SHARE_WIDTH;image.height=QUEST_SHARE_HEIGHT;image.bits_per_pixel=32;
 image.bytes_per_line=QUEST_SHARE_WIDTH*4;image.red_mask=0xff0000;image.green_mask=0xff00;image.blue_mask=0xff;
 image.data=calloc(QUEST_SHARE_HEIGHT,image.bytes_per_line);if(!image.data)return 3;
 struct QuestShareMirror* mirror=quest_share_map(1);if(!mirror)return 4;
 uint64_t initial=__atomic_load_n(&mirror->frame,__ATOMIC_ACQUIRE);
 for(int attempt=0;attempt<20;attempt++){
  if(!capture(display,root(display),&image,0,0,~0ul))return 5;
  size_t colored=0;for(size_t i=0;i<sizeof(mirror->pixels);i+=4)if(image.data[i]||image.data[i+1]||image.data[i+2])colored++;
  uint64_t frame=__atomic_load_n(&mirror->frame,__ATOMIC_ACQUIRE);
  if(frame&&frame!=initial&&colored>1000){
   FILE* out=fopen("/run/stearlight/quest/share-mirror-test.bgra","wb");if(!out)return 6;
   fwrite(image.data,1,sizeof(mirror->pixels),out);fclose(out);
   printf("QUEST_SHARE_MIRROR_VERIFIED frame=%llu colored=%zu %dx%d\n",(unsigned long long)frame,colored,image.width,image.height);
   free(image.data);munmap(mirror,sizeof(*mirror));return 0;
  }
  usleep(100000);
 }
 fprintf(stderr,"No live compositor pixels reached the sharing backend\n");return 7;
}
