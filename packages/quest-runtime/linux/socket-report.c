#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <unistd.h>
/* Transfer the real loopback socket. Android reads its kernel endpoints and
 * inode; this adapter does not invent network state or process identities. */
static void report(int fd) {
 int saved=errno;const char *path=getenv("QUEST_SOCKET_REGISTRY");
 if(!path||fd<0){errno=saved;return;}
 struct sockaddr_in local;socklen_t size=sizeof(local);
 if(syscall(SYS_getsockname,fd,&local,&size)||local.sin_family!=AF_INET||local.sin_addr.s_addr!=htonl(INADDR_LOOPBACK)){errno=saved;return;}
 int channel=syscall(SYS_socket,AF_UNIX,SOCK_DGRAM|SOCK_CLOEXEC,0);
 if(channel<0){errno=saved;return;}
 struct sockaddr_un address={.sun_family=AF_UNIX};
 if(strlen(path)<sizeof(address.sun_path)){
  strcpy(address.sun_path,path);
  char control[CMSG_SPACE(sizeof(int))]={0};int32_t original=fd;
  struct iovec io={&original,sizeof(original)};
  /* PRoot translates AF_UNIX connect paths; sendmsg destinations are not
   * translated. Connect this private datagram before passing the descriptor. */
  struct msghdr message={.msg_iov=&io,.msg_iovlen=1,.msg_control=control,.msg_controllen=sizeof(control)};
  struct cmsghdr *c=CMSG_FIRSTHDR(&message);c->cmsg_level=SOL_SOCKET;c->cmsg_type=SCM_RIGHTS;c->cmsg_len=CMSG_LEN(sizeof(fd));memcpy(CMSG_DATA(c),&fd,sizeof(fd));
  if(!syscall(SYS_connect,channel,&address,sizeof(address)))
   syscall(SYS_sendmsg,channel,&message,MSG_DONTWAIT|MSG_NOSIGNAL);
 }
 syscall(SYS_close,channel);errno=saved;
}
#define RESOLVE(name,arguments) \
 typedef int (*function) arguments; static _Atomic(function) cached; \
 function real=atomic_load(&cached); \
 if(!real){real=(function)dlsym(RTLD_NEXT,name);if(!real){errno=ENOSYS;return -1;}atomic_store(&cached,real);}
int bind(int fd,const struct sockaddr *a,socklen_t size) {
 RESOLVE("bind",(int,const struct sockaddr*,socklen_t));
 int result=real(fd,a,size);if(!result)report(fd);return result;
}
int connect(int fd,const struct sockaddr *a,socklen_t size) {
 RESOLVE("connect",(int,const struct sockaddr*,socklen_t));
 int result=real(fd,a,size);if(!result||errno==EINPROGRESS)report(fd);return result;
}
int listen(int fd,int backlog) {
 RESOLVE("listen",(int,int));
 int result=real(fd,backlog);if(!result)report(fd);return result;
}
int accept(int fd,struct sockaddr *a,socklen_t *size) {
 RESOLVE("accept",(int,struct sockaddr*,socklen_t*));
 int result=real(fd,a,size);if(result>=0)report(result);return result;
}
int accept4(int fd,struct sockaddr *a,socklen_t *size,int flags) {
 RESOLVE("accept4",(int,struct sockaddr*,socklen_t*,int));
 int result=real(fd,a,size,flags);if(result>=0)report(result);return result;
}
