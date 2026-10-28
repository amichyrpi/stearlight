// Steam's SDL3 sees Android's physical gamepad through its supported virtual
// joystick API. No raw-HID poller, kernel input device, or worker is needed.
#include "external-gamepad-reader.h"
#include <dlfcn.h>
#include <time.h>
#include <cstdio>
#include <mutex>
#include <string>
#include <array>


// The ELF loader has already loaded us in Steam. Restore its inherited list
// before it launches browser helpers, musl tools or games with another SDL ABI.
__attribute__((constructor)) static void scopePreload(){
 const char* inherited=getenv("LD_PRELOAD");if(!inherited)return;
 std::string list=inherited,kept;
 for(size_t start=0;start<list.size();){size_t end=list.find_first_of(": ",start);if(end==std::string::npos)end=list.size();
  auto item=list.substr(start,end-start);auto slash=item.find_last_of('/');auto base=item.substr(slash==std::string::npos?0:slash+1);
  if(!item.empty()&&base!="libquest_android_gamepad.so"){if(!kept.empty())kept+=':';kept+=item;}
  start=end+1;
 }
 if(kept.empty())unsetenv("LD_PRELOAD");else setenv("LD_PRELOAD",kept.c_str(),1);
}

namespace {
// SDL 3.2's append-compatible public descriptor, version = sizeof(desc).
// https://wiki.libsdl.org/SDL3/SDL_VirtualJoystickDesc
struct Descriptor {
 uint32_t version;uint16_t type,padding,vendor,product,axes,buttons,balls,hats,touchpads,sensors,padding2[2];
 uint32_t buttonMask,axisMask;const char* name;const void* touchpadInfo;const void* sensorInfo;
 void* userdata;void(*update)(void*);void* callbacks[7];
};
static_assert(sizeof(Descriptor)==136,"SDL3 virtual joystick descriptor ABI");
ExternalGamepadReader reader;
std::recursive_mutex mutex;
struct API {
 bool(*init)(uint32_t)=nullptr;bool(*subsystem)(uint32_t)=nullptr;
 void(*pump)()=nullptr;bool(*poll)(void*)=nullptr;void(*update)()=nullptr;
 uint32_t*(*get)(int*)=nullptr;uint32_t(*attach)(const Descriptor*)=nullptr;
 void*(*open)(uint32_t)=nullptr;void(*close)(void*)=nullptr;bool(*detach)(uint32_t)=nullptr;
 bool(*axis)(void*,int,int16_t)=nullptr;bool(*button)(void*,int,bool)=nullptr;
 uint32_t(*wasInit)(uint32_t)=nullptr;void(*quit)()=nullptr;void(*quitSubsystem)(uint32_t)=nullptr;
} api;
using Lookup=void*(*)(void*,const char*);
Lookup lookup(){
#if defined(__aarch64__)
 static auto fn=reinterpret_cast<Lookup>(dlvsym(RTLD_NEXT,"dlsym","GLIBC_2.17"));
#else
 static auto fn=reinterpret_cast<Lookup>(dlvsym(RTLD_NEXT,"dlsym","GLIBC_2.2.5"));
#endif
 return fn;
}
void load(void* lib){
 std::lock_guard<std::recursive_mutex> guard(mutex);
 if(api.init)return;
 auto fn=lookup();if(!fn)return;
 // Resolve the unique SDL3 entry point to its owning module. A global
 // lookup can otherwise mix SDL2's common function names into this API.
 void* entry=fn(lib,"SDL_GetJoysticks");Dl_info owner{};
 if(!entry||!dladdr(entry,&owner)||!owner.dli_fname)return;
 void* sdk=dlopen(owner.dli_fname,RTLD_NOW|RTLD_NOLOAD);if(!sdk)return;
 lib=sdk; // Keep one module reference for the cached function pointers.
#define LOAD(field,name) api.field=reinterpret_cast<decltype(api.field)>(fn(lib,name))
 LOAD(init,"SDL_Init");LOAD(subsystem,"SDL_InitSubSystem");LOAD(pump,"SDL_PumpEvents");LOAD(poll,"SDL_PollEvent");
 LOAD(update,"SDL_UpdateJoysticks");LOAD(get,"SDL_GetJoysticks");LOAD(attach,"SDL_AttachVirtualJoystick");
 LOAD(open,"SDL_OpenJoystick");LOAD(close,"SDL_CloseJoystick");LOAD(detach,"SDL_DetachVirtualJoystick");
 LOAD(axis,"SDL_SetJoystickVirtualAxis");LOAD(button,"SDL_SetJoystickVirtualButton");LOAD(wasInit,"SDL_WasInit");
 LOAD(quit,"SDL_Quit");LOAD(quitSubsystem,"SDL_QuitSubSystem");
#undef LOAD
}
uint32_t id=0;void* joystick=nullptr;thread_local bool busy=false;
uint32_t vendor=0,product=0;
std::array<int16_t,6> previousAxes{};
uint32_t knownAxes=0,knownButtons=0,previousButtons=0;
void resetInputs(){knownAxes=knownButtons=previousButtons=0;}
void disconnect(){if(joystick)api.close(joystick);joystick=nullptr;if(id)api.detach(id);id=0;resetInputs();}
void feed(){
 std::unique_lock<std::recursive_mutex> guard(mutex,std::try_to_lock);if(!guard.owns_lock())return;
 if(busy)return;
 load(RTLD_NEXT);
 if(!api.attach||!api.open||!api.close||!api.detach||!api.axis||!api.button||!api.wasInit||!api.wasInit(0x200))return;
 busy=true;timespec stamp{};clock_gettime(CLOCK_MONOTONIC,&stamp);uint64_t now=uint64_t(stamp.tv_sec)*1000000000+stamp.tv_nsec;
 QuestExternalGamepad physical{};bool connected=id!=0;
 bool fresh=reader.read(physical,now,&connected);
 // A stalled XR producer releases held input, but is not a Bluetooth removal.
 // Re-enumerating Steam's pad during game loads repeatedly rebuilds bindings.
 if(id&&(!connected||(fresh&&(vendor!=physical.vendor||product!=physical.product))))disconnect();
 if(fresh&&!id){
  physical.name[sizeof(physical.name)-1]=0;
  Descriptor desc{};desc.version=sizeof(desc);desc.type=1;desc.vendor=physical.vendor;desc.product=physical.product;
  desc.axes=6;desc.buttons=15;desc.axisMask=0x3f;desc.buttonMask=0x7fff;desc.name=physical.name;
  id=api.attach(&desc);if(id){resetInputs();joystick=api.open(id);vendor=physical.vendor;product=physical.product;
   std::fprintf(stderr,"Quest physical SDL3 gamepad attached: %s\n",physical.name);
   if(!joystick){api.detach(id);id=0;}
  }
 }
 if(!fresh)physical.input=QuestGamepadState{};
 if(joystick){for(int i=0;i<6;i++){
  float value=physical.input.axes[i];if(!std::isfinite(value))value=i>=4?-1:0;value=std::clamp(value,-1.f,1.f);
  int16_t axis=static_cast<int16_t>(value<0?value*32768:value*32767);
  // SDL resets trigger centers when a gamepad is opened/remapped. Refresh
  // those two axes; repeated stick/button setters can safely be suppressed.
  if((i>=4||!(knownAxes&(1u<<i))||axis!=previousAxes[i])&&api.axis(joystick,i,axis)){
   previousAxes[i]=axis;knownAxes|=1u<<i;
  }
 }for(int i=0;i<15;i++){
  uint32_t bit=1u<<i;bool pressed=(physical.input.buttons&bit)!=0;
  if((!(knownButtons&bit)||((physical.input.buttons^previousButtons)&bit))&&api.button(joystick,i,pressed)){
   knownButtons|=bit;previousButtons=(previousButtons&~bit)|(physical.input.buttons&bit);
  }
 }}
 busy=false;
}
}
extern "C" {
static bool quest_SDL_Init(uint32_t flags){load(RTLD_NEXT);bool result=api.init&&api.init(flags);if(result)feed();return result;}
static bool quest_SDL_InitSubSystem(uint32_t flags){load(RTLD_NEXT);bool result=api.subsystem&&api.subsystem(flags);if(result)feed();return result;}
static void quest_SDL_PumpEvents(){feed();if(api.pump)api.pump();}
static bool quest_SDL_PollEvent(void* event){feed();return api.poll&&api.poll(event);}
static void quest_SDL_UpdateJoysticks(){feed();if(api.update)api.update();}
static uint32_t* quest_SDL_GetJoysticks(int* count){feed();return api.get?api.get(count):nullptr;}
static void quest_SDL_Quit(){std::lock_guard<std::recursive_mutex> guard(mutex);load(RTLD_NEXT);bool previous=busy;busy=true;if(api.quit)api.quit();id=0;joystick=nullptr;resetInputs();busy=previous;}
static void quest_SDL_QuitSubSystem(uint32_t flags){std::lock_guard<std::recursive_mutex> guard(mutex);load(RTLD_NEXT);bool previous=busy;busy=true;if(api.quitSubsystem)api.quitSubsystem(flags);if(api.wasInit&&!api.wasInit(0x200)){id=0;joystick=nullptr;resetInputs();}busy=previous;}
void* dlsym(void* lib,const char* name){auto fn=lookup();if(!fn)return nullptr;void* value=fn(lib,name);if(!value||strncmp(name,"SDL_",4))return value;
 void* entry=fn(lib,"SDL_GetJoysticks");Dl_info sdk{},actual{};
 if(!entry||!dladdr(entry,&sdk)||!dladdr(value,&actual)||sdk.dli_fbase!=actual.dli_fbase)return value;
 load(lib);
#define HOOK(symbol) if(!strcmp(name,#symbol))return reinterpret_cast<void*>(&quest_##symbol)
 HOOK(SDL_Init);HOOK(SDL_InitSubSystem);HOOK(SDL_PumpEvents);HOOK(SDL_PollEvent);HOOK(SDL_UpdateJoysticks);HOOK(SDL_GetJoysticks);HOOK(SDL_Quit);HOOK(SDL_QuitSubSystem);
#undef HOOK
 return value;}
}
