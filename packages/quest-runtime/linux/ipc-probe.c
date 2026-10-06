#define _GNU_SOURCE
#include <pthread.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <linux/futex.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
int main(void) {
 struct robust_list_head *head=NULL;size_t size=0;
 long result=syscall(SYS_get_robust_list,0,&head,&size);
 printf("QUEST_IPC get_robust_list(self)=%ld errno=%d head=%p size=%zu\n",result,errno,(void*)head,size);
 head=NULL;size=0;errno=0;
 result=syscall(SYS_get_robust_list,syscall(SYS_gettid),&head,&size);
 printf("QUEST_IPC get_robust_list(tid)=%ld errno=%d head=%p size=%zu\n",result,errno,(void*)head,size);
 pthread_mutexattr_t attr;pthread_mutex_t mutex;pthread_mutexattr_init(&attr);
 int error=pthread_mutexattr_setrobust(&attr,PTHREAD_MUTEX_ROBUST);
 if(!error)error=pthread_mutex_init(&mutex,&attr);
 if(!error)error=pthread_mutex_lock(&mutex);
 printf("QUEST_IPC robust mutex lock=%d (%s)\n",error,strerror(error));
 pthread_mutex_t *shared=mmap(NULL,sizeof(*shared),PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0);
 pthread_mutexattr_setpshared(&attr,PTHREAD_PROCESS_SHARED);
 error=pthread_mutex_init(shared,&attr);
 if(error)return 1;
 fflush(stdout);pid_t child=fork();if(child<0)return 1;
 if(!child){int locked=pthread_mutex_lock(shared);_exit(locked?1:0);}
 int status;waitpid(child,&status,0);
 struct timespec deadline;clock_gettime(CLOCK_REALTIME,&deadline);deadline.tv_sec+=2;
 error=pthread_mutex_timedlock(shared,&deadline);
 printf("QUEST_IPC process owner death=%d (%s), expected=%d\n",error,strerror(error),EOWNERDEAD);
 return error==EOWNERDEAD?0:1;
}
