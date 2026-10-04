#define _GNU_SOURCE

#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
// Steam Frame CEF compatibility: ANGLE's Vulkan XCB backend advertises the root
// visual for every BGRA8 config, ignoring the requested EGL_X11_VISUAL_ID_ANGLE.
// The existing backend successfully renders the same format into ARGB windows.
// Correct only that metadata for a specifically requested, compatible visual.
typedef void *Display;typedef void *Config;

typedef void *(*Dlsym)(void *,const char *);
static Dlsym real_dlsym;
static pthread_once_t resolve_once=PTHREAD_ONCE_INIT;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static struct {Display display;int visual;} selected[16];
static Display (*real_platform)(unsigned,void*,const intptr_t*);
static Display (*real_platform_ext)(unsigned,void*,const int*);
static unsigned (*real_config)(Display,Config,int,int*);
static void *(*real_proc)(const char*);
static unsigned (*real_terminate)(Display);
static unsigned adapted_terminate(Display display);
static void resolve_symbols(void){
 real_dlsym=(Dlsym)dlvsym(RTLD_NEXT,"dlsym","GLIBC_2.17");
 if(!real_dlsym)abort();
}
static void *lookup(void *handle,const char *name){
 pthread_once(&resolve_once,resolve_symbols);return real_dlsym(handle,name);
}
typedef struct {void *visual;unsigned long visualid;int screen,depth,visual_class;
 unsigned long red_mask,green_mask,blue_mask;int colormap_size,bits_per_rgb;} VisualInfo;
static int compatible_argb_visual(int id){
 void *lib=dlopen("libX11.so.6",RTLD_NOW|RTLD_LOCAL);if(!lib)return 0;
 void *(*openDisplay)(const char*)=lookup(lib,"XOpenDisplay");
 VisualInfo *(*getVisual)(void*,long,VisualInfo*,int*)=lookup(lib,"XGetVisualInfo");
 int (*closeDisplay)(void*)=lookup(lib,"XCloseDisplay");
 int (*freeX)(void*)=lookup(lib,"XFree");
 if(!openDisplay||!getVisual||!closeDisplay||!freeX){dlclose(lib);return 0;}
 void *xd=openDisplay(0);if(!xd){dlclose(lib);return 0;}
 VisualInfo query={0};query.visualid=(unsigned long)id;int count=0;
 VisualInfo *match=getVisual(xd,1,&query,&count);
 int compatible=match&&count&&match->depth==32&&match->visual_class==4&&
     match->red_mask==0xff0000&&match->green_mask==0xff00&&match->blue_mask==0xff;
 if(match)freeX(match);
 closeDisplay(xd);dlclose(lib);return compatible;
}
static void remember_visual(Display display,int type,int native,int visual){
 if(!display||type!=0x3450||native!=0x31D5||visual<=0||!compatible_argb_visual(visual))return;
 pthread_mutex_lock(&lock);
 unsigned slot=16;
 for(unsigned i=0;i<16;++i){
  if(selected[i].display==display){slot=i;break;}
  if(!selected[i].display&&slot==16)slot=i;
 }
 if(slot<16){selected[slot].display=display;selected[slot].visual=visual;}
 pthread_mutex_unlock(&lock);
}
static Display adapted_platform(unsigned platform,void *native_display,const intptr_t *attrs){
 Display (*fn)(unsigned,void*,const intptr_t*)=__atomic_load_n(&real_platform,__ATOMIC_ACQUIRE);
 Display display=fn(platform,native_display,attrs);
 int type=0,native=0,visual=0;
 if(platform==0x3202&&attrs)for(unsigned i=0;i<128&&attrs[i]!=0x3038;i+=2){
  if(attrs[i]==0x3203)type=(int)attrs[i+1];
  if(attrs[i]==0x348f)native=(int)attrs[i+1];
  if(attrs[i]==0x33a3)visual=(int)attrs[i+1];
 }
 remember_visual(display,type,native,visual);return display;
}
static Display adapted_platform_ext(unsigned platform,void *native_display,const int *attrs){
 Display (*fn)(unsigned,void*,const int*)=__atomic_load_n(&real_platform_ext,__ATOMIC_ACQUIRE);
 Display display=fn(platform,native_display,attrs);
 int type=0,native=0,visual=0;
 if(platform==0x3202&&attrs)for(unsigned i=0;i<128&&attrs[i]!=0x3038;i+=2){
  if(attrs[i]==0x3203)type=attrs[i+1];
  if(attrs[i]==0x348f)native=attrs[i+1];
  if(attrs[i]==0x33a3)visual=attrs[i+1];
 }
 remember_visual(display,type,native,visual);return display;
}
static unsigned adapted_config(Display display,Config config,int attr,int *value){
 unsigned (*fn)(Display,Config,int,int*)=__atomic_load_n(&real_config,__ATOMIC_ACQUIRE);
 unsigned result=fn(display,config,attr,value);
 if(result&&value&&attr==0x302e){
  int visual=0,alpha=0,buffer=0;
  pthread_mutex_lock(&lock);
  for(unsigned i=0;i<16;++i)if(selected[i].display==display){visual=selected[i].visual;break;}
  pthread_mutex_unlock(&lock);
  if(visual&&fn(display,config,0x3021,&alpha)&&fn(display,config,0x3020,&buffer)&&alpha==8&&buffer==32)
   *value=visual;
 }
 return result;
}
static void *adapted_proc(const char *name){
 if(!name)return NULL;
 if(!strcmp(name,"eglGetConfigAttrib"))return (void*)adapted_config;
 if(!strcmp(name,"eglGetPlatformDisplay"))return (void*)adapted_platform;
 if(!strcmp(name,"eglGetPlatformDisplayEXT"))return (void*)adapted_platform_ext;
 if(!strcmp(name,"eglTerminate"))return (void*)adapted_terminate;
 void *(*fn)(const char*)=__atomic_load_n(&real_proc,__ATOMIC_ACQUIRE);return fn(name);
}

static unsigned adapted_terminate(Display display){
 unsigned (*fn)(Display)=__atomic_load_n(&real_terminate,__ATOMIC_ACQUIRE);
 unsigned result=fn(display);
 if(result){
  pthread_mutex_lock(&lock);
  for(unsigned i=0;i<16;++i)if(selected[i].display==display){selected[i].display=NULL;selected[i].visual=0;}
  pthread_mutex_unlock(&lock);
 }
 return result;
}

void *dlsym(void *handle,const char *name){
 void *function=lookup(handle,name);
 if(!function)return function;
 if(strcmp(name,"eglGetPlatformDisplay")&&strcmp(name,"eglGetPlatformDisplayEXT")&&
    strcmp(name,"eglGetConfigAttrib")&&strcmp(name,"eglGetProcAddress")&&strcmp(name,"eglTerminate"))
  return function;
 Dl_info info={0};
 const char *expected=getenv("STEARLIGHT_ANGLE_EGL_PATH");
 if(!expected)expected="/opt/steam/steamrtarm64/libEGL.so";
 if(!dladdr(function,&info)||!info.dli_fname)return function;
 char *loaded_path=realpath(info.dli_fname,NULL),*expected_path=realpath(expected,NULL);
 int matches=loaded_path&&expected_path&&!strcmp(loaded_path,expected_path);
 free(loaded_path);free(expected_path);
 if(!matches)return function;
 if(!strcmp(name,"eglGetPlatformDisplay")){
  __atomic_store_n(&real_platform,(Display(*)(unsigned,void*,const intptr_t*))function,__ATOMIC_RELEASE);
  return (void*)adapted_platform;
 }
 if(!strcmp(name,"eglGetPlatformDisplayEXT")){
  __atomic_store_n(&real_platform_ext,(Display(*)(unsigned,void*,const int*))function,__ATOMIC_RELEASE);
  return (void*)adapted_platform_ext;
 }
 if(!strcmp(name,"eglGetConfigAttrib")){
  __atomic_store_n(&real_config,(unsigned(*)(Display,Config,int,int*))function,__ATOMIC_RELEASE);
  return (void*)adapted_config;
 }
 if(!strcmp(name,"eglGetProcAddress")){
  // eglGetProcAddress may be queried before these APIs are bound individually.
  __atomic_store_n(&real_platform,(Display(*)(unsigned,void*,const intptr_t*))lookup(handle,"eglGetPlatformDisplay"),__ATOMIC_RELEASE);
  __atomic_store_n(&real_platform_ext,(Display(*)(unsigned,void*,const int*))lookup(handle,"eglGetPlatformDisplayEXT"),__ATOMIC_RELEASE);
  __atomic_store_n(&real_config,(unsigned(*)(Display,Config,int,int*))lookup(handle,"eglGetConfigAttrib"),__ATOMIC_RELEASE);
  __atomic_store_n(&real_terminate,(unsigned(*)(Display))lookup(handle,"eglTerminate"),__ATOMIC_RELEASE);
  __atomic_store_n(&real_proc,(void*(*)(const char*))function,__ATOMIC_RELEASE);
  return (void*)adapted_proc;
 }
 __atomic_store_n(&real_terminate,(unsigned(*)(Display))function,__ATOMIC_RELEASE);
 return (void*)adapted_terminate;
}
