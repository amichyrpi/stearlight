from pathlib import Path
import subprocess
import tempfile

header = Path(__file__).resolve().parents[1] / 'app/src/main/cpp/loading_boost.h'
with tempfile.TemporaryDirectory() as folder:
    source, binary = Path(folder) / 'check.cpp', Path(folder) / 'check'
    source.write_text('#include "' + str(header) + '"\n' + r'''
#include <cassert>
int main(){
 LoadingBoost b;
 assert(!b.update(0,true,true)); // Horizon's automatic launch boost.
 assert(!b.update(44,true,true));
 b.request(46,2);assert(b.update(46,false,true));
 assert(b.update(47,false,true));assert(!b.update(48,false,true));
 // A second interaction need not wait for the old 75-second lockout.
 b.request(50,2);assert(b.update(50,false,true));
 assert(!b.update(51,false,false));assert(!b.update(52,false,true));
 b.request(53,2);assert(b.update(53,false,true));
 assert(!b.update(55,false,true));
 LoadingBoost launch;launch.update(0,false,true);
 launch.request(46,100);assert(launch.update(46,false,true));
 assert(!launch.update(61,false,true)); // Individual requests cap at 15s.
 LoadingBoost busy;double used=0,continuous=0,longest=0;
 for(int tick=0;tick<100000;tick++){
  double now=tick*.01;
  if(tick%20==0)busy.request(now,15);
  bool on=busy.update(now,false,true);
  if(on){used+=.01;continuous+=.01;if(continuous>longest)longest=continuous;}
  else continuous=0;
  assert(used<=15+.2*now+.02);
  if(now<45)assert(!on);
 }
 assert(used>100);assert(longest<=45.02);
}
''')
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', str(source), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
    print('Activity boost: repeat bursts, launch cap, focus loss, warmup and long-run 20% budget passed')
