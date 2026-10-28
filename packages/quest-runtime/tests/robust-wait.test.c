#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <linux/futex.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
static pthread_mutex_t mutex;
static int locked;
static void *owner(void *unused) {
 (void)unused;assert(!pthread_mutex_lock(&mutex));
 __atomic_store_n(&locked,1,__ATOMIC_RELEASE);usleep(20000);
 return NULL; /* Deliberately die while owning a robust mutex. */
}
static void recovered(pthread_mutex_t *value) {
 assert(pthread_mutex_lock(value)==EOWNERDEAD);
 assert(!pthread_mutex_consistent(value));assert(!pthread_mutex_unlock(value));
}
int main(void) {
 uint32_t tid=(uint32_t)syscall(SYS_gettid),word=tid+1;struct timespec timeout={0,5000000};
 /* A different actual owner must retain the kernel's EAGAIN. */
 errno=0;assert(syscall(SYS_futex,&word,FUTEX_WAIT,tid,&timeout,NULL,0)==-1&&errno==EAGAIN);
 word=tid;errno=0;assert(syscall(SYS_futex,&word,FUTEX_WAIT,tid,&timeout,NULL,0)==-1&&errno==ETIMEDOUT);
 struct timespec absolute;assert(!clock_gettime(CLOCK_MONOTONIC,&absolute));
 absolute.tv_nsec+=5000000;if(absolute.tv_nsec>=1000000000){absolute.tv_sec++;absolute.tv_nsec-=1000000000;}
 errno=0;assert(syscall(SYS_futex,&word,FUTEX_WAIT_BITSET,tid,&absolute,NULL,FUTEX_BITSET_MATCH_ANY)==-1&&errno==ETIMEDOUT);
 timeout.tv_nsec=-1;errno=0;assert(syscall(SYS_futex,&word,FUTEX_WAIT,tid,&timeout,NULL,0)==-1&&errno==EINVAL);
 errno=0;assert(syscall(SYS_futex,(void*)4,FUTEX_WAIT,tid,NULL,NULL,0)==-1&&errno==EFAULT);
 pthread_mutexattr_t attr;assert(!pthread_mutexattr_init(&attr));assert(!pthread_mutexattr_setrobust(&attr,PTHREAD_MUTEX_ROBUST));
 assert(!pthread_mutex_init(&mutex,&attr));pthread_t thread;assert(!pthread_create(&thread,NULL,owner,NULL));
 while(!__atomic_load_n(&locked,__ATOMIC_ACQUIRE))usleep(100);
 recovered(&mutex);assert(!pthread_join(thread,NULL));assert(!pthread_mutex_destroy(&mutex));
 /* A second process dies without pthread cleanup. */
 pthread_mutex_t *shared=mmap(NULL,sizeof(*shared),PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0);
 assert(shared!=MAP_FAILED);assert(!pthread_mutexattr_setpshared(&attr,PTHREAD_PROCESS_SHARED));assert(!pthread_mutex_init(shared,&attr));
 int ready[2];assert(!pipe(ready));pid_t child=fork();assert(child>=0);
 if(!child){close(ready[0]);assert(!pthread_mutex_lock(shared));assert(write(ready[1],"1",1)==1);usleep(20000);_exit(0);}
 close(ready[1]);char signal;assert(read(ready[0],&signal,1)==1);close(ready[0]);recovered(shared);
 int status;assert(waitpid(child,&status,0)==child&&WIFEXITED(status)&&!WEXITSTATUS(status));
 assert(!pthread_mutex_destroy(shared));munmap(shared,sizeof(*shared));pthread_mutexattr_destroy(&attr);
 puts("Futex mismatch, relative/absolute timeouts, errors and thread/process robust-owner death pass.");
}
