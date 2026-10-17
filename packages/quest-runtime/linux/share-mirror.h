// Private, bounded mirror of the virtual scene. Camera passthrough stays on Quest.
#ifndef QUEST_SHARE_MIRROR_H
#define QUEST_SHARE_MIRROR_H
#include <stdint.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#define QUEST_SHARE_WIDTH 1280
#define QUEST_SHARE_HEIGHT 720
#define QUEST_SHARE_MAGIC 0x51534831u
#define QUEST_SHARE_DASHBOARD 1u
#define QUEST_SHARE_PATH "/run/stearlight/quest/share-mirror"
struct QuestShareMirror {
 uint32_t magic,version;
 uint64_t requested_ns;
 uint32_t sequence,reserved;
 uint64_t frame;
 unsigned char pixels[QUEST_SHARE_WIDTH*QUEST_SHARE_HEIGHT*4];
};
static inline uint64_t quest_share_now(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;return (uint64_t)t.tv_sec*1000000000+t.tv_nsec;}
static inline struct QuestShareMirror* quest_share_map(int create){
 int fd=open(QUEST_SHARE_PATH,O_RDWR|O_CLOEXEC|O_NOFOLLOW|(create?O_CREAT:0),0600);if(fd<0)return NULL;
 struct stat st;int valid=!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_uid==getuid();
 if(valid&&st.st_size!=(off_t)sizeof(struct QuestShareMirror))valid=create&&!ftruncate(fd,sizeof(struct QuestShareMirror));
 void* mapped=valid?mmap(NULL,sizeof(struct QuestShareMirror),PROT_READ|PROT_WRITE,MAP_SHARED,fd,0):MAP_FAILED;close(fd);
 if(mapped==MAP_FAILED)return NULL;
 struct QuestShareMirror* mirror=(struct QuestShareMirror*)mapped;
 if(__atomic_load_n(&mirror->magic,__ATOMIC_ACQUIRE)!=QUEST_SHARE_MAGIC){
  if(!create){munmap(mirror,sizeof(*mirror));return NULL;}
  mirror->version=1;mirror->sequence=0;mirror->frame=0;mirror->requested_ns=0;mirror->reserved=0;
  __atomic_store_n(&mirror->magic,QUEST_SHARE_MAGIC,__ATOMIC_RELEASE);
 }
 if(mirror->version!=1){munmap(mirror,sizeof(*mirror));return NULL;}return mirror;
}
#endif
