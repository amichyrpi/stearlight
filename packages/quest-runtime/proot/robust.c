/* SPDX-License-Identifier: GPL-2.0-or-later
 * Android blocks Linux robust-list syscalls. Keep the actual guest list per
 * thread and process owner death while PTRACE_EVENT_EXIT still exposes its
 * memory. Only waits on words owned by a registered thread are bounded;
 * artificial timeouts become EAGAIN, a normal futex retry, so sleeping owners'
 * waiters observe FUTEX_OWNER_DIED without needing a remote FUTEX_WAKE.
 * The Quest guest ABI is exclusively 64-bit ARM Linux.
 */
#include <errno.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/utsname.h>
#include <stddef.h>
#include <linux/futex.h>
#include "robust.h"
#include "syscall/sysnum.h"
#include "tracee/reg.h"
#include "tracee/mem.h"
#include "tracee/seccomp.h"

struct head64 { uint64_t next; int64_t offset; uint64_t pending; };
struct time64 { int64_t sec, nsec; };
static int enabled(void) {
 static int value=-1;
 if(value<0){const char *v=getenv("PROOT_QUEST_ROBUST");value=v&&!strcmp(v,"1");}
 return value;
}
static int compare(struct time64 a,struct time64 b) {
 return a.sec==b.sec?(a.nsec>b.nsec)-(a.nsec<b.nsec):(a.sec>b.sec?1:-1);
}
static int adapt(Tracee *t,Sysnum n) {
 if(!enabled())return 0;
 t->quest_robust_retry=false;
 /* Xorg's compiler child drops credentials to the IDs it already has.
  * Android blocks these syscalls too. Honor only genuine no-op requests;
  * every requested identity change retains the host's refusal. */
 unsigned ids=0;bool group=false;
 switch(n){
  case PR_setgid:group=true;ids=1;break;
  case PR_setuid:ids=1;break;
  case PR_setregid:group=true;ids=2;break;
  case PR_setreuid:ids=2;break;
  case PR_setresgid:group=true;ids=3;break;
  case PR_setresuid:ids=3;break;
  default:break;
 }
 if(ids){
  uint32_t real=group?getgid():getuid(),effective=group?getegid():geteuid();
  if(real!=effective)return 0;
  for(unsigned i=0;i<ids;i++){
   uint32_t value=peek_reg(t,CURRENT,(Reg)(SYSARG_1+i));
   if(value!=real&&(ids==1||value!=UINT32_MAX))return 0;
  }
  set_sysnum(t,PR_void);poke_reg(t,SYSARG_RESULT,0);return 1;
 }
 if(n==PR_set_robust_list||n==PR_get_robust_list){
  int result=0;
  if(n==PR_set_robust_list){
   if(peek_reg(t,CURRENT,SYSARG_2)!=sizeof(struct head64))result=-EINVAL;
   else t->quest_robust_head=peek_reg(t,CURRENT,SYSARG_1);
  }else{
   pid_t pid=(pid_t)peek_reg(t,CURRENT,SYSARG_1);
   Tracee *owner=pid?get_tracee(t,pid,false):t;
   if(!owner||owner->terminated)result=-ESRCH;
   else{
    uint64_t head=owner->quest_robust_head,size=sizeof(struct head64);
    if(write_data(t,peek_reg(t,CURRENT,SYSARG_2),&head,sizeof(head))<0||
       write_data(t,peek_reg(t,CURRENT,SYSARG_3),&size,sizeof(size))<0)result=-EFAULT;
   }
  }
  set_sysnum(t,PR_void);poke_reg(t,SYSARG_RESULT,(word_t)result);
  return 1;
 }
 if(n!=PR_futex)return 0;
 unsigned op=peek_reg(t,CURRENT,SYSARG_2),cmd=op&FUTEX_CMD_MASK;
 if(cmd!=FUTEX_WAIT&&cmd!=FUTEX_WAIT_BITSET)return 0;
 /* FUTEX_WAIT can sleep only if the word equals its expected value. A
  * mismatch is EAGAIN in the kernel, so looking up the expected owner is
  * sufficient even if another thread changes the word before kernel entry.
  * Avoid a cross-process memory read for every compositor/game wait. */
 uint32_t word=(uint32_t)peek_reg(t,CURRENT,SYSARG_3);
 pid_t pid=word&FUTEX_TID_MASK;
 Tracee *owner=pid?get_tracee(t,pid,false):NULL;
 if(!owner||!owner->quest_robust_head||owner->terminated)return 0;
 struct time64 limit={0,100000000},original;
 if(cmd==FUTEX_WAIT_BITSET){
  struct timespec now;
  if(clock_gettime(op&FUTEX_CLOCK_REALTIME?CLOCK_REALTIME:CLOCK_MONOTONIC,&now))return 0;
  limit.sec=now.tv_sec;limit.nsec+=now.tv_nsec;
  if(limit.nsec>=1000000000){limit.sec++;limit.nsec-=1000000000;}
 }
 word_t timeout=peek_reg(t,CURRENT,SYSARG_4);
 if(timeout){
  if(read_data(t,&original,timeout,sizeof(original))<0||original.sec<0||
     original.nsec<0||original.nsec>=1000000000||compare(original,limit)<=0)return 0;
 }
 word_t scratch=alloc_mem(t,sizeof(limit));
 if(!scratch||write_data(t,scratch,&limit,sizeof(limit))<0)return 0;
 poke_reg(t,SYSARG_4,scratch);t->quest_robust_retry=true;
 return 0;
}
int quest_robust_enter(Tracee *t) { return adapt(t,get_sysnum(t,ORIGINAL)); }
int quest_robust_sigsys(Tracee *t) {
 Sysnum n=get_sysnum(t,CURRENT);
 /* The inherited Android TRAP filter wins over PRoot's TRACE filter.
  * These calls can therefore reach SIGSYS without an entry stop. */
 if(n==PR_futex||!adapt(t,n))return 0;
 set_result_after_seccomp(t,peek_reg(t,CURRENT,SYSARG_RESULT));
 return 1;
}
void quest_robust_exit(Tracee *t) {
 /* Android owns the host UTS namespace. Give every Quest Linux guest the
  * Frame hostname without changing Android or requiring root. Keep the
  * kernel release, architecture and all other uname fields untouched. */
 if(enabled()&&get_sysnum(t,ORIGINAL)==PR_uname&&
    (long)peek_reg(t,CURRENT,SYSARG_RESULT)==0){
  static const char hostname[sizeof(((struct utsname*)0)->nodename)]="frame";
  word_t address=peek_reg(t,ORIGINAL,SYSARG_1)+offsetof(struct utsname,nodename);
  int result=write_data(t,address,hostname,sizeof(hostname));
  if(result<0)poke_reg(t,SYSARG_RESULT,(word_t)result);
 }
 if(t->quest_robust_retry&&get_sysnum(t,ORIGINAL)==PR_futex&&
    (long)peek_reg(t,CURRENT,SYSARG_RESULT)==-ETIMEDOUT)
  poke_reg(t,SYSARG_RESULT,(word_t)-EAGAIN);
 t->quest_robust_retry=false;
}
static void mark(Tracee *t,uint64_t node,int64_t offset) {
 /* The low pointer bit denotes a PI entry; its kernel-managed state is not
  * a plain robust futex word and must not be overwritten by this adapter. */
 if(node&1)return;
 uint64_t address=node+(uint64_t)offset;uint32_t value;
 if(read_data(t,&value,address,sizeof(value))<0||(value&FUTEX_TID_MASK)!=(uint32_t)t->pid)return;
 /* No other thread can acquire this still-owned word. Preserve a waiter
  * indication even if a waiter sets it concurrently with our read. */
 value=FUTEX_OWNER_DIED|FUTEX_WAITERS;
 write_data(t,address,&value,sizeof(value));
}
void quest_robust_death(Tracee *t) {
 if(!enabled()||!t->quest_robust_head)return;
 struct head64 head;
 if(read_data(t,&head,t->quest_robust_head,sizeof(head))==0){
  uint64_t node=head.next;
  for(unsigned i=0;i<2048&&(node&~1ULL)!=t->quest_robust_head;i++){
   uint64_t next;
   if(!node||read_data(t,&next,node&~1ULL,sizeof(next))<0)break;
   mark(t,node,head.offset);node=next;
  }
  if(head.pending)mark(t,head.pending,head.offset);
 }
 t->quest_robust_head=0;
}
