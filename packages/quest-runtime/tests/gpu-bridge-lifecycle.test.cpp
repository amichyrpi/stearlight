// Android hardware test. Link gpu_bridge.cpp, EGL, GLESv3, android and log.
#include "../app/src/main/cpp/gpu_bridge.h"
#include <cassert>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
int connectRenderer(const char* directory){
 int fd=socket(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0);assert(fd>=0);
 timeval timeout{2,0};assert(!setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout)));
 sockaddr_un address{};address.sun_family=AF_UNIX;
 std::string path=std::string(directory)+"/gpu.sock";assert(path.size()<sizeof(address.sun_path));strcpy(address.sun_path,path.c_str());
 assert(!connect(fd,reinterpret_cast<sockaddr*>(&address),sizeof(address)));
 QuestGpuHello hello{};char control[CMSG_SPACE(sizeof(int)*QUEST_GPU_BUFFERS)]{};
 iovec io{&hello,sizeof(hello)};msghdr message{};message.msg_iov=&io;message.msg_iovlen=1;
 message.msg_control=control;message.msg_controllen=sizeof(control);
 assert(recvmsg(fd,&message,0)==sizeof(hello));assert(hello.magic==QUEST_GPU_MAGIC&&hello.count==QUEST_GPU_BUFFERS);
 for(cmsghdr* header=CMSG_FIRSTHDR(&message);header;header=CMSG_NXTHDR(&message,header))
  if(header->cmsg_level==SOL_SOCKET&&header->cmsg_type==SCM_RIGHTS){
   auto* fds=reinterpret_cast<int*>(CMSG_DATA(header));
   for(size_t i=0;i<(header->cmsg_len-CMSG_LEN(0))/sizeof(int);i++)close(fds[i]);
  }
 return fd;
}
int main(int argc,char** argv){
 assert(argc==2);auto d=eglGetDisplay(EGL_DEFAULT_DISPLAY);assert(eglInitialize(d,nullptr,nullptr));
 EGLint attr[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,EGL_NONE};
 EGLConfig cfg;EGLint count;assert(eglChooseConfig(d,attr,&cfg,1,&count)&&count);
 EGLint ca[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE},sa[]={EGL_WIDTH,8,EGL_HEIGHT,8,EGL_NONE};
 auto context=eglCreateContext(d,cfg,EGL_NO_CONTEXT,ca);auto surface=eglCreatePbufferSurface(d,cfg,sa);
 assert(eglMakeCurrent(d,surface,surface,context));
 for(int connected=0;connected<2;connected++){
  GpuBridge bridge;bridge.initialize(d,argv[1],16,8);
  int fd=-1;if(connected){fd=connectRenderer(argv[1]);close(fd);fd=connectRenderer(argv[1]);}
  auto before=std::chrono::steady_clock::now();bridge.shutdown();
  assert(std::chrono::steady_clock::now()-before<std::chrono::seconds(1));
  if(fd>=0)close(fd);
 }
 eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
 eglDestroySurface(d,surface);eglDestroyContext(d,context);eglTerminate(d);
 puts("Actual GPU bridge: idle shutdown, renderer handshake, reconnect and connected shutdown pass.");
}
