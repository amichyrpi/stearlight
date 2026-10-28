// Count calls made by the production SDL2 feed, including rejected updates.
#include "../linux/sdl-gamepad.cpp"
#include <cassert>
#include <cstdio>
namespace {
std::array<int,6> calls{};int buttonsSent=0,attached=0;bool fail=false;
int attachPad(int,int,int,int){return attached++;}
void* openPad(int){return reinterpret_cast<void*>(1);}
int axisValue(void*,int i,int16_t){++calls[i];if(fail){fail=false;return -1;}return 0;}
int buttonValue(void*,int,uint8_t){++buttonsSent;return 0;}
}
int main(){
 char path[]="/tmp/quest-sdl2-delta-XXXXXX";int fd=mkstemp(path);assert(fd>=0&&!ftruncate(fd,sizeof(QuestGamepadState)));
 auto* page=static_cast<QuestGamepadState*>(mmap(nullptr,sizeof(QuestGamepadState),PROT_READ|PROT_WRITE,MAP_SHARED,fd,0));
 assert(page!=MAP_FAILED);close(fd);*page=QuestGamepadState{};setenv("STEARLIGHT_GAMEPAD_STATE",path,1);
 setenv("STEARLIGHT_EXTERNAL_GAMEPAD_STATE","/nonexistent/quest-sdl2-delta",1);
 api.attach=attachPad;api.open=openPad;api.axis=axisValue;api.button=buttonValue;
 QuestGamepadState value{};value.axes[0]=.5f;value.buttons=1;
 auto publish=[&](){timespec t{};clock_gettime(CLOCK_MONOTONIC,&t);value.monotonic_ns=uint64_t(t.tv_sec)*1000000000+t.tv_nsec;quest::publishGamepad(page,value);};
 for(int i=0;i<10000;i++){publish();feed();}
 assert(attached==1&&calls[0]==1&&calls[1]==1&&calls[2]==1&&calls[3]==1&&buttonsSent==15);
 assert(calls[4]==10000&&calls[5]==10000);
 value.axes[0]=.75f;value.buttons=3;publish();feed();assert(calls[0]==2&&buttonsSent==16);
 fail=true;value.axes[1]=1;publish();feed();int rejected=calls[1];feed();assert(calls[1]==rejected+1);feed();assert(calls[1]==rejected+1);
 value.monotonic_ns=0;quest::publishGamepad(page,value);feed();assert(previousAxes[0]==0&&previousAxes[1]==0&&previousButtons==0);
 reset();assert(!joystick&&!knownAxes&&!knownButtons);publish();feed();assert(attached==2&&knownAxes==63&&knownButtons==32767);
 reset();munmap(page,sizeof(*page));unlink(path);
 puts("SDL2: 10000 polls suppress repeated sticks/buttons; trigger refresh, changes, retries, stale release and restart pass.");
}
