// Steam's GLX pixmap capture disconnects from Xvfb on this Zink runtime.
// Select Valve's X11 capture backend, supplying the actual compositor mirror.
#include <X11/Xlib.h>
#include <X11/extensions/XShm.h>
#include "share-mirror.h"
extern char* program_invocation_short_name;
static int quest_steam_capture(void){return program_invocation_short_name&&!strcmp(program_invocation_short_name,"steam");}
static void* quest_gl_proc(const unsigned char* name){
 typedef void*(*function)(const unsigned char*);static _Atomic(function) cached;
 function real=atomic_load(&cached);
 if(!real){void* lib=dlopen("libGL.so.1",RTLD_NOW|RTLD_NOLOAD);if(lib)real=(function)dlsym(lib,"glXGetProcAddress");if(real)atomic_store(&cached,real);}
 return real?real(name):NULL;
}
void* glXGetProcAddress(const unsigned char* name){
 if(quest_steam_capture()&&!strcmp((const char*)name,"glXBindTexImageEXT"))return NULL;
 return quest_gl_proc(name);
}
static int quest_capture_image(Display* display,Drawable drawable,XImage* image){
 if(!quest_steam_capture()||!image||image->width!=QUEST_SHARE_WIDTH||image->height!=QUEST_SHARE_HEIGHT||
    image->bits_per_pixel!=32||image->byte_order!=LSBFirst||image->red_mask!=0xff0000||image->blue_mask!=0xff||
    image->bytes_per_line<QUEST_SHARE_WIDTH*4)return 0;
 typedef Window(*root_function)(Display*);static root_function root;
 if(!root){void* lib=dlopen("libX11.so.6",RTLD_NOW|RTLD_NOLOAD);if(lib)root=(root_function)dlsym(lib,"XDefaultRootWindow");}
 if(!root||drawable!=root(display))return 0;
 static struct QuestShareMirror* mirror;
 if(!mirror){mirror=quest_share_map(1);if(!mirror)return 0;}
 __atomic_store_n(&mirror->requested_ns,quest_share_now(),__ATOMIC_RELEASE);
 // Pace capture to fresh compositor frames instead of converting/encoding
 // the same image at the PC monitor's (often much higher) refresh rate.
 static _Thread_local uint64_t last_frame;
 uint32_t wait_sequence=__atomic_load_n(&mirror->sequence,__ATOMIC_ACQUIRE);
 if(__atomic_load_n(&mirror->frame,__ATOMIC_RELAXED)==last_frame){
  struct timespec timeout={0,50000000};
  syscall(SYS_futex,&mirror->sequence,FUTEX_WAIT,wait_sequence,&timeout,NULL,0);
 }
 for(int attempt=0;attempt<3;attempt++){
  uint32_t sequence=__atomic_load_n(&mirror->sequence,__ATOMIC_ACQUIRE);if(sequence&1)continue;
  uint64_t captured=__atomic_load_n(&mirror->frame,__ATOMIC_RELAXED);
  for(unsigned y=0;y<QUEST_SHARE_HEIGHT;y++)memcpy(image->data+(size_t)y*image->bytes_per_line,mirror->pixels+(size_t)y*QUEST_SHARE_WIDTH*4,QUEST_SHARE_WIDTH*4);
  __atomic_thread_fence(__ATOMIC_ACQUIRE);
  if(sequence==__atomic_load_n(&mirror->sequence,__ATOMIC_ACQUIRE)){last_frame=captured;return 1;}
 }
 return 1;
}
Bool XShmGetImage(Display* display,Drawable drawable,XImage* image,int x,int y,unsigned long mask){
 if(x==0&&y==0&&quest_capture_image(display,drawable,image))return True;
 typedef Bool(*function)(Display*,Drawable,XImage*,int,int,unsigned long);static function real;
 if(!real){void* lib=dlopen("libXext.so.6",RTLD_NOW|RTLD_NOLOAD);if(lib)real=(function)dlsym(lib,"XShmGetImage");}
 return real?real(display,drawable,image,x,y,mask):False;
}
XImage* XGetImage(Display* display,Drawable drawable,int x,int y,unsigned width,unsigned height,unsigned long mask,int format){
 typedef XImage*(*function)(Display*,Drawable,int,int,unsigned,unsigned,unsigned long,int);static function real;
 if(!real){void* lib=dlopen("libX11.so.6",RTLD_NOW|RTLD_NOLOAD);if(lib)real=(function)dlsym(lib,"XGetImage");}
 XImage* image=real?real(display,drawable,x,y,width,height,mask,format):NULL;
 if(x==0&&y==0)quest_capture_image(display,drawable,image);
 return image;
}
