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
#include <signal.h>
#include <sys/shm.h>
#include <stdint.h>
static pthread_mutex_t *thread_mutex;
static void *thread_owner(void *unused){(void)unused;return (void*)(long)pthread_mutex_lock(thread_mutex);}
static int killed_owner(pthread_mutex_t *mutex){
 int ready[2],result[2];if(pipe(ready)||pipe(result))return 1;
 pid_t owner=fork();if(owner<0)return 1;
 if(!owner){close(ready[0]);int error=pthread_mutex_lock(mutex);char value=error?0:1;if(write(ready[1],&value,1)!=1)_exit(1);for(;;)pause();}
 close(ready[1]);char value=0;if(read(ready[0],&value,1)!=1||value!=1){kill(owner,SIGKILL);waitpid(owner,NULL,0);return 1;}close(ready[0]);
 pid_t waiter=fork();if(waiter<0){kill(owner,SIGKILL);waitpid(owner,NULL,0);return 1;}
 if(!waiter){close(result[0]);struct timespec deadline;clock_gettime(CLOCK_REALTIME,&deadline);deadline.tv_sec+=3;int error=pthread_mutex_timedlock(mutex,&deadline);_exit(write(result[1],&error,sizeof(error))==sizeof(error)?0:1);}
 close(result[1]);usleep(200000);kill(owner,SIGKILL);waitpid(owner,NULL,0);
 int error=0;ssize_t bytes=read(result[0],&error,sizeof(error));close(result[0]);waitpid(waiter,NULL,0);
 printf("QUEST_IPC killed owner with blocked waiter=%d (%s), expected=%d\n",error,strerror(error),EOWNERDEAD);
 return bytes!=sizeof(error)||error!=EOWNERDEAD;
}
int main(void) {
 int gid=setgid(getgid()),uid=setuid(getuid());
 printf("QUEST_IPC unchanged identity setgid=%d setuid=%d\n",gid,uid);
 if(gid||uid)return 1;
 int segment=shmget(IPC_PRIVATE,4096,IPC_CREAT|0600);
 if(segment<0){printf("QUEST_IPC SysV allocation failed errno=%d\n",errno);return 1;}
 uint32_t *region=shmat(segment,NULL,0);
 if(region==(void*)-1){shmctl(segment,IPC_RMID,NULL);return 1;}
 *region=0;pid_t writer=fork();if(writer<0)return 1;
 if(!writer){uint32_t *other=shmat(segment,NULL,0);if(other==(void*)-1)_exit(1);*other=0x53544c52;_exit(0);}
 int writer_status;waitpid(writer,&writer_status,0);int shared_ok=*region==0x53544c52&&WIFEXITED(writer_status)&&WEXITSTATUS(writer_status)==0;
 shmdt(region);shmctl(segment,IPC_RMID,NULL);
 printf("QUEST_IPC SysV cross-process sharing=%s\n",shared_ok?"verified":"failed");
 if(!shared_ok)return 1;
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
 if(error!=EOWNERDEAD)return 1;
 pthread_mutex_consistent(shared);pthread_mutex_unlock(shared);pthread_mutex_destroy(shared);
 if(pthread_mutex_init(shared,&attr)||killed_owner(shared))return 1;
 thread_mutex=&mutex;pthread_mutex_unlock(&mutex);pthread_t thread;
 if(pthread_create(&thread,NULL,thread_owner,NULL))return 1;
 void *locked=NULL;pthread_join(thread,&locked);if(locked)return 1;
 clock_gettime(CLOCK_REALTIME,&deadline);deadline.tv_sec+=2;error=pthread_mutex_timedlock(&mutex,&deadline);
 printf("QUEST_IPC thread owner death=%d (%s), expected=%d\n",error,strerror(error),EOWNERDEAD);
 return error==EOWNERDEAD?0:1;
}
