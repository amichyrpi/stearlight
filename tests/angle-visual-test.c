#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
typedef void *EGLDisplay;typedef void *EGLConfig;typedef void *EGLContext;typedef void *EGLSurface;
typedef struct {void *visual;unsigned long visualid;int screen,depth,visual_class;unsigned long red_mask,green_mask,blue_mask;int colormap_size,bits_per_rgb;} VisualInfo;
typedef struct {unsigned long background_pixmap,background_pixel,border_pixmap,border_pixel;int bit_gravity,win_gravity,backing_store;unsigned long backing_planes,backing_pixel;int save_under;long event_mask,do_not_propagate_mask;int override_redirect;unsigned long colormap,cursor;} WindowAttrs;
int main(void){
 const char *angle_path=getenv("STEARLIGHT_ANGLE_EGL_PATH");if(!angle_path)angle_path="/opt/steam/steamrtarm64/libEGL.so";
 void *xlib=dlopen("libX11.so.6",RTLD_NOW);if(!xlib){puts(dlerror());return 8;}
 void *(*openDisplay)(const char*)=dlsym(xlib,"XOpenDisplay");
 unsigned long (*rootWindow)(void*)=dlsym(xlib,"XDefaultRootWindow");
 unsigned long (*createWindow)(void*,unsigned long,int,int,unsigned,unsigned,unsigned,unsigned long,unsigned long)=dlsym(xlib,"XCreateSimpleWindow");
 int (*mapWindow)(void*,unsigned long)=dlsym(xlib,"XMapWindow");
 int (*syncDisplay)(void*,int)=dlsym(xlib,"XSync");
 void *xd=openDisplay(0);if(!xd){puts("No X display");return 9;}
 int (*defaultScreen)(void*)=dlsym(xlib,"XDefaultScreen");
 int (*matchVisual)(void*,int,int,int,VisualInfo*)=dlsym(xlib,"XMatchVisualInfo");
 unsigned long (*createColormap)(void*,unsigned long,void*,int)=dlsym(xlib,"XCreateColormap");
 unsigned long (*createArgbWindow)(void*,unsigned long,int,int,unsigned,unsigned,unsigned,int,unsigned,void*,unsigned long,WindowAttrs*)=dlsym(xlib,"XCreateWindow");
 VisualInfo visual={0};if(!matchVisual(xd,defaultScreen(xd),32,4,&visual)){puts("No ARGB visual");return 13;}
 printf("NATIVE_ARGB_VISUAL=0x%lx depth=%d\n",visual.visualid,visual.depth);
 WindowAttrs wa={0};wa.colormap=createColormap(xd,rootWindow(xd),visual.visual,0);
 unsigned long window=createArgbWindow(xd,rootWindow(xd),0,0,32,32,0,32,1,visual.visual,(1ul<<3)|(1ul<<13),&wa);
 (void)createWindow;
 mapWindow(xd,window);syncDisplay(xd,0);

 void *lib=dlopen(angle_path,RTLD_NOW);if(!lib){puts(dlerror());return 1;}
 EGLDisplay (*getDisplay)(void*)=dlsym(lib,"eglGetDisplay");
 unsigned (*initialize)(EGLDisplay,int*,int*)=dlsym(lib,"eglInitialize");
 unsigned (*choose)(EGLDisplay,const int*,EGLConfig*,int,int*)=dlsym(lib,"eglChooseConfig");
 unsigned (*bind)(unsigned)=dlsym(lib,"eglBindAPI");
 EGLSurface (*surface)(EGLDisplay,EGLConfig,const int*)=dlsym(lib,"eglCreatePbufferSurface");
 EGLContext (*context)(EGLDisplay,EGLConfig,EGLContext,const int*)=dlsym(lib,"eglCreateContext");
 unsigned (*current)(EGLDisplay,EGLSurface,EGLSurface,EGLContext)=dlsym(lib,"eglMakeCurrent");
 unsigned (*error)(void)=dlsym(lib,"eglGetError");
 void *(*proc)(const char*)=dlsym(lib,"eglGetProcAddress");
 EGLDisplay (*platform)(unsigned,void*,const int*)=proc("eglGetPlatformDisplayEXT");
 if(!platform){puts("ANGLE platform entry point unavailable");return 7;}
 int platform_attrs[]={0x3203,0x3450,0x348F,0x31D5,0x33A3,(int)visual.visualid,0x3038};
 int major=0,minor=0,count=0;EGLDisplay d=platform(0x3202,xd,platform_attrs);
 (void)getDisplay;
 if(!initialize(d,&major,&minor)){printf("eglInitialize failed 0x%x\n",error());return 2;}
 printf("EGL %d.%d\n",major,minor);fflush(stdout);
 int attrs[]={0x3033,4,0x3040,4,0x3024,8,0x3023,8,0x3022,8,0x3021,8,0x3038};EGLConfig c=0;
 if(!choose(d,attrs,&c,1,&count)||!count){printf("eglChooseConfig failed 0x%x\n",error());return 3;}
 unsigned (*getAttr)(EGLDisplay,EGLConfig,int,int*)=dlsym(lib,"eglGetConfigAttrib");
 const char *(*queryString)(EGLDisplay,int)=dlsym(lib,"eglQueryString");
 void *(*realLookup)(void*,const char*)=dlvsym(RTLD_NEXT,"dlsym","GLIBC_2.17");
 unsigned (*nativeAttr)(EGLDisplay,EGLConfig,int,int*)=realLookup(lib,"eglGetConfigAttrib");
 int a=123,b=123;unsigned ar=getAttr(d,0,0x302E,&a),ae=error();
 unsigned br=nativeAttr(d,0,0x302E,&b),be=error();
 if(ar!=br||ae!=be||a!=b||ar!=0){puts("EGL error propagation FAILED");return 17;}
 if(proc("eglGetConfigAttrib")!=(void*)getAttr){puts("EGL proc routing FAILED");return 18;}
 void *mesa=dlopen("libEGL.so.1",RTLD_NOW|RTLD_LOCAL);if(!mesa)return 19;
 if(dlsym(mesa,"eglGetConfigAttrib")!=realLookup(mesa,"eglGetConfigAttrib")){
  puts("Non-ANGLE EGL symbol was intercepted");return 20;
 }
 printf("EGL CONTRACT PASS error=0x%x; native Mesa symbol preserved\n",ae);
 int config_visual=0,surfaces=0;getAttr(d,c,0x302E,&config_visual);getAttr(d,c,0x3033,&surfaces);
 printf("CONFIG visual=0x%x expected=0x%lx surfaces=0x%x\n",config_visual,visual.visualid,surfaces);if(config_visual!=(int)visual.visualid)return 14;(void)queryString;fflush(stdout);
 EGLSurface (*windowSurface)(EGLDisplay,EGLConfig,unsigned long,const int*)=dlsym(lib,"eglCreateWindowSurface");
 unsigned (*swap)(EGLDisplay,EGLSurface)=dlsym(lib,"eglSwapBuffers");
 bind(0x30A0);int ca[]={0x3098,2,0x3038};
 (void)surface;

 EGLSurface sf=windowSurface(d,c,window,0);if(!sf){printf("WINDOW SURFACE FAILED 0x%x\n",error());return 10;}EGLContext ct=context(d,c,0,ca);
 if(!sf||!ct||!current(d,sf,sf,ct)){printf("eglMakeCurrent failed 0x%x\n",error());return 4;}
 const unsigned char *(*string)(unsigned)=proc("glGetString");
 void (*clearColor)(float,float,float,float)=proc("glClearColor");void (*clear)(unsigned)=proc("glClear");
 void (*finish)(void)=proc("glFinish");void (*read)(int,int,int,int,unsigned,unsigned,void*)=proc("glReadPixels");
 printf("GL vendor=%s renderer=%s version=%s\n",string(0x1F00),string(0x1F01),string(0x1F02));fflush(stdout);
 clearColor(.25f,.5f,.75f,1.f);clear(0x4000);finish();uint8_t pixel[4]={0};read(0,0,1,1,0x1908,0x1401,pixel);
 printf("PIXEL %u %u %u %u\n",pixel[0],pixel[1],pixel[2],pixel[3]);if(!swap(d,sf)){printf("SWAP FAILED 0x%x\n",error());return 11;}syncDisplay(xd,0);sleep(1); void *(*getImage)(void*,unsigned long,int,int,unsigned,unsigned,unsigned long,int)=dlsym(xlib,"XGetImage"); unsigned long (*getPixel)(void*,int,int)=dlsym(xlib,"XGetPixel"); int (*destroyImage)(void*)=dlsym(xlib,"XDestroyImage"); void *image=getImage(xd,window,0,0,1,1,~0ul,2); if(!image)return 15;unsigned long actual=getPixel(image,0,0)&0xffffff;destroyImage(image);printf("XWINDOW PIXEL 0x%06lx\n",actual);if(actual!=0x407fbf&&actual!=0x4080bf)return 16;unsigned (*terminate)(EGLDisplay)=dlsym(lib,"eglTerminate");
 unsigned (*destroySurface)(EGLDisplay,EGLSurface)=dlsym(lib,"eglDestroySurface");
 unsigned (*destroyContext)(EGLDisplay,EGLContext)=dlsym(lib,"eglDestroyContext");
 current(d,0,0,0);destroySurface(d,sf);destroyContext(d,ct);
 if(proc("eglTerminate")!=(void*)terminate){puts("Terminate proc routing FAILED");return 22;}
 if(!terminate(d)){puts("Display cleanup FAILED");return 21;}
 puts("ANGLE visual, error propagation and real Xwindow pixel checks passed");return 0;
}
