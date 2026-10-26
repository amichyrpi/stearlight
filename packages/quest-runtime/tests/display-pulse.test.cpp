#include "../linux/display-pulse.h"
#include <sys/mman.h>
#include <sys/wait.h>
#include <cassert>
#include <cerrno>
int main(){
 int fd=memfd_create("quest-display-pulse",MFD_CLOEXEC);assert(fd>=0&&!ftruncate(fd,sizeof(QuestTracking)));
 auto* shared=static_cast<QuestTracking*>(mmap(nullptr,sizeof(QuestTracking),PROT_READ|PROT_WRITE,MAP_SHARED,fd,0));
 auto* reader=static_cast<const QuestTracking*>(mmap(nullptr,sizeof(QuestTracking),PROT_READ,MAP_SHARED,fd,0));
 assert(reader!=MAP_FAILED);close(fd);
 timespec tiny{0,1};errno=0;
 assert(syscall(SYS_futex,&reader->sequence,FUTEX_WAIT,0,&tiny,nullptr,0)==-1&&errno==ETIMEDOUT);
 assert(shared!=MAP_FAILED);shared->magic=QUEST_BRIDGE_MAGIC;shared->version=QUEST_BRIDGE_VERSION;
 QuestTracking sample{};sample.session_visible=1;sample.refresh_hz=72;
 questPublishTracking(shared,sample);QuestDisplayPulse pulse;
 assert(pulse.wait(reader,2000000));
 assert(!pulse.wait(reader,2000000)); // Never signal twice for one frame.
 pid_t child=fork();assert(child>=0);
 if(!child){usleep(5000);questPublishTracking(shared,sample);_exit(0);}
 assert(pulse.wait(reader,100000000)); // Cross-process wake, coherent sample.
 int status=0;assert(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0);
 assert(!pulse.wait(reader,2000000));
 // An older publisher has no futex wake; it must still work through timeout.
 child=fork();assert(child>=0);
 if(!child){usleep(5000);__atomic_add_fetch(&shared->sequence,2,__ATOMIC_RELEASE);_exit(0);}
 assert(pulse.wait(reader,100000000));assert(waitpid(child,&status,0)==child);
 sample.session_visible=0;questPublishTracking(shared,sample);
 assert(!pulse.wait(reader,2000000));assert(!pulse.wait(nullptr,2000000));
 munmap(const_cast<QuestTracking*>(reader),sizeof(*reader));munmap(shared,sizeof(*shared));
}
