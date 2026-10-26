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
#include <poll.h>
using VkResult=int;
constexpr int VK_SUCCESS=0,VK_TIMEOUT=2,VK_ERROR_UNKNOWN=-13;
constexpr int KGSL_TIMESTAMP_RETIRED=2;
constexpr int IOCTL_KGSL_CMDSTREAM_READTIMESTAMP_CTXTID=1;
constexpr int IOCTL_KGSL_DEVICE_WAITTIMESTAMP_CTXTID=2;
constexpr int IOCTL_KGSL_TIMESTAMP_EVENT=3,KGSL_TIMESTAMP_EVENT_FENCE=1;
struct kgsl_cmdstream_readtimestamp_ctxtid {unsigned context_id,type,timestamp;};
struct kgsl_device_waittimestamp_ctxtid {unsigned context_id,timestamp,timeout;};
struct kgsl_timestamp_event {unsigned type,timestamp,context_id;void *priv;unsigned len;};
static uint64_t now=1000000000;
static unsigned retired=5,polls=0,waits=0;
static int read_errno=0,export_errno=0,poll_result=0;
static unsigned exported=0,closed=0;
static short poll_events=POLLIN;
static bool interrupt_once=false,poll_interrupt_once=false;
static uint64_t os_time_get_nano(){return now;}
static int safe_ioctl(int,int command,void *arg){
 if(command==IOCTL_KGSL_TIMESTAMP_EVENT){
  if(export_errno){errno=export_errno;return -1;}
  auto *event=static_cast<kgsl_timestamp_event*>(arg);
  assert(event->type==KGSL_TIMESTAMP_EVENT_FENCE&&event->context_id==3);
  assert(event->len==sizeof(int));*static_cast<int*>(event->priv)=77;++exported;return 0;
 }
 assert(command==IOCTL_KGSL_CMDSTREAM_READTIMESTAMP_CTXTID);++polls;
 if(read_errno){errno=read_errno;return -1;}
 auto *read=static_cast<kgsl_cmdstream_readtimestamp_ctxtid*>(arg);
 assert(read->type==KGSL_TIMESTAMP_RETIRED);read->timestamp=retired;return 0;
}
static int test_poll(pollfd *fds,nfds_t count,int timeout){
 assert(count==1&&fds->fd==77&&fds->events==POLLIN&&timeout==0);
 if(poll_interrupt_once){poll_interrupt_once=false;errno=EINTR;return -1;}
 fds->revents=poll_events;if(poll_result<0)errno=EIO;return poll_result;
}
static int test_close(int fd){assert(fd==77);++closed;return 0;}
#define poll test_poll
#define close test_close
static int ioctl(int,int command,void *arg){
 assert(command==IOCTL_KGSL_DEVICE_WAITTIMESTAMP_CTXTID);++waits;
 auto *wait=static_cast<kgsl_device_waittimestamp_ctxtid*>(arg);
 assert(wait->timeout!=0);
 if(interrupt_once){interrupt_once=false;now+=20000000;errno=EINTR;return -1;}
 return 0;
}
'''
tests=r'''
int main(int argc,char **argv){
 assert(wait_timestamp_safe(1,3,6,0)==VK_TIMEOUT);
 assert(polls==1&&waits==0);
 assert(wait_timestamp_safe(1,3,5,0)==VK_SUCCESS);
 assert(wait_timestamp_safe(1,3,5,now-1)==VK_SUCCESS);
 retired=1;assert(wait_timestamp_safe(1,3,UINT32_MAX,0)==VK_SUCCESS);
 retired=5;assert(wait_timestamp_safe(1,3,6,now+10000000)==VK_SUCCESS);
 assert(waits==1);
 interrupt_once=true;assert(wait_timestamp_safe(1,3,5,now+10000000)==VK_SUCCESS);
 assert(waits==2);
 read_errno=EIO;assert(wait_timestamp_safe(1,3,5,0)==VK_ERROR_UNKNOWN);
 read_errno=0;assert(wait_timestamp_safe(1,3,6,UINT64_MAX)==VK_SUCCESS);
 // Android rejects the retired-timestamp ioctl; fallback must never block,
 // leak an exported fence, or mistake a driver error for GPU completion.
 read_errno=argc>1&&argv[1][0]=='p'?EPERM:argc>1&&argv[1][0]=='n'?ENOTTY:EACCES;
 assert(wait_timestamp_safe(1,3,6,0)==VK_TIMEOUT);
 unsigned previous_polls=polls;
 read_errno=EIO;poll_result=1;
 assert(wait_timestamp_safe(1,3,6,0)==VK_SUCCESS&&polls==previous_polls);
 poll_interrupt_once=true;assert(wait_timestamp_safe(1,3,6,0)==VK_SUCCESS);
 poll_events=POLLERR;assert(wait_timestamp_safe(1,3,6,0)==VK_ERROR_UNKNOWN);
 poll_events=POLLNVAL;assert(wait_timestamp_safe(1,3,6,0)==VK_ERROR_UNKNOWN);
 poll_events=0;poll_result=-1;assert(wait_timestamp_safe(1,3,6,0)==VK_ERROR_UNKNOWN);
 export_errno=ENOMEM;assert(wait_timestamp_safe(1,3,6,0)==VK_ERROR_UNKNOWN);
 assert(exported==closed&&exported==6);
}
'''
with tempfile.TemporaryDirectory() as folder:
    code=Path(folder)/'poll.cpp';binary=Path(folder)/'poll'
    code.write_text(prefix+compare+functions+tests)
    subprocess.run(['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-Wno-missing-field-initializers',str(code),'-o',str(binary)],check=True)
    for denied in ('access','permission','notty'):
        subprocess.run([str(binary),denied],check=True,timeout=5)
print('Actual KGSL helpers pass timestamp probes, Android-denial fallback, nonblocking fence polls, interruption, errors and FD ownership checks.')
