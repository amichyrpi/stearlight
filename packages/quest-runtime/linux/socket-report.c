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
#include <sys/stat.h>
#include <poll.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdio.h>
#include <time.h>
/* This preload also runs in musl programs. Use the public POSIX ABI names
 * rather than glibc headers' redirects to glibc-only C23 entry points. */
extern int quest_fscanf(FILE*,const char*,...) __asm__("fscanf") __attribute__((format(scanf,2,3)));
#include "network-interfaces.h"
#include "steam-capture.h"
#define QUEST_CAPTURE_MATRIX_EXPORTS
#include "capture-matrix.h"
/* Only USB subnet and scoped link-local sockets use the cable. Internet and Wi-Fi
 * sockets retain Android's normal route. Android binds the real descriptor,
 * so UDP/video traffic travels on NCM directly, without an ADB tunnel. */
static _Atomic uint64_t usb_bound[1024];
static _Atomic uint64_t usb_generation[1024];
struct usb_state {unsigned scope;uint32_t address,mask;unsigned long long generation;};
static struct usb_state usb_state(const char* registry){
 static _Thread_local struct usb_state cached;
 static _Thread_local uint64_t next;
 struct timespec now;if(clock_gettime(CLOCK_MONOTONIC,&now))return cached;
 uint64_t stamp=(uint64_t)now.tv_sec*1000000000+now.tv_nsec;
 if(stamp<next)return cached;
 next=stamp+250000000;
 cached=(struct usb_state){0};char path[256];size_t prefix=strlen(registry);
 while(prefix&&registry[prefix-1]!='/')prefix--;
 if(prefix+sizeof("usb-network-scope")>sizeof(path))return cached;
 memcpy(path,registry,prefix);memcpy(path+prefix,"usb-network-scope",sizeof("usb-network-scope"));
 FILE* file=fopen(path,"r");if(!file)return cached;
 char address[16]={0};unsigned bits=0;cached.generation=1;
 int fields=quest_fscanf(file,"%u %15s %u %llu",&cached.scope,address,&bits,&cached.generation);fclose(file);
 if(fields<1||!cached.scope){cached=(struct usb_state){0};return cached;}
 struct in_addr ipv4;
 if(fields>=3&&bits>=8&&bits<=32&&inet_pton(AF_INET,address,&ipv4)==1){cached.address=ipv4.s_addr;cached.mask=htonl(UINT32_MAX<<(32-bits));}
 return cached;
}
static int usb_socket(int fd,const struct sockaddr *address,socklen_t length) {
 if(!address||length<sizeof(sa_family_t)||(address->sa_family!=AF_INET&&address->sa_family!=AF_INET6))return 0;
 const char* registry=getenv("QUEST_SOCKET_REGISTRY");if(!registry)return 0;
 struct usb_state state=usb_state(registry);int use_usb=0;
 if(address->sa_family==AF_INET&&length>=sizeof(struct sockaddr_in)&&state.address){
  uint32_t target=((const struct sockaddr_in*)address)->sin_addr.s_addr;
  use_usb=(target&state.mask)==(state.address&state.mask);
  if(!use_usb&&(target==INADDR_BROADCAST||IN_MULTICAST(ntohl(target)))){
   struct sockaddr_in local={0};socklen_t size=sizeof(local);
   if(!syscall(SYS_getsockname,fd,&local,&size)&&local.sin_addr.s_addr==state.address)use_usb=1;
   if(!use_usb){struct in_addr chosen={0};size=sizeof(chosen);if(!getsockopt(fd,IPPROTO_IP,IP_MULTICAST_IF,&chosen,&size)&&chosen.s_addr==state.address)use_usb=1;}
  }
 }else if(address->sa_family==AF_INET6&&length>=sizeof(struct sockaddr_in6)){
  const struct sockaddr_in6* target=(const struct sockaddr_in6*)address;unsigned scope=target->sin6_scope_id;
  if(!scope&&IN6_IS_ADDR_MC_LINKLOCAL(&target->sin6_addr)){socklen_t size=sizeof(scope);getsockopt(fd,IPPROTO_IPV6,IPV6_MULTICAST_IF,&scope,&size);}
  use_usb=state.scope&&scope==state.scope&&(IN6_IS_ADDR_LINKLOCAL(&target->sin6_addr)||IN6_IS_ADDR_MC_LINKLOCAL(&target->sin6_addr));
 }
 unsigned slot=(unsigned)fd%1024;uint64_t previous=atomic_load(&usb_bound[slot]);
 if(!use_usb&&!previous)return 0;
 struct stat statbuf;if(fstat(fd,&statbuf))return -1;
 uint64_t inode=(uint64_t)statbuf.st_ino;
 uint64_t generation=use_usb?state.generation:0;
 if(previous!=inode&&!use_usb)return 0;
 if(previous==inode&&atomic_load(&usb_generation[slot])==generation)return 0;
 char path[sizeof(((struct sockaddr_un*)0)->sun_path)]={0};size_t prefix=strlen(registry);
 while(prefix&&registry[prefix-1]!='/')prefix--;
 if(prefix+sizeof("usb-network.sock")>sizeof(path))return 0;
 memcpy(path,registry,prefix);
 strcpy(path+prefix,"usb-network.sock");
 int channel=syscall(SYS_socket,AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0);if(channel<0)return -1;
 struct sockaddr_un endpoint={.sun_family=AF_UNIX};memcpy(endpoint.sun_path,path,sizeof(endpoint.sun_path));
 int error=0;
 if(syscall(SYS_connect,channel,&endpoint,sizeof(endpoint)))error=errno;
 else{
  char control[CMSG_SPACE(sizeof(int))]={0};int32_t request=use_usb;
  struct iovec io={&request,sizeof(request)};struct msghdr message={.msg_iov=&io,.msg_iovlen=1,.msg_control=control,.msg_controllen=sizeof(control)};
  struct cmsghdr* c=CMSG_FIRSTHDR(&message);c->cmsg_level=SOL_SOCKET;c->cmsg_type=SCM_RIGHTS;c->cmsg_len=CMSG_LEN(sizeof(int));memcpy(CMSG_DATA(c),&fd,sizeof(fd));
  if(syscall(SYS_sendmsg,channel,&message,MSG_NOSIGNAL)!=sizeof(request))error=errno?errno:EIO;
  else{struct pollfd ready={channel,POLLIN,0};int32_t result=EIO;
   if(poll(&ready,1,1000)<=0)error=ETIMEDOUT;
   else if(syscall(SYS_recvfrom,channel,&result,sizeof(result),0,NULL,NULL)!=sizeof(result))error=EIO;
   else error=result;
  }
 }
 syscall(SYS_close,channel);
 if(error){errno=error;return -1;}
 atomic_store(&usb_generation[slot],generation);atomic_store(&usb_bound[slot],use_usb?inode:0);return 0;
}
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
 size_t path_size=strlen(path)+1;
 if(path_size<=sizeof(address.sun_path)){
  memcpy(address.sun_path,path,path_size);
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
 if(usb_socket(fd,a,size))return -1;
 int result=real(fd,a,size);if(!result)report(fd);return result;
}
int connect(int fd,const struct sockaddr *a,socklen_t size) {
 RESOLVE("connect",(int,const struct sockaddr*,socklen_t));
 if(usb_socket(fd,a,size))return -1;
 int result=real(fd,a,size);if(!result||errno==EINPROGRESS)report(fd);return result;
}
ssize_t sendto(int fd,const void* data,size_t length,int flags,const struct sockaddr* address,socklen_t size){
 typedef ssize_t (*function)(int,const void*,size_t,int,const struct sockaddr*,socklen_t);static _Atomic(function) cached;
 function real=atomic_load(&cached);if(!real){real=(function)dlsym(RTLD_NEXT,"sendto");if(!real){errno=ENOSYS;return -1;}atomic_store(&cached,real);}
 if(usb_socket(fd,address,size))return -1;
 return real(fd,data,length,flags,address,size);
}
ssize_t sendmsg(int fd,const struct msghdr* message,int flags){
 typedef ssize_t (*function)(int,const struct msghdr*,int);static _Atomic(function) cached;
 function real=atomic_load(&cached);if(!real){real=(function)dlsym(RTLD_NEXT,"sendmsg");if(!real){errno=ENOSYS;return -1;}atomic_store(&cached,real);}
 if(message&&usb_socket(fd,(const struct sockaddr*)message->msg_name,message->msg_namelen))return -1;
 return real(fd,message,flags);
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
