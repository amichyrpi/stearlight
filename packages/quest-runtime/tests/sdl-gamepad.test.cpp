// Run with LD_PRELOAD pointing at libquest_gamepad_x86_64.so. This uses the
// real system SDL2 through dlopen/dlsym, as Mono's native imports do.
#include "gamepad_state.h"
#include "external_gamepad.h"
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <time.h>
#include <cassert>
#include <cstdlib>
#include <cstdio>
template<class T>T symbol(void* library,const char* name){auto p=reinterpret_cast<T>(dlsym(library,name));assert(p);return p;}
int main(int argc,char** argv){
 char path[]="/tmp/stearlight-gamepad-test-XXXXXX";int fd=mkstemp(path);assert(fd>=0);
 assert(ftruncate(fd,sizeof(QuestGamepadState))==0);
 auto* shared=static_cast<QuestGamepadState*>(mmap(nullptr,sizeof(QuestGamepadState),PROT_READ|PROT_WRITE,MAP_SHARED,fd,0));assert(shared!=MAP_FAILED);close(fd);
 *shared=QuestGamepadState{};setenv("STEARLIGHT_GAMEPAD_STATE",path,1);
 QuestTracking input{};input.session_visible=input.session_focused=1;input.grip[0].flags=input.grip[1].flags=15;
 char externalPath[]="/tmp/stearlight-external-test-XXXXXX";fd=mkstemp(externalPath);assert(fd>=0);assert(ftruncate(fd,sizeof(QuestExternalGamepad))==0);
 auto* physical=static_cast<QuestExternalGamepad*>(mmap(nullptr,sizeof(QuestExternalGamepad),PROT_READ|PROT_WRITE,MAP_SHARED,fd,0));assert(physical!=MAP_FAILED);close(fd);*physical=QuestExternalGamepad{};setenv("STEARLIGHT_EXTERNAL_GAMEPAD_STATE",externalPath,1);
 input.buttons[1]=QUEST_A;input.buttons[0]=QUEST_X;input.thumbstick[0][1]=.75f;input.trigger[1]=1;
 auto publish=[&](bool left,bool right){timespec now{};clock_gettime(CLOCK_MONOTONIC,&now);quest::publishGamepad(shared,quest::gamepadState(input,left,right,uint64_t(now.tv_sec)*1000000000+now.tv_nsec));};
 publish(true,true);
 void* sdl=dlopen(argc>1?argv[1]:"libSDL2-2.0.so.0",RTLD_NOW|RTLD_LOCAL);assert(sdl);
 auto init=symbol<int(*)(uint32_t)>(sdl,"SDL_Init");auto pump=symbol<void(*)()>(sdl,"SDL_PumpEvents");
 auto count=symbol<int(*)()>(sdl,"SDL_NumJoysticks");auto isController=symbol<int(*)(int)>(sdl,"SDL_IsGameController");
 auto open=symbol<void*(*)(int)>(sdl,"SDL_GameControllerOpen");auto button=symbol<uint8_t(*)(void*,int)>(sdl,"SDL_GameControllerGetButton");
 auto axis=symbol<int16_t(*)(void*,int)>(sdl,"SDL_GameControllerGetAxis");
 bool wine=getenv("STEARLIGHT_WINE_GAMEPAD")&&!strcmp(getenv("STEARLIGHT_WINE_GAMEPAD"),"1");
 auto initialized=symbol<uint32_t(*)(uint32_t)>(sdl,"SDL_WasInit");
 bool pacing=getenv("STEARLIGHT_RENPY_SDL")&&!strcmp(getenv("STEARLIGHT_RENPY_SDL"),"1");
 // Without a GL context the real SDL call fails; only the scoped Ren'Py
 // adapter may acknowledge it without calling the broken GLX thunk.
 assert(symbol<int(*)(int)>(sdl,"SDL_GL_SetSwapInterval")(1)==(pacing?0:-1));
 assert(symbol<int(*)()>(sdl,"SDL_GL_GetSwapInterval")()==(pacing?1:0));
 void* libc=dlopen("libc.so.6",RTLD_NOW);assert(libc);
 auto realLookup=symbol<void*(*)(void*,const char*)>(libc,"dlsym");
 bool dynapi=pacing&&getenv("SDL_DYNAMIC_API");
 // Embedded Ren'Py calls SDL internally, bypassing dlsym interposition.
 // Exercise those public SDK stubs directly, including shutdown/reinit.
 auto dispatch=[&](const char* name){return dynapi?realLookup(sdl,name):dlsym(sdl,name);};
 init=reinterpret_cast<decltype(init)>(dispatch("SDL_Init"));
 pump=reinterpret_cast<decltype(pump)>(dispatch("SDL_PumpEvents"));
 auto quit=reinterpret_cast<void(*)()>(dispatch("SDL_Quit"));
 auto quitSubsystem=reinterpret_cast<void(*)(uint32_t)>(dispatch("SDL_QuitSubSystem"));
 auto initSubsystem=reinterpret_cast<int(*)(uint32_t)>(dispatch("SDL_InitSubSystem"));
 assert(init(0x3000)==0);assert(bool(initialized(0x1000))!=wine);
 quitSubsystem(0x1000);
 assert(initSubsystem(0x1000)==0);
 assert(bool(initialized(0x1000))!=wine);
 auto internalSwap=reinterpret_cast<int(*)(int)>(realLookup(sdl,"SDL_GL_SetSwapInterval"));assert(internalSwap);
 assert(internalSwap(1)==(dynapi?0:-1));
 if(dynapi){
  auto swap=reinterpret_cast<void(*)(void*)>(realLookup(sdl,"SDL_GL_SwapWindow"));assert(swap);
  // The dummy driver returns immediately. Pacing must still yield between
  // frames without requiring a GPU, display server, or privileged clock.
  timespec before{},after{};clock_gettime(CLOCK_MONOTONIC,&before);
  swap(nullptr);swap(nullptr);swap(nullptr);clock_gettime(CLOCK_MONOTONIC,&after);
  assert((after.tv_sec-before.tv_sec)*1000000000LL+after.tv_nsec-before.tv_nsec>=30000000);
  assert(internalSwap(0)==0);
  assert(symbol<int(*)()>(sdl,"SDL_GL_GetSwapInterval")()==0);
  assert(internalSwap(1)==0);
 }
 if(getenv("STEARLIGHT_STEAM_INPUT")&&!strcmp(getenv("STEARLIGHT_STEAM_INPUT"),"0")){
  assert(count()==0);quit();
  munmap(physical,sizeof(*physical));unlink(externalPath);munmap(shared,sizeof(QuestGamepadState));unlink(path);puts("SDL2 graphics adapter preserves gamepad opt-out");return 0;
 }
 assert(count()==1&&isController(0));void* controller=open(0);assert(controller);
 pump();assert(button(controller,0)&&button(controller,2));assert(axis(controller,1)<-24000);assert(axis(controller,5)==32767);
 publish(true,false);pump();assert(!button(controller,0)&&button(controller,2));assert(axis(controller,5)==0);
 input.session_focused=0;publish(true,true);pump();assert(!button(controller,2)&&axis(controller,1)==0);
 input.session_focused=1;publish(true,true);pump();assert(button(controller,0));
 quest::publishGamepad(shared,QuestGamepadState{});pump();assert(!button(controller,0)&&axis(controller,5)==0);
 QuestExternalGamepad pad{};pad.connected=1;pad.input.monotonic_ns=shared->monotonic_ns;timespec stamp{};clock_gettime(CLOCK_MONOTONIC,&stamp);pad.input.monotonic_ns=uint64_t(stamp.tv_sec)*1000000000+stamp.tv_nsec;
 pad.input.buttons=(1<<1)|(1<<11);pad.input.axes[0]=.9f;pad.input.axes[4]=1;quest::publishExternalGamepad(physical,pad);pump();
 assert(button(controller,1)&&button(controller,11)&&!button(controller,0));assert(axis(controller,0)>29000&&axis(controller,4)==32767);
 pad.input.monotonic_ns=0;quest::publishExternalGamepad(physical,pad);pump();assert(!button(controller,1)&&axis(controller,0)==0);
 pad.connected=0;quest::publishExternalGamepad(physical,pad);pump();assert(!button(controller,11));
 symbol<void(*)(void*)>(sdl,"SDL_GameControllerClose")(controller);quit();
 assert(init(0x2000)==0&&count()==1);quit();
 munmap(physical,sizeof(*physical));unlink(externalPath);munmap(shared,sizeof(QuestGamepadState));unlink(path);puts("SDL2 Touch/physical gamepad priority, buttons, axes, mode gates and release passed");
}
