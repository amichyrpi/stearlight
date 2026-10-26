"""Compile and exercise the actual patched Mesa KGSL helper with mocked ioctls/FDs."""
from pathlib import Path
import subprocess,tempfile,sys
if len(sys.argv)!=2:
    raise SystemExit('Usage: python3 '+Path(__file__).name+' /path/to/patched/mesa-26.1.6')
source=(Path(sys.argv[1])/'src/freedreno/vulkan/tu_knl_kgsl.cc').read_text()
functions=source[source.index('static int\nsync_merge_close('):source.index('\nstruct vk_kgsl_syncobj\n')]
prefix=r'''
#include <cassert>
#include <cstdint>
#include <map>
#include <algorithm>
enum {KGSL_SYNCOBJ_STATE_UNSIGNALED,KGSL_SYNCOBJ_STATE_SIGNALED,KGSL_SYNCOBJ_STATE_TS,KGSL_SYNCOBJ_STATE_FD};
struct tu_queue {int id;};
struct kgsl_syncobj {int state;tu_queue *queue;uint32_t timestamp;int fd;};
using Values=std::map<int,uint32_t>;
static std::map<int,Values> descriptors;
static int next_fd=1;
static int make_fd(Values v){int fd=next_fd++;descriptors[fd]=v;return fd;}
static int close(int fd){assert(descriptors.erase(fd)==1);return 0;}
static int os_dupfd_cloexec(int fd){assert(descriptors.count(fd));return make_fd(descriptors.at(fd));}
static int sync_merge(const char*,int a,int b){
 assert(descriptors.count(a)&&descriptors.count(b));
 Values v=descriptors.at(a);for(auto [q,t]:descriptors.at(b))v[q]=std::max(v[q],t);
 return make_fd(v);
}
static uint32_t max_ts(uint32_t a,uint32_t b){return int32_t(a-b)>=0?a:b;}
static int kgsl_syncobj_ts_to_fd(const kgsl_syncobj *s){
 assert(s->state==KGSL_SYNCOBJ_STATE_TS&&s->queue);return make_fd({{s->queue->id,s->timestamp}});
}
static void kgsl_syncobj_init(kgsl_syncobj *s,bool signaled){*s={signaled?KGSL_SYNCOBJ_STATE_SIGNALED:KGSL_SYNCOBJ_STATE_UNSIGNALED,nullptr,0,-1};}
static void kgsl_syncobj_reset(kgsl_syncobj *s){if(s->state==KGSL_SYNCOBJ_STATE_FD)close(s->fd);kgsl_syncobj_init(s,false);}
static void kgsl_syncobj_destroy(kgsl_syncobj *s){kgsl_syncobj_reset(s);}
#define UNREACHABLE(message) assert(false && message)
'''
tests=r'''
int main(){
 tu_queue q1{1},q2{2};
 kgsl_syncobj a{KGSL_SYNCOBJ_STATE_TS,&q1,3,-1},b{KGSL_SYNCOBJ_STATE_TS,&q2,7,-1};
 kgsl_syncobj file{KGSL_SYNCOBJ_STATE_FD,nullptr,0,make_fd({{3,4}})};
 const kgsl_syncobj *ts[]={&a,&b};auto result=kgsl_syncobj_merge(ts,2);
 assert(result.state==KGSL_SYNCOBJ_STATE_FD);assert((descriptors.at(result.fd)==Values{{1,3},{2,7}}));kgsl_syncobj_destroy(&result);
 const kgsl_syncobj *mixed[]={&a,&file};result=kgsl_syncobj_merge(mixed,2);
 assert((descriptors.at(result.fd)==Values{{1,3},{3,4}}));assert(descriptors.count(file.fd));kgsl_syncobj_destroy(&result);
 const kgsl_syncobj *reverse[]={&file,&a};result=kgsl_syncobj_merge(reverse,2);
 assert((descriptors.at(result.fd)==Values{{1,3},{3,4}}));assert(descriptors.count(file.fd));kgsl_syncobj_destroy(&result);
 b.queue=&q1;result=kgsl_syncobj_merge(ts,2);assert(result.state==KGSL_SYNCOBJ_STATE_TS&&result.timestamp==7);
 kgsl_syncobj pending{};kgsl_syncobj_init(&pending,false);
 const kgsl_syncobj *unsignaled[]={&file,&pending};result=kgsl_syncobj_merge(unsignaled,2);assert(result.state==KGSL_SYNCOBJ_STATE_UNSIGNALED);
 result=kgsl_syncobj_merge(nullptr,0);assert(result.state==KGSL_SYNCOBJ_STATE_SIGNALED);
 kgsl_syncobj_destroy(&file);assert(descriptors.empty());
}
'''
with tempfile.TemporaryDirectory() as folder:
    code=Path(folder)/'merge.cpp';binary=Path(folder)/'merge'
    code.write_text(prefix+functions+tests)
    subprocess.run(['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror',str(code),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True,timeout=5)
print('Actual KGSL merge helper passes cross-queue timestamps, both timestamp/FD orders, unsignaled input and FD ownership checks.')
