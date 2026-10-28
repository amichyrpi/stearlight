"""Exercise the patched allocator itself against old/new/restricted KGSL ABIs."""
from pathlib import Path
import subprocess, sys, tempfile
source=(Path(sys.argv[1])/'src/freedreno/vulkan/tu_knl_kgsl.cc').read_text()
start=source.index('static int\nquest_kgsl_free(')
helper=source[start:source.index('\nstatic VkResult\nkgsl_bo_init(',start)]
prefix=r'''#include <cstdint>
#include <cerrno>
#include <cassert>
struct kgsl_gpumem_alloc_id {uint64_t size;unsigned flags,id;uint64_t gpuaddr,mmapsize;};
struct kgsl_gpuobj_alloc {uint64_t size,flags,va_len,mmapsize;unsigned id,metadata_len;uint64_t metadata;};
struct kgsl_gpuobj_info {uint64_t gpuaddr,flags,size,va_len,va_addr;unsigned id;};
struct kgsl_gpumem_free_id {unsigned id;};
struct kgsl_gpuobj_free {uint64_t flags,priv;unsigned id,type,len;};
enum {IOCTL_KGSL_GPUMEM_ALLOC_ID=1,IOCTL_KGSL_GPUOBJ_ALLOC,IOCTL_KGSL_GPUOBJ_INFO,IOCTL_KGSL_GPUOBJ_FREE,IOCTL_KGSL_GPUMEM_FREE_ID};
constexpr uint64_t KGSL_CACHEMODE_MASK=0xc000000;
static int legacyError=0,modernError=0,infoError=0,modern=0,freed=0,freeError=0;
static uint64_t returnedFlags=0x8801;
static int safe_ioctl(int fd,int command,void* data){
 assert(fd==42);
 if(command==IOCTL_KGSL_GPUMEM_ALLOC_ID){
  if(legacyError){errno=legacyError;return -1;}
  auto* r=static_cast<kgsl_gpumem_alloc_id*>(data);r->id=7;r->gpuaddr=0x100000;r->mmapsize=0x4000;return 0;
 }
 if(command==IOCTL_KGSL_GPUOBJ_ALLOC){
  ++modern;auto* r=static_cast<kgsl_gpuobj_alloc*>(data);
  assert(r->size==0x3456&&r->flags==0x8801); // cache/access flags are preserved
  if(modernError){errno=modernError;return -1;}r->id=9;r->mmapsize=0x4000;return 0;
 }
 if(command==IOCTL_KGSL_GPUOBJ_INFO){
  auto* r=static_cast<kgsl_gpuobj_info*>(data);assert(r->id==9);
  if(infoError){errno=infoError;return -1;}r->gpuaddr=0x200000;r->size=0x4000;r->flags=returnedFlags;return 0;
 }
 if(command==IOCTL_KGSL_GPUMEM_FREE_ID){
  assert(static_cast<kgsl_gpumem_free_id*>(data)->id==9);
  if(freeError){errno=freeError;return -1;}return 0;
 }
 assert(command==IOCTL_KGSL_GPUOBJ_FREE);assert(static_cast<kgsl_gpuobj_free*>(data)->id==9);
 ++freed;errno=EIO;return 0;
}
'''
tests=r'''
int main(){
 auto request=[](){return kgsl_gpumem_alloc_id{0x3456,0x8801,0,0,0};};
 auto r=request();assert(quest_kgsl_allocate(42,&r)==0&&r.id==7&&modern==0);
 for(int error:{EACCES,EPERM,ENOTTY}){
  legacyError=error;r=request();assert(quest_kgsl_allocate(42,&r)==0);
  assert(r.id==9&&r.gpuaddr==0x200000&&r.mmapsize==0x4000&&r.flags==0x8801);
 }
 int calls=modern;
 for(int error:{ENOMEM,EIO,EINVAL}){legacyError=error;r=request();assert(quest_kgsl_allocate(42,&r)==-1&&errno==error&&modern==calls);}
 legacyError=EPERM;modernError=EACCES;r=request();assert(quest_kgsl_allocate(42,&r)==-1&&errno==EACCES&&freed==0);
 modernError=0;infoError=EINVAL;r=request();assert(quest_kgsl_allocate(42,&r)==-1&&errno==EINVAL&&freed==1);
 assert(quest_kgsl_free(42,9)==0&&freed==1);
 for(int error:{EACCES,EPERM,ENOTTY}){freeError=error;assert(quest_kgsl_free(42,9)==0);}
 assert(freed==4);for(int error:{EIO,EINVAL}){freeError=error;assert(quest_kgsl_free(42,9)==-1&&errno==error&&freed==4);}
 infoError=0;freeError=0;
 for(uint64_t flags:{uint64_t(0),uint64_t(0x4008801)}){returnedFlags=flags;r=request();assert(quest_kgsl_allocate(42,&r)==-1&&errno==EINVAL);}
 assert(freed==6);
}
'''
with tempfile.TemporaryDirectory() as directory:
 root=Path(directory);cpp=root/'alloc.cpp';cpp.write_text('#include <initializer_list>\n'+prefix+helper+tests)
 subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-Wno-missing-field-initializers',str(cpp),'-o',str(root/'alloc')],check=True)
 subprocess.run([str(root/'alloc')],check=True)
print('KGSL legacy success, modern fallback, exact flags, real OOM and error cleanup passed')
