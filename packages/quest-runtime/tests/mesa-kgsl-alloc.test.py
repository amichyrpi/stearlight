"""Exercise the patched allocator and capability probe against KGSL ABIs."""
from pathlib import Path
import subprocess
import sys
import tempfile

source = (Path(sys.argv[1]) / 'src/freedreno/vulkan/tu_knl_kgsl.cc').read_text()
start = source.index('static int\nquest_kgsl_free(')
helper = source[start:source.index('\nstatic VkResult\nkgsl_bo_init(', start)]
start = source.index('static bool\nkgsl_is_memory_type_supported(')
probe = source[start:source.index('\nstatic bool\nkgsl_is_virtual_bo_supported(', start)]

prefix = r'''#include <cstdint>
#include <cerrno>
#include <cassert>
#include <initializer_list>
#include <set>
#define __user
#include "msm_kgsl.h"
static int legacyError,modernError,infoError,freeError;
static int modern,freed,legacyFreed;
static uint64_t expectedSize,expectedFlags,returnedFlags;
static std::set<unsigned> live;

static void reset(uint64_t size=0x3456,uint64_t flags=0x8801){
 assert(live.empty());
 legacyError=modernError=infoError=freeError=0;
 modern=freed=legacyFreed=0;
 expectedSize=size;expectedFlags=returnedFlags=flags;
}

static int safe_ioctl(int fd,unsigned long command,void* data){
 assert(fd==42);
 if(command==IOCTL_KGSL_GPUMEM_ALLOC_ID){
  auto* r=static_cast<kgsl_gpumem_alloc_id*>(data);
  assert(r->size==expectedSize&&r->flags==expectedFlags);
  if(legacyError){errno=legacyError;return -1;}
  r->id=7;r->gpuaddr=0x100000;r->mmapsize=0x4000;r->flags=returnedFlags;
  assert(live.insert(7).second);return 0;
 }
 if(command==IOCTL_KGSL_GPUOBJ_ALLOC){
  ++modern;auto* r=static_cast<kgsl_gpuobj_alloc*>(data);
  assert(r->size==expectedSize&&r->flags==expectedFlags);
  if(modernError){errno=modernError;return -1;}
  r->id=9;r->mmapsize=0x4000;assert(live.insert(9).second);return 0;
 }
 if(command==IOCTL_KGSL_GPUOBJ_INFO){
  auto* r=static_cast<kgsl_gpuobj_info*>(data);assert(r->id==9);
  if(infoError){errno=infoError;return -1;}
  r->gpuaddr=0x200000;r->size=0x4000;r->flags=returnedFlags;return 0;
 }
 if(command==IOCTL_KGSL_GPUMEM_FREE_ID){
  unsigned id=static_cast<kgsl_gpumem_free_id*>(data)->id;
  assert(live.count(id));
  if(freeError){errno=freeError;return -1;}
  ++legacyFreed;assert(live.erase(id)==1);return 0;
 }
 assert(command==IOCTL_KGSL_GPUOBJ_FREE);
 assert(static_cast<kgsl_gpuobj_free*>(data)->id==9);
 ++freed;assert(live.erase(9)==1);
 errno=EIO;return 0; // A successful cleanup must not hide the original errno.
}
'''

tests = r'''
int main(){
 auto request=[](){return kgsl_gpumem_alloc_id{.flags=0x8801,.size=0x3456};};
 reset();auto r=request();
 assert(quest_kgsl_allocate(42,&r)==0&&r.id==7&&modern==0);
 assert(quest_kgsl_free(42,r.id)==0&&legacyFreed==1);
 for(int error:{EACCES,EPERM,ENOTTY}){
  reset();legacyError=freeError=error;r=request();
  assert(quest_kgsl_allocate(42,&r)==0);
  assert(r.id==9&&r.gpuaddr==0x200000&&r.mmapsize==0x4000&&r.flags==0x8801);
  assert(quest_kgsl_free(42,r.id)==0&&freed==1);
 }
 for(int error:{ENOMEM,EIO,EINVAL}){
  reset();legacyError=error;r=request();
  assert(quest_kgsl_allocate(42,&r)==-1&&errno==error&&modern==0);
 }
 reset();legacyError=EPERM;modernError=EACCES;r=request();
 assert(quest_kgsl_allocate(42,&r)==-1&&errno==EACCES&&freed==0);
 reset();legacyError=EPERM;infoError=EINVAL;r=request();
 assert(quest_kgsl_allocate(42,&r)==-1&&errno==EINVAL&&freed==1);
 for(uint64_t flags:{uint64_t(0),uint64_t(0x4008801)}){
  reset();legacyError=EPERM;returnedFlags=flags;r=request();
  assert(quest_kgsl_allocate(42,&r)==-1&&errno==EINVAL&&freed==1);
 }
 for(int error:{EIO,EINVAL}){
  reset();legacyError=EPERM;r=request();assert(quest_kgsl_allocate(42,&r)==0);
  freeError=error;assert(quest_kgsl_free(42,r.id)==-1&&errno==error&&freed==0);
  freeError=0;assert(quest_kgsl_free(42,r.id)==0&&legacyFreed==1);
 }

 // Run Mesa's real capability probe, not a copy of its implementation.
 constexpr uint32_t coherent=KGSL_MEMFLAGS_IOCOHERENT |
                            (KGSL_CACHEMODE_WRITEBACK << KGSL_CACHEMODE_SHIFT);
 for(uint32_t flags:{0u,uint32_t(KGSL_MEMFLAGS_USE_CPU_MAP),coherent}){
  reset(0x1000,flags);
  assert(kgsl_is_memory_type_supported(42,flags)&&modern==0&&legacyFreed==1);
  for(int error:{EACCES,EPERM,ENOTTY}){
   reset(0x1000,flags);legacyError=freeError=error;
   assert(kgsl_is_memory_type_supported(42,flags)&&modern==1&&freed==1);
  }
 }
 for(int error:{ENOMEM,EIO,EINVAL}){
  reset(0x1000,coherent);legacyError=error;
  assert(!kgsl_is_memory_type_supported(42,coherent)&&modern==0);
 }
 // Unsupported coherent/CPU-map bits and altered cache modes are never advertised.
 for(int error:{0,EACCES}){
  for(uint32_t flags:{uint32_t(KGSL_MEMFLAGS_USE_CPU_MAP),coherent}){
   reset(0x1000,flags);legacyError=freeError=error;
   returnedFlags=flags & ~uint32_t(KGSL_MEMFLAGS_IOCOHERENT | KGSL_MEMFLAGS_USE_CPU_MAP);
   assert(!kgsl_is_memory_type_supported(42,flags));
   assert(freed+legacyFreed==1&&live.empty());
  }
  reset(0x1000,0);legacyError=freeError=error;returnedFlags=0x4000000;
  assert(!kgsl_is_memory_type_supported(42,0));
  assert(freed+legacyFreed==1&&live.empty());
 }
 reset(0x1000,coherent);legacyError=EPERM;modernError=ENOMEM;
 assert(!kgsl_is_memory_type_supported(42,coherent)&&freed==0&&live.empty());
 reset(0x1000,coherent);legacyError=EPERM;infoError=EACCES;
 assert(!kgsl_is_memory_type_supported(42,coherent)&&freed==1&&live.empty());
}
'''

with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    cpp = root / 'alloc.cpp'
    cpp.write_text(prefix + helper + probe + tests)
    subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-Wno-missing-field-initializers', '-I',
                    str(Path(sys.argv[1]) / 'src/freedreno/vulkan'),
                    str(cpp), '-o', str(root / 'alloc')], check=True)
    subprocess.run([str(root / 'alloc')], check=True)
print('KGSL allocation and memory probes: legacy/fallback, exact flags, OOM and cleanup passed')
