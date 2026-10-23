"""Compile and exercise the actual patched Mesa KGSL helper with mocked ioctls/FDs."""
from pathlib import Path
import subprocess,tempfile,sys
if len(sys.argv)!=2:
    raise SystemExit('Usage: python3 '+Path(__file__).name+' /path/to/patched/mesa-26.1.6')
source=(Path(sys.argv[1])/'src/freedreno/vulkan/tu_knl_kgsl.cc').read_text()
compare=source[source.index('static inline bool\ntimestamp_cmp('):source.index('static uint32_t\nmax_ts(')]
functions=source[source.index('static int\nget_relative_ms('):source.index('\nVkResult\nkgsl_queue_wait_fence(')]
prefix=r'''
#include <cassert>
#include <cstdint>
#include <cerrno>
using VkResult=int;
constexpr int VK_SUCCESS=0,VK_TIMEOUT=2,VK_ERROR_UNKNOWN=-13;
constexpr int KGSL_TIMESTAMP_RETIRED=2;
constexpr int IOCTL_KGSL_CMDSTREAM_READTIMESTAMP_CTXTID=1;
constexpr int IOCTL_KGSL_DEVICE_WAITTIMESTAMP_CTXTID=2;
struct kgsl_cmdstream_readtimestamp_ctxtid {unsigned context_id,type,timestamp;};
struct kgsl_device_waittimestamp_ctxtid {unsigned context_id,timestamp,timeout;};
static uint64_t now=1000000000;
static unsigned retired=5,polls=0,waits=0;
static bool read_error=false,interrupt_once=false;
static uint64_t os_time_get_nano(){return now;}
static int safe_ioctl(int,int command,void *arg){
 assert(command==IOCTL_KGSL_CMDSTREAM_READTIMESTAMP_CTXTID);++polls;
 if(read_error){errno=EIO;return -1;}
 auto *read=static_cast<kgsl_cmdstream_readtimestamp_ctxtid*>(arg);
 assert(read->type==KGSL_TIMESTAMP_RETIRED);read->timestamp=retired;return 0;
}
static int ioctl(int,int command,void *arg){
 assert(command==IOCTL_KGSL_DEVICE_WAITTIMESTAMP_CTXTID);++waits;
 auto *wait=static_cast<kgsl_device_waittimestamp_ctxtid*>(arg);
 assert(wait->timeout!=0);
 if(interrupt_once){interrupt_once=false;now+=20000000;errno=EINTR;return -1;}
 return 0;
}
'''
tests=r'''
int main(){
 assert(wait_timestamp_safe(1,3,6,0)==VK_TIMEOUT);
 assert(polls==1&&waits==0);
 assert(wait_timestamp_safe(1,3,5,0)==VK_SUCCESS);
 assert(wait_timestamp_safe(1,3,5,now-1)==VK_SUCCESS);
 retired=1;assert(wait_timestamp_safe(1,3,UINT32_MAX,0)==VK_SUCCESS);
 retired=5;assert(wait_timestamp_safe(1,3,6,now+10000000)==VK_SUCCESS);
 assert(waits==1);
 interrupt_once=true;assert(wait_timestamp_safe(1,3,5,now+10000000)==VK_SUCCESS);
 assert(waits==2);
 read_error=true;assert(wait_timestamp_safe(1,3,5,0)==VK_ERROR_UNKNOWN);
 read_error=false;assert(wait_timestamp_safe(1,3,6,UINT64_MAX)==VK_SUCCESS);
}
'''
with tempfile.TemporaryDirectory() as folder:
    code=Path(folder)/'poll.cpp';binary=Path(folder)/'poll'
    code.write_text(prefix+compare+functions+tests)
    subprocess.run(['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-Wno-missing-field-initializers',str(code),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True,timeout=5)
print('Actual KGSL helpers pass zero/expired probes, wraparound, finite/infinite waits, interruption and read-error checks.')
