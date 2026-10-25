"""Exercise the real preload's UDP fast path and its libc fallbacks."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[3]
with tempfile.TemporaryDirectory() as directory:
    folder = Path(directory)
    (folder / 'observer.c').write_text(r'''
#define _GNU_SOURCE
#include <sys/socket.h>
#include <dlfcn.h>
#include <stdatomic.h>
static _Atomic int count;
int quest_test_batch_calls(void){return atomic_load(&count);}
int recvmmsg(int fd,struct mmsghdr* messages,unsigned n,int flags,struct timespec* timeout){
 typedef int (*function)(int,struct mmsghdr*,unsigned,int,struct timespec*);
 function real=(function)dlsym(RTLD_NEXT,"recvmmsg");atomic_fetch_add(&count,1);
 return real(fd,messages,n,flags,timeout);
}
''')
    (folder / 'test.c').write_text(r'''
#define _GNU_SOURCE
#include <assert.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>
static void* blocked(void* value){char c;recv(*(int*)value,&c,1,0);return NULL;}
int main(void){
 int (*calls)(void)=dlsym(RTLD_DEFAULT,"quest_test_batch_calls");assert(calls);
 int enabled=!strcmp(getenv("QUEST_STREAM_SOCKET_BATCH"),"1");
 for(int family=AF_INET;family<=AF_INET6;family+=AF_INET6-AF_INET){
  int rx=socket(family,SOCK_DGRAM,0),tx=socket(family,SOCK_DGRAM,0);assert(rx>=0&&tx>=0);
  struct sockaddr_storage address={0};socklen_t size;
  if(family==AF_INET){struct sockaddr_in* a=(void*)&address;a->sin_family=family;a->sin_addr.s_addr=htonl(INADDR_LOOPBACK);size=sizeof(*a);}
  else{struct sockaddr_in6* a=(void*)&address;a->sin6_family=family;a->sin6_addr=in6addr_loopback;size=sizeof(*a);}
  assert(!bind(rx,(void*)&address,size)&&!getsockname(rx,(void*)&address,&size));
  assert(!connect(tx,(void*)&address,size));char data[16]={0};int before=calls();
  assert(send(tx,"payload",7,0)==7);
  assert(recv(rx,data,2,MSG_PEEK|MSG_TRUNC)==7&&!memcmp(data,"pa",2));
  assert(recvfrom(rx,data,sizeof(data),0,NULL,NULL)==7&&!memcmp(data,"payload",7));
  assert(send(tx,"",0,0)==0&&recv(rx,data,sizeof(data),0)==0);
  assert(recv(rx,data,sizeof(data),MSG_DONTWAIT)==-1&&(errno==EAGAIN||errno==EWOULDBLOCK));
  assert(enabled?calls()>before:calls()==before);
  // Internet peer addresses require no guest filesystem translation.
  before=calls();assert(send(tx,"peer",4,0)==4);struct sockaddr_storage peer;socklen_t length=sizeof(peer);
  assert(recvfrom(rx,data,sizeof(data),0,(void*)&peer,&length)==4&&peer.ss_family==family);
  assert(enabled?calls()>before:calls()==before);
  before=calls();assert(send(tx,"bad",3,0)==3);
  assert(recvfrom(rx,data,sizeof(data),0,(void*)&peer,NULL)==-1&&errno==EFAULT&&calls()==before);
  // The fast path must remain a pthread cancellation point while blocked.
  pthread_t thread;assert(!pthread_create(&thread,NULL,blocked,&rx));usleep(20000);
  assert(!pthread_cancel(thread));void* result=NULL;assert(!pthread_join(thread,&result)&&result==PTHREAD_CANCELED);
  close(tx);close(rx);
 }
 for(int type=SOCK_STREAM;type<=SOCK_DGRAM;type++){
  int pair[2];assert(!socketpair(AF_UNIX,type,0,pair));int before=calls();char value=0;
  assert(send(pair[0],"x",1,0)==1&&recv(pair[1],&value,1,0)==1&&value=='x'&&calls()==before);
  assert(send(pair[0],"y",1,0)==1);struct sockaddr_storage peer;socklen_t size=sizeof(peer);
  assert(recvfrom(pair[1],&value,1,0,(void*)&peer,&size)==1&&value=='y'&&calls()==before);
  close(pair[0]);close(pair[1]);
 }
 // Internet stream sockets retain their original framing and WAITALL behavior.
 int listener=socket(AF_INET,SOCK_STREAM,0),tx=socket(AF_INET,SOCK_STREAM,0);assert(listener>=0&&tx>=0);
 struct sockaddr_in address={.sin_family=AF_INET,.sin_addr.s_addr=htonl(INADDR_LOOPBACK)};socklen_t size=sizeof(address);
 assert(!bind(listener,(void*)&address,size)&&!getsockname(listener,(void*)&address,&size)&&!listen(listener,1));
 assert(!connect(tx,(void*)&address,size));int rx=accept(listener,NULL,NULL);assert(rx>=0);int before=calls();char data[4];
 assert(send(tx,"abc",3,0)==3&&recv(rx,data,3,MSG_WAITALL)==3&&!memcmp(data,"abc",3)&&calls()==before);
 close(tx);close(rx);close(listener);
 return 0;
}
''')
    library, observer, executable = folder / 'socket.so', folder / 'observer.so', folder / 'test'
    subprocess.run(['gcc', '-shared', '-fPIC', '-O2', '-Wall', '-Wextra', '-Werror',
                    str(root / 'packages/quest-runtime/linux/socket-report.c'), '-ldl', '-o', str(library)], check=True)
    subprocess.run(['gcc', '-shared', '-fPIC', str(folder / 'observer.c'), '-ldl', '-o', str(observer)], check=True)
    subprocess.run(['gcc', str(folder / 'test.c'), '-ldl', '-pthread', '-o', str(executable)], check=True)
    for enabled in ('0', '1'):
        subprocess.run([str(executable)], env={**os.environ, 'LD_PRELOAD': f'{library}:{observer}',
                                              'QUEST_STREAM_SOCKET_BATCH': enabled}, timeout=5, check=True)
print('Streaming UDP: IPv4/IPv6, payloads, peek/truncation, empty packets, EAGAIN, cancellation and Unix fallbacks passed')
