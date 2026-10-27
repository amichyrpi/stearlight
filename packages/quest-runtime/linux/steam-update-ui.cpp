// Route only Valve's updater child through Gamescope. Steam and its CEF
// renderer retain their existing offscreen display and texture-sharing path.
#include <dlfcn.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <signal.h>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifndef QUEST_UPDATE_LOADER
#define QUEST_UPDATE_LOADER "/opt/frame-gamescope/lib/ld-linux-aarch64.so.1"
#endif
#ifndef QUEST_UPDATE_TIMEOUT
#define QUEST_UPDATE_TIMEOUT 10000
#endif
#ifndef QUEST_UPDATE_LOG
#define QUEST_UPDATE_LOG "/tmp/quest-native-updater.log"
#endif
extern char** environ;
namespace {
using Exec = int(*)(const char*, char*const[], char*const[]);
Exec realExec(bool search) {
#if defined(__aarch64__)
 constexpr auto version="GLIBC_2.17";
#else
 const char* version=search?"GLIBC_2.11":"GLIBC_2.2.5";
#endif
 // The SDL adapter also interposes dlsym; dlvsym preserves RTLD_NEXT's owner.
 return reinterpret_cast<Exec>(dlvsym(RTLD_NEXT,search?"execvpe":"execve",version));
}
__attribute__((constructor)) void scopePreload() {
 const char* inherited=getenv("LD_PRELOAD");if(!inherited)return;
 std::string list=inherited,kept;
 for(size_t at=0;at<list.size();) {
  size_t end=list.find_first_of(": ",at);if(end==std::string::npos)end=list.size();
  auto item=list.substr(at,end-at);auto slash=item.find_last_of('/');
  if(!item.empty()&&item.substr(slash==std::string::npos?0:slash+1)!="libquest_steam_update_ui.so") {
   if(!kept.empty())kept+=':';
   kept+=item;
  }
  at=end+1;
 }
 if(kept.empty())unsetenv("LD_PRELOAD");else setenv("LD_PRELOAD",kept.c_str(),1);
}
void discard(pid_t compositor) {
 if(compositor<=0)return;
 kill(compositor,SIGKILL);
 while(waitpid(compositor,nullptr,0)<0&&errno==EINTR){}
}
int execute(const char* path,char*const argv[],char*const environment[],bool search) {
 auto real=realExec(search);if(!real){errno=ENOSYS;return -1;}
 bool updater=false;int socket=-1;
 for(size_t i=0;argv&&argv[i];i++) {
  if(!strcmp(argv[i],"-child-update-ui"))updater=true;
  if(!strcmp(argv[i],"-child-update-ui-socket")&&argv[i+1])socket=atoi(argv[i+1]);
 }
 if(!updater||socket<3||fcntl(socket,F_GETFD)<0)return real(path,argv,environment);
 int log=open(QUEST_UPDATE_LOG,O_RDWR|O_CREAT|O_TRUNC|O_CLOEXEC,0600);
 if(log<0)return real(path,argv,environment);
 pid_t owner=getpid(),compositor=fork();
 if(compositor==0) {
  close(socket);
  prctl(PR_SET_PDEATHSIG,SIGTERM);if(getppid()!=owner)_exit(125);
  dup2(log,STDOUT_FILENO);dup2(log,STDERR_FILENO);if(log>2)close(log);
  setenv("VK_ICD_FILENAMES","/opt/quest/turnip_icd.json",1);
  setenv("VK_DRIVER_FILES","/opt/quest/turnip_icd.json",1);
  unsetenv("EGL_PLATFORM");
  setenv("XKB_CONFIG_ROOT","/opt/frame-gamescope/share/X11/xkb",1);
  setenv("PATH","/opt/quest:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin",1);
  const char* command[]={"ld-linux-aarch64.so.1","--preload","/opt/quest/libquest_socket.so:/usr/lib/aarch64-linux-gnu/libpipewire-0.3.so.0",
   "--library-path","/opt/frame-gamescope/lib:/usr/lib/aarch64-linux-gnu","/opt/frame-gamescope/bin/gamescope",
   "--backend","openvr","--expose-wayland","--xwayland-count","1",
   "-w","1280","-h","720","-r","30","--steam","--virtual-connector-strategy","SingleApplication",
   "--vr-overlay-key","valve.steam.gamepadui.bootstrapper","--vr-overlay-explicit-name","Steam",
   "--vr-overlay-show-immediately",nullptr};
  realExec(false)(QUEST_UPDATE_LOADER,const_cast<char*const*>(command),environ);_exit(127);
 }
 char display[64]={},wayland[64]={};bool connected=false;
 for(int elapsed=0;compositor>0&&elapsed<QUEST_UPDATE_TIMEOUT;elapsed+=20){
  char text[65537]={};ssize_t length=pread(log,text,sizeof(text)-1,0);
  if(length>0){
   const char* w=strstr(text,"Running compositor on wayland display '");
   const char* x=strstr(text,"Starting Xwayland on ");
   if(w&&x&&sscanf(w,"Running compositor on wayland display '%63[^']",wayland)==1&&
      sscanf(x,"Starting Xwayland on %63s",display)==1){
     void* library=dlopen("libX11.so.6",RTLD_LAZY|RTLD_LOCAL);
     if(library){
      auto open=reinterpret_cast<void*(*)(const char*)>(dlsym(library,"XOpenDisplay"));
      auto close=reinterpret_cast<int(*)(void*)>(dlsym(library,"XCloseDisplay"));
      void* connection=open&&close?open(display):nullptr;
      if(connection){close(connection);connected=true;}
      dlclose(library);
      if(connected)break;
     }
   }
  }
  int status=0;if(waitpid(compositor,&status,WNOHANG)==compositor){compositor=-1;break;}
  usleep(20000);
 }
 close(log);
 if(connected&&display[0]==':') {
  std::vector<std::string> values;
  for(size_t i=0;environment&&environment[i];i++) {
   bool replaced=false;
   for(const char* prefix:{"DISPLAY=","WAYLAND_DISPLAY=","GAMESCOPE_WAYLAND_DISPLAY=",
                          "VK_ICD_FILENAMES=","VK_DRIVER_FILES=","EGL_PLATFORM=","MESA_VK_WSI_DEBUG="})
    if(!strncmp(environment[i],prefix,strlen(prefix))){replaced=true;break;}
   if(!replaced)values.emplace_back(environment[i]);
  }
  values.push_back(std::string("DISPLAY=")+display);values.push_back(std::string("WAYLAND_DISPLAY=")+wayland);
  values.push_back(std::string("GAMESCOPE_WAYLAND_DISPLAY=")+wayland);
  // The normal Steam adapter captures offscreen swapchains. The real loader
  // must present to Xwayland instead; only this transient child uses raw Mesa.
  values.emplace_back("VK_ICD_FILENAMES=/opt/quest/turnip_icd.json");
  values.emplace_back("VK_DRIVER_FILES=/opt/quest/turnip_icd.json");
  values.emplace_back("MESA_VK_WSI_DEBUG=sw");
  std::vector<char*> child;for(auto& value:values)child.push_back(value.data());child.push_back(nullptr);
  std::fputs("QUEST_NATIVE_UPDATE_DISPLAY_READY\n",stderr);
  int result=real(path,argv,child.data()),error=errno;discard(compositor);errno=error;return result;
 }
 discard(compositor);
 // A failed VR compositor must not prevent Steam's original updater running.
 return real(path,argv,environment);
}
int executeList(const char* path,const char* first,va_list arguments,bool search) {
 std::vector<char*> argv{const_cast<char*>(first)};
 if(first)while(auto value=va_arg(arguments,char*))argv.push_back(value);
 argv.push_back(nullptr);return execute(path,argv.data(),environ,search);
}
}
extern "C" int execve(const char* p,char*const a[],char*const e[]){return execute(p,a,e,false);}
extern "C" int execvpe(const char* p,char*const a[],char*const e[]){return execute(p,a,e,true);}
extern "C" int execv(const char* p,char*const a[]){return execute(p,a,environ,false);}
extern "C" int execvp(const char* p,char*const a[]){return execute(p,a,environ,true);}
extern "C" int execl(const char* p,const char* first,...){va_list a;va_start(a,first);int result=executeList(p,first,a,false);va_end(a);return result;}

// The bootstrapper uses this marker to select its native graphical loader.
// Keep the compatibility answer inside that executable: Steam's client,
// browser and games retain their actual display environment.
extern "C" char* getenv(const char* name) noexcept {
 using Get=char*(*)(const char*);
#if defined(__aarch64__)
 constexpr auto version="GLIBC_2.17";
#else
 constexpr auto version="GLIBC_2.2.5";
#endif
 static auto original=reinterpret_cast<Get>(dlvsym(RTLD_NEXT,"getenv",version));
 char* value=original?original(name):nullptr;
 if(!value&&!strcmp(name,"GAMESCOPE_WAYLAND_DISPLAY")) {
  Dl_info caller{};
  if(dladdr(__builtin_return_address(0),&caller)&&caller.dli_fname) {
   const char* base=strrchr(caller.dli_fname,'/');base=base?base+1:caller.dli_fname;
   if(!strcmp(base,"steam")){static char display[]="gamescope-0";return display;}
  }
 }
 return value;
}
