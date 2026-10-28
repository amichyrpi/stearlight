// Exercise the production feed loop with the real shared-memory reader.
#include "../linux/sdl3-gamepad.cpp"
#include <cassert>
#include <cstdio>
namespace {
int axesSent=0,buttonsSent=0,attached=0,closed=0,detached=0;
std::array<int,6> axisCalls{};
bool initialized=true,failAxis=false;
bool init(uint32_t){initialized=true;return true;}
uint32_t wasInit(uint32_t){return initialized?0x200:0;}
uint32_t attach(const Descriptor*){return ++attached;}
void* openPad(uint32_t){return reinterpret_cast<void*>(1);}
void closePad(void*){++closed;}
bool detachPad(uint32_t){++detached;return true;}
bool axis(void*,int index,int16_t){++axesSent;++axisCalls[index];if(failAxis){failAxis=false;return false;}return true;}
bool buttonValue(void*,int,bool){++buttonsSent;return true;}
void quit(){initialized=false;}
}
int main(){
 char path[]="/tmp/quest-sdl3-delta-XXXXXX";int fd=mkstemp(path);assert(fd>=0);
 assert(ftruncate(fd,sizeof(QuestExternalGamepad))==0);
 auto* shared=static_cast<QuestExternalGamepad*>(mmap(nullptr,sizeof(QuestExternalGamepad),PROT_READ|PROT_WRITE,MAP_SHARED,fd,0));
 assert(shared!=MAP_FAILED);close(fd);*shared={};
 setenv("STEARLIGHT_EXTERNAL_GAMEPAD_STATE",path,1);
 api.init=init;api.wasInit=wasInit;api.attach=attach;api.open=openPad;
 api.close=closePad;api.detach=detachPad;api.axis=axis;api.button=buttonValue;api.quit=quit;
 QuestExternalGamepad value{};value.connected=1;value.vendor=0x057e;value.product=0x2009;
 strcpy(value.name,"Pro Controller");value.input.axes[0]=.5f;value.input.buttons=1;
 auto publish=[&](){timespec t{};clock_gettime(CLOCK_MONOTONIC,&t);value.input.monotonic_ns=uint64_t(t.tv_sec)*1000000000+t.tv_nsec;quest::publishExternalGamepad(shared,value);};
 publish();feed();assert(attached==1&&axesSent==6&&buttonsSent==15);
 for(int i=0;i<10000;i++){publish();feed();}
 assert(axisCalls[0]==1&&axisCalls[1]==1&&axisCalls[2]==1&&axisCalls[3]==1&&buttonsSent==15);
 assert(axisCalls[4]==10001&&axisCalls[5]==10001);
 value.input.axes[0]=.75f;publish();feed();assert(axisCalls[0]==2&&buttonsSent==15);
 value.input.buttons=3;publish();feed();assert(buttonsSent==16);
 failAxis=true;value.input.axes[1]=1;publish();feed();int rejected=axisCalls[1];
 feed();assert(axisCalls[1]==rejected+1);feed();assert(axisCalls[1]==rejected+1);
 value.input.monotonic_ns=0;quest::publishExternalGamepad(shared,value);feed();
 assert(id==1&&previousAxes[0]==0&&previousAxes[1]==0&&previousButtons==0);
 int quietAxes=axesSent,quietButtons=buttonsSent;feed();
 assert(axesSent==quietAxes+2&&buttonsSent==quietButtons);
 value.connected=0;quest::publishExternalGamepad(shared,value);feed();
 assert(!id&&closed==1&&detached==1);int disconnectedAxes=axesSent;
 value.connected=1;publish();feed();
 assert(attached==2&&axesSent==disconnectedAxes+6&&buttonsSent==quietButtons+15);
 quest_SDL_Quit();assert(!id&&!knownAxes&&!knownButtons);
 assert(quest_SDL_Init(0x2000));assert(attached==3);
 assert(axesSent==disconnectedAxes+12&&buttonsSent==quietButtons+30);
 quest_SDL_Quit();munmap(shared,sizeof(*shared));unlink(path);
 puts("SDL3: 10000 polls suppress repeated sticks/buttons, retain SDL trigger initialization; changes, retries, stale release and reconnect pass.");
}
