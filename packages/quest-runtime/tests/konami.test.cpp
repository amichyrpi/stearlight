#include "konami.h"
#include <cassert>
#include <limits>
#include <cstdio>

int main(){
 quest::Konami code;uint64_t now=1;
 auto key=[&](int key){now+=100000000;code.update(0,0,false,false,true,now);now+=100000000;
  return code.update(key==3?-1.f:key==4?1.f:0.f,key==1?1.f:key==2?-1.f:0.f,key==6,key==5,true,now);};
 constexpr int pattern[]={1,1,2,2,3,4,3,4,5,6};
 for(int repeat=0;repeat<2;repeat++)for(int i=0;i<10;i++)assert(key(pattern[i])==(i==9));
 code.reset();key(1);for(int i=0;i<1000;i++)assert(!code.update(0,1,false,false,true,++now));
 // Holding up cannot count as the second up.
 for(int i=2;i<10;i++)assert(!key(pattern[i]));
 code.reset();key(1);key(1);now+=4000000000ull;
 for(int i=2;i<10;i++)assert(!key(pattern[i]));
 code.reset();key(1);key(1);code.update(0,0,false,false,false,++now);
 for(int i=2;i<10;i++)assert(!key(pattern[i]));
 code.reset();key(1);key(1);code.update(std::numeric_limits<float>::quiet_NaN(),0,false,false,true,++now);
 for(int i=2;i<10;i++)assert(!key(pattern[i]));
 code.reset();key(1);key(1);key(3);for(int i=2;i<10;i++)assert(!key(pattern[i]));
 puts("Konami: exact sequence, repeat toggles, held inputs, timeout, focus loss and invalid axes pass");
}
