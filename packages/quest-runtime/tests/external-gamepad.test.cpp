#include "external_gamepad.h"
#include <cassert>
#include <limits>
#include <cstdio>
int main(){
 QuestExternalGamepad shared{},sample{},copy{};sample.connected=1;sample.input.monotonic_ns=100;
 sample.input.buttons=1<<14;sample.input.axes[4]=1;quest::publishExternalGamepad(&shared,sample);
 assert(quest::readExternalGamepad(&shared,copy,200)&&copy.input.buttons==(1<<14)&&copy.input.axes[4]==1);
 assert(!quest::readExternalGamepad(&shared,copy,99));assert(!quest::readExternalGamepad(&shared,copy,2000000100));
 ++shared.sequence;assert(!quest::readExternalGamepad(&shared,copy,200));++shared.sequence;
 shared.magic=0;assert(!quest::readExternalGamepad(&shared,copy,200));shared.magic=sample.magic;
 shared.version=99;assert(!quest::readExternalGamepad(&shared,copy,200));shared.version=1;
 sample.connected=0;quest::publishExternalGamepad(&shared,sample);assert(!quest::readExternalGamepad(&shared,copy,200));
 assert(quest::gamepadAxis(.03f)==0&&quest::gamepadAxis(-.07f)==0);
 assert(quest::gamepadAxis(.1f,.15f)==0&&quest::gamepadAxis(.5f)==.5f);
 assert(quest::gamepadAxis(-2)==-1&&quest::gamepadAxis(2)==1&&quest::gamepadAxis(std::numeric_limits<float>::quiet_NaN())==0);
 puts("Physical gamepad: protocol, stale/disconnected input, deadzone and invalid axes passed");
}
