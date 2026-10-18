#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "../proot/syscall-fast-path.h"

/* Execute the actual prefix. Reaching its end means the original PRoot
 * program handles the syscall, not that this prefix permits it. */
static int permitted_value(uint32_t arch,int nr,uint64_t op,uint64_t expected) {
 struct seccomp_data data={.arch=arch,.nr=nr,.args={0,op,expected}};
 uint32_t accumulator=0;
 for(size_t pc=0;pc<sizeof(quest_syscall_fast_path)/sizeof(*quest_syscall_fast_path);pc++) {
  struct sock_filter instruction=quest_syscall_fast_path[pc];
  switch(instruction.code) {
   case BPF_LD+BPF_W+BPF_ABS:memcpy(&accumulator,(char*)&data+instruction.k,4);break;
   case BPF_JMP+BPF_JEQ+BPF_K:pc+=accumulator==instruction.k?instruction.jt:instruction.jf;break;
   case BPF_JMP+BPF_JGT+BPF_K:pc+=accumulator>instruction.k?instruction.jt:instruction.jf;break;
   case BPF_ALU+BPF_AND+BPF_K:accumulator&=instruction.k;break;
   case BPF_RET+BPF_K:return instruction.k==SECCOMP_RET_ALLOW;
   default:assert(0);
  }
 }
 return 0;
}
static int permitted(uint32_t arch,int nr,uint64_t op) {
 return permitted_value(arch,nr,op,10000);
}
int main(void) {
 assert(permitted(AUDIT_ARCH_AARCH64,98,FUTEX_WAKE));
 assert(permitted(AUDIT_ARCH_AARCH64,98,FUTEX_WAKE_PRIVATE));
 assert(!permitted(AUDIT_ARCH_AARCH64,98,FUTEX_WAIT));
 assert(!permitted(AUDIT_ARCH_AARCH64,98,FUTEX_WAIT_PRIVATE));
 assert(!permitted(AUDIT_ARCH_AARCH64,98,FUTEX_WAIT_BITSET|FUTEX_PRIVATE_FLAG|FUTEX_CLOCK_REALTIME));
 assert(!permitted(AUDIT_ARCH_AARCH64,98,FUTEX_LOCK_PI));
 assert(!permitted(AUDIT_ARCH_AARCH64,98,FUTEX_REQUEUE));
 assert(!permitted(AUDIT_ARCH_AARCH64,99,FUTEX_WAKE));
 assert(!permitted(AUDIT_ARCH_X86_64,98,FUTEX_WAKE));
 assert(!permitted(AUDIT_ARCH_ARM,98,FUTEX_WAKE));
 assert(permitted(AUDIT_ARCH_AARCH64,29,0xc040094a));
 assert(permitted(AUDIT_ARCH_AARCH64,29,0x40080907));
 assert(!permitted(AUDIT_ARCH_AARCH64,29,0x40087703)); /* ashmem */
 assert(permitted(AUDIT_ARCH_AARCH64,29,0x5401)); /* TCGETS: read-only */
 assert(!permitted(AUDIT_ARCH_AARCH64,29,0x5402)); /* TCSETS */
 assert(!permitted(AUDIT_ARCH_AARCH64,29,0x802c542a)); /* TCGETS2 needs translation */
 assert(!permitted(AUDIT_ARCH_ARM,29,0x5401));
 assert(!permitted(AUDIT_ARCH_X86_64,29,0x5401));
 assert(!permitted(AUDIT_ARCH_AARCH64,30,0xc040094a));
 assert(!permitted(AUDIT_ARCH_ARM,29,0xc040094a));
 for(unsigned owner=0;owner<3;owner++) {
  assert(permitted_value(AUDIT_ARCH_AARCH64,98,FUTEX_WAIT,owner));
  assert(permitted_value(AUDIT_ARCH_AARCH64,98,FUTEX_WAIT_PRIVATE,owner));
  assert(permitted_value(AUDIT_ARCH_AARCH64,98,FUTEX_WAIT_BITSET|FUTEX_CLOCK_REALTIME,owner));
  assert(permitted_value(AUDIT_ARCH_AARCH64,98,FUTEX_WAIT_BITSET_PRIVATE,owner|FUTEX_WAITERS));
  assert(!permitted_value(AUDIT_ARCH_ARM,98,FUTEX_WAIT,owner));
  assert(!permitted_value(AUDIT_ARCH_AARCH64,98,FUTEX_LOCK_PI,owner));
 }
 assert(!permitted_value(AUDIT_ARCH_AARCH64,98,FUTEX_WAIT,3));
 assert(!permitted_value(AUDIT_ARCH_AARCH64,98,FUTEX_WAIT_PRIVATE,10000|FUTEX_WAITERS));
 assert(!permitted_value(AUDIT_ARCH_AARCH64,98,FUTEX_WAIT_BITSET,10000|FUTEX_OWNER_DIED));
 return 0;
}
