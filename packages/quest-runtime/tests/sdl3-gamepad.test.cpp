// Run against the installed Frame SDL3 with LD_PRELOAD=libquest_android_gamepad.so.
#include "external_gamepad.h"
#include <dlfcn.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <cassert>
#include <cstdlib>
#include <cstdio>
template<class T>T symbol(void* lib,const char* name){auto value=reinterpret_cast<T>(dlsym(lib,name));assert(value);return value;}
int main(int argc,char** argv){
 assert(argc==2);char path[]="/tmp/quest-physical-gamepad-XXXXXX";int fd=mkstemp(path);assert(fd>=0);
 assert(ftruncate(fd,sizeof(QuestExternalGamepad))==0);
 auto* shared=static_cast<QuestExternalGamepad*>(mmap(nullptr,sizeof(QuestExternalGamepad),PROT_READ|PROT_WRITE,MAP_SHARED,fd,0));assert(shared!=MAP_FAILED);close(fd);
 *shared=QuestExternalGamepad{};setenv("STEARLIGHT_EXTERNAL_GAMEPAD_STATE",path,1);
 void* sdl=dlopen(argv[1],RTLD_NOW|RTLD_GLOBAL);if(!sdl){puts(dlerror());return 1;}
 auto init=symbol<bool(*)(uint32_t)>(sdl,"SDL_Init");auto pump=symbol<void(*)()>(sdl,"SDL_PumpEvents");auto get=symbol<uint32_t*(*)(int*)>(sdl,"SDL_GetJoysticks");auto free=symbol<void(*)(void*)>(sdl,"SDL_free");
 auto quit=symbol<void(*)()>(sdl,"SDL_Quit");auto open=symbol<void*(*)(uint32_t)>(sdl,"SDL_OpenGamepad");
 auto axis=symbol<int16_t(*)(void*,int)>(sdl,"SDL_GetGamepadAxis");auto button=symbol<bool(*)(void*,int)>(sdl,"SDL_GetGamepadButton");
 assert(init(0x2000));int count=-1;auto ids=get(&count);assert(count==0);free(ids);
 QuestExternalGamepad value{};value.connected=1;value.vendor=0x057e;value.product=0x2009;strcpy(value.name,"Pro Controller");
 auto publish=[&](){timespec t{};clock_gettime(CLOCK_MONOTONIC,&t);value.input.monotonic_ns=uint64_t(t.tv_sec)*1000000000+t.tv_nsec;quest::publishExternalGamepad(shared,value);};
 value.input.buttons=(1<<0)|(1<<14);value.input.axes[0]=.75f;value.input.axes[4]=1;publish();pump();
 ids=get(&count);assert(count==1);void* gamepad=open(ids[0]);free(ids);assert(gamepad);pump();
 assert(button(gamepad,0)&&button(gamepad,14));assert(axis(gamepad,0)>24000&&axis(gamepad,4)==32767);
 value.input=QuestGamepadState{};publish();pump();assert(!button(gamepad,0)&&axis(gamepad,0)==0&&axis(gamepad,4)==0);
 symbol<void(*)(void*)>(sdl,"SDL_CloseGamepad")(gamepad);
 value.connected=0;publish();pump();ids=get(&count);assert(count==0);free(ids);
 value.connected=1;value.input.monotonic_ns=0;quest::publishExternalGamepad(shared,value);pump();ids=get(&count);assert(count==0);free(ids);
 publish();pump();ids=get(&count);assert(count==1);free(ids);quit();
 assert(init(0x2000));ids=get(&count);assert(count==1);free(ids);quit();
 munmap(shared,sizeof(*shared));unlink(path);puts("SDL3 physical gamepad: detection, buttons, axes, disconnect, stale state and restart passed");
}
