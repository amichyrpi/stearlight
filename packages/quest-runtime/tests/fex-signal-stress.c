/* Manual Quest regression: compile with x86_64-linux-gnu-gcc -O1 -static -pthread.
 * Run through Steam FEX in the Quest guest. Optional argument 0 disables async
 * signals for the control run. Checks normal and modified-IP signal returns. */
#define _GNU_SOURCE
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <ucontext.h>
static volatile sig_atomic_t done, seen, traps;
static void change_ip(int sig,siginfo_t* info,void* context){
 (void)sig;(void)info;ucontext_t* uc=context;uc->uc_mcontext.gregs[REG_RIP]+=2;++traps;
}
static void handler(int sig){(void)sig;++seen;if(seen<3){ssize_t n=write(2,"SIGNAL\n",7);(void)n;}}
__attribute__((noinline)) static void work(unsigned depth){
 volatile uint64_t guard[64];
 for(unsigned i=0;i<64;++i)guard[i]=0x51b0000000000000ULL+depth*1024+i;
 if(depth)work(depth-1);
 else {for(volatile unsigned i=0;i<15000;++i){}}
 for(unsigned i=0;i<64;++i)if(guard[i]!=0x51b0000000000000ULL+depth*1024+i){
  fprintf(stderr,"CORRUPT depth=%u slot=%u signals=%d\n",depth,i,seen);_exit(3);
 }
}
static void* worker(void* unused){(void)unused;while(!done){for(volatile unsigned i=0;i<15000;++i){}work(10);}return NULL;}
int main(int argc,char** argv){
 int deliver=argc<2 || atoi(argv[1]);
 struct sigaction trap={0};trap.sa_sigaction=change_ip;trap.sa_flags=SA_SIGINFO;sigemptyset(&trap.sa_mask);
 if(sigaction(SIGILL,&trap,0))return 2;
 for(unsigned i=0;i<100;++i)__asm__ volatile("ud2");
 if(traps!=100)return 4;
 struct sigaction action={0};action.sa_handler=handler;action.sa_flags=SA_RESTART;sigemptyset(&action.sa_mask);
 if(sigaction(SIGUSR1,&action,0))return 2;
 pthread_t thread;if(pthread_create(&thread,0,worker,0))return 2;
 struct timespec pause={0,100000};
 for(unsigned i=0;i<10000;++i){if(deliver)pthread_kill(thread,SIGUSR1);nanosleep(&pause,0);}
 done=1;pthread_join(thread,0);printf("OK signals=%d\n",seen);return 0;
}
