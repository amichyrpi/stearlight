#include <jni.h>
#include <android/log.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <unistd.h>
#include <poll.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <thread>
#include <vector>
namespace {
struct Entry { int fd=-1,original=-1;pid_t pid=0;uid_t uid=0;uint64_t inode=0;std::chrono::steady_clock::time_point expires; sockaddr_in local{},peer{};bool listening=false; };
std::atomic<bool> running{false};std::thread worker;
bool stillOwned(const Entry &e){
 char path[128],value[128];snprintf(path,sizeof(path),"/proc/%d/fd/%d",e.pid,e.original);
 ssize_t size=readlink(path,value,sizeof(value)-1);if(size<0)return false;value[size]=0;
 unsigned long long inode=0;return sscanf(value,"socket:[%llu]",&inode)==1&&inode==e.inode;
}
void serve(std::string directory,int socket){
 std::map<uint64_t,Entry> entries;
 auto publish=[&](){
  std::string path=directory+"/proc-net/tcp",temporary=path+".tmp";
  FILE *out=fopen(temporary.c_str(),"w");if(!out)return;
  fputs("  sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode\n",out);
  unsigned row=0;
  for(auto &[inode,e]:entries)if(e.listening||e.peer.sin_port)
   fprintf(out,"%4u: %08X:%04X %08X:%04X %02X 00000000:00000000 00:00000000 00000000 %u 0 %llu\n",row++,e.local.sin_addr.s_addr,ntohs(e.local.sin_port),e.peer.sin_addr.s_addr,ntohs(e.peer.sin_port),e.listening?10:1,e.uid,(unsigned long long)inode);
  if(fclose(out)==0)rename(temporary.c_str(),path.c_str());
 };
 while(running){
  pollfd p{socket,POLLIN,0};poll(&p,1,50);
  if(p.revents&POLLIN){
   int32_t original=-1;char control[CMSG_SPACE(sizeof(int)*8)+CMSG_SPACE(sizeof(ucred))]{};
   iovec io{&original,sizeof(original)};msghdr message{};message.msg_iov=&io;message.msg_iovlen=1;message.msg_control=control;message.msg_controllen=sizeof(control);
   ssize_t size=recvmsg(socket,&message,MSG_DONTWAIT|MSG_CMSG_CLOEXEC);ucred sender{};std::vector<int> fds;
   for(cmsghdr *c=CMSG_FIRSTHDR(&message);c;c=CMSG_NXTHDR(&message,c)){
    if(c->cmsg_level!=SOL_SOCKET)continue;
    if(c->cmsg_type==SCM_CREDENTIALS&&c->cmsg_len==CMSG_LEN(sizeof(sender)))memcpy(&sender,CMSG_DATA(c),sizeof(sender));
    if(c->cmsg_type==SCM_RIGHTS){size_t count=(c->cmsg_len-CMSG_LEN(0))/sizeof(int);int *data=(int*)CMSG_DATA(c);fds.insert(fds.end(),data,data+count);}
   }
   for(int fd:fds){
    struct stat st{};int type=0;socklen_t length=sizeof(type);
    if(size!=sizeof(original)||(message.msg_flags&(MSG_TRUNC|MSG_CTRUNC))||fds.size()!=1||sender.uid!=getuid()||sender.pid<=0||fstat(fd,&st)||!S_ISSOCK(st.st_mode)||getsockopt(fd,SOL_SOCKET,SO_TYPE,&type,&length)||type!=SOCK_STREAM){close(fd);continue;}
    Entry e;e.fd=fd;e.original=original;e.pid=sender.pid;e.uid=sender.uid;e.inode=st.st_ino;e.expires=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    if(!stillOwned(e)){close(fd);continue;}
    auto old=entries.find(e.inode);if(old!=entries.end()&&old->second.fd>=0)close(old->second.fd);
    entries[e.inode]=e;
   }
  }
  for(auto i=entries.begin();i!=entries.end();){
   Entry &e=i->second;bool remove=!stillOwned(e);
   if(!remove&&e.fd>=0){
    socklen_t size=sizeof(e.local);int listening=0;socklen_t intSize=sizeof(listening);
    if(getsockname(e.fd,(sockaddr*)&e.local,&size)||e.local.sin_family!=AF_INET||e.local.sin_addr.s_addr!=htonl(INADDR_LOOPBACK))remove=true;
    else{
     getsockopt(e.fd,SOL_SOCKET,SO_ACCEPTCONN,&listening,&intSize);e.listening=listening;
     size=sizeof(e.peer);if(getpeername(e.fd,(sockaddr*)&e.peer,&size))e.peer={};
     if(e.listening||e.peer.sin_port){close(e.fd);e.fd=-1;}
     else if(std::chrono::steady_clock::now()>=e.expires)remove=true;
    }
   }
   if(remove){if(e.fd>=0)close(e.fd);i=entries.erase(i);}else ++i;
  }
  publish();
 }
 for(auto &[inode,e]:entries)if(e.fd>=0)close(e.fd);
 close(socket);unlink((directory+"/socket-registry.sock").c_str());
}
void stop(){running=false;if(worker.joinable())worker.join();}
}
extern "C" JNIEXPORT void JNICALL Java_net_stearlight_quest_QuestActivity_startSocketRegistry(JNIEnv *env,jobject,jstring directory){
 stop();const char *chars=env->GetStringUTFChars(directory,nullptr);std::string path(chars);env->ReleaseStringUTFChars(directory,chars);
 mkdir((path+"/proc-net").c_str(),0700);
 int fd=socket(AF_UNIX,SOCK_DGRAM|SOCK_CLOEXEC,0),pass=1;sockaddr_un address{};address.sun_family=AF_UNIX;std::string name=path+"/socket-registry.sock";
 if(fd<0||name.size()>=sizeof(address.sun_path)){if(fd>=0)close(fd);return;}
 strcpy(address.sun_path,name.c_str());unlink(name.c_str());
 if(setsockopt(fd,SOL_SOCKET,SO_PASSCRED,&pass,sizeof(pass))||bind(fd,(sockaddr*)&address,sizeof(address))){__android_log_print(ANDROID_LOG_ERROR,"StearlightQuest","Socket registry could not start");close(fd);return;}
 chmod(name.c_str(),0600);running=true;worker=std::thread(serve,path,fd);
}
extern "C" JNIEXPORT void JNICALL Java_net_stearlight_quest_QuestActivity_stopSocketRegistry(JNIEnv*,jobject){stop();}
