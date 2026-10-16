// SDL2 gamepad input for unprivileged local games. No kernel input device or
// background worker is needed. SteamVR owns the per-hand gamepad-mode gates.
#include "gamepad_state.h"
#include <dlfcn.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <cstring>
#include <cmath>
#include <cstdlib>

namespace {
using Lookup=void*(*)(void*,const char*);
Lookup lookup(){
 static Lookup fn=nullptr;
 if(fn)return fn;
#if defined(__aarch64__)
 fn=reinterpret_cast<Lookup>(dlvsym(RTLD_NEXT,"dlsym","GLIBC_2.17"));
#else
 fn=reinterpret_cast<Lookup>(dlvsym(RTLD_NEXT,"dlsym","GLIBC_2.2.5"));
#endif
 return fn;
}
struct API {
 int(*init)(uint32_t)=nullptr;int(*subsystem)(uint32_t)=nullptr;
 void(*pump)()=nullptr;int(*poll)(void*)=nullptr;int(*wait)(void*,int)=nullptr;
 void(*update)()=nullptr;void(*controllerUpdate)()=nullptr;
 int(*attach)(int,int,int,int)=nullptr;void*(*open)(int)=nullptr;
 int(*axis)(void*,int,int16_t)=nullptr;int(*button)(void*,int,uint8_t)=nullptr;
 void(*quit)()=nullptr;void(*quitSubsystem)(uint32_t)=nullptr;
 uint32_t(*wasInit)(uint32_t)=nullptr;
} api;
const QuestGamepadState* shared=nullptr;void* joystick=nullptr;
thread_local bool busy=false;
void load(void* library){
 auto fn=lookup();if(!fn||api.init)return;
#define LOAD(field,name) api.field=reinterpret_cast<decltype(api.field)>(fn(library,name))
 LOAD(init,"SDL_Init");LOAD(subsystem,"SDL_InitSubSystem");LOAD(pump,"SDL_PumpEvents");
 LOAD(poll,"SDL_PollEvent");LOAD(wait,"SDL_WaitEventTimeout");
 LOAD(update,"SDL_JoystickUpdate");LOAD(controllerUpdate,"SDL_GameControllerUpdate");
 LOAD(open,"SDL_JoystickOpen");LOAD(axis,"SDL_JoystickSetVirtualAxis");LOAD(button,"SDL_JoystickSetVirtualButton");
 LOAD(quit,"SDL_Quit");LOAD(quitSubsystem,"SDL_QuitSubSystem");LOAD(wasInit,"SDL_WasInit");
#undef LOAD
 // The common entry points remain transparent for an SDL3-only process.
 // Its virtual-joystick ABI differs; only attach through the SDL2 contract.
 if(!fn(library,"SDL_GetJoysticks"))api.attach=reinterpret_cast<decltype(api.attach)>(fn(library,"SDL_JoystickAttachVirtual"));
}
bool map(){
 if(shared)return true;
 const char* path=getenv("STEARLIGHT_GAMEPAD_STATE");
 int fd=open(path?path:"/run/stearlight/quest/gamepad",O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
 if(fd<0)return false;
 struct stat status{};void* mapping=MAP_FAILED;
 if(fstat(fd,&status)==0&&status.st_size==sizeof(QuestGamepadState))mapping=mmap(nullptr,sizeof(QuestGamepadState),PROT_READ,MAP_SHARED,fd,0);
 close(fd);if(mapping==MAP_FAILED)return false;
 shared=static_cast<const QuestGamepadState*>(mapping);return true;
}
void feed(){
 if(busy||!api.attach||!api.open||!api.axis||!api.button||!map())return;
 busy=true;
 if(!joystick){int index=api.attach(1,6,15,0);if(index>=0){joystick=api.open(index);if(joystick){const char message[]="Quest SDL gamepad attached\n";[[maybe_unused]] auto written=write(STDERR_FILENO,message,sizeof(message)-1);}}}
 if(joystick){
  QuestGamepadState value;timespec now{};clock_gettime(CLOCK_MONOTONIC,&now);
  uint64_t ns=uint64_t(now.tv_sec)*1000000000+now.tv_nsec;
  if(!quest::readGamepad(shared,value)||ns<value.monotonic_ns||ns-value.monotonic_ns>500000000)value=QuestGamepadState{};
  for(int i=0;i<6;i++){
   float axis=std::isfinite(value.axes[i])?std::clamp(value.axes[i],-1.f,1.f):(i>=4?-1.f:0.f);
   api.axis(joystick,i,static_cast<int16_t>(axis<0?axis*32768:axis*32767));
  }
  for(int i=0;i<15;i++)api.button(joystick,i,(value.buttons>>i)&1);
 }
 busy=false;
}
void reset(){
 if(busy)return;
 busy=true;
 // SDL's subsystem shutdown owns its joystick handles and virtual devices.
 // Closing them again here races its controller-removal callbacks.
 joystick=nullptr;
 // Retain this single read-only page across SDL subsystem restarts. The
 // process releases it on exit; restarting SDL allocates no additional page.
 busy=false;
}
}
extern "C" {
int SDL_Init(uint32_t flags){if(!api.init)load(RTLD_NEXT);int result=api.init?api.init(flags):-1;if(result==0&&(flags&0x2200))feed();return result;}
int SDL_InitSubSystem(uint32_t flags){if(!api.subsystem)load(RTLD_NEXT);int result=api.subsystem?api.subsystem(flags):-1;if(result==0&&(flags&0x2200))feed();return result;}
void SDL_PumpEvents(){feed();if(api.pump)api.pump();}
int SDL_PollEvent(void* event){feed();return api.poll?api.poll(event):0;}
int SDL_WaitEventTimeout(void* event,int timeout){feed();return api.wait?api.wait(event,timeout):0;}
void SDL_JoystickUpdate(){feed();if(api.update)api.update();}
void SDL_GameControllerUpdate(){feed();if(api.controllerUpdate)api.controllerUpdate();}
void SDL_Quit(){reset();bool previous=busy;busy=true;if(api.quit)api.quit();busy=previous;}
void SDL_QuitSubSystem(uint32_t flags){bool previous=busy;busy=true;if(api.quitSubsystem)api.quitSubsystem(flags);busy=previous;if((flags&0x2200)&&api.wasInit&&!api.wasInit(0x200))reset();}
void* dlsym(void* library,const char* name){
 auto fn=lookup();if(!fn)return nullptr;
 void* original=fn(library,name);
 if(!original||strncmp(name,"SDL_",4))return original;
 if(fn(library,"SDL_GetJoysticks"))return original;
 load(library);if(!api.attach)return original;
#define HOOK(symbol) if(!strcmp(name,#symbol))return reinterpret_cast<void*>(&symbol)
 HOOK(SDL_Init);HOOK(SDL_InitSubSystem);HOOK(SDL_PumpEvents);HOOK(SDL_PollEvent);
 HOOK(SDL_WaitEventTimeout);HOOK(SDL_JoystickUpdate);HOOK(SDL_GameControllerUpdate);
 HOOK(SDL_Quit);HOOK(SDL_QuitSubSystem);
#undef HOOK
 return original;
}
}
