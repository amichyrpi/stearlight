#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <arpa/inet.h>
#include <stdint.h>
// Steam supplies every address of this PC through -c, but -e can still pick
// Wi-Fi while the USB adapter is connected. Prefer a unique advertised PC
// address on Android's live USB subnet. Keep authentication/ports untouched;
// do not redirect internet traffic or invent a host address.
static void prefer_usb(int argc,char** argv){
 const char* preference=getenv("STEARLIGHT_VRLINK_PREFER_USB");
 const char* registry=getenv("QUEST_SOCKET_REGISTRY");
 if((preference&&!strcmp(preference,"0"))||!registry)return;
 char path[PATH_MAX];const char* slash=strrchr(registry,'/');if(!slash)return;
 size_t prefix=(size_t)(slash-registry)+1;
 if(prefix+sizeof("usb-network-scope")>sizeof(path))return;
 memcpy(path,registry,prefix);strcpy(path+prefix,"usb-network-scope");
 FILE* file=fopen(path,"r");if(!file)return;
 unsigned scope=0,bits=0;char local[16];
 int fields=fscanf(file,"%u %15s %u",&scope,local,&bits);fclose(file);
 struct in_addr address;
 if(fields!=3||!scope||bits<8||bits>30||inet_pton(AF_INET,local,&address)!=1)return;
 uint32_t mask=htonl(UINT32_MAX<<(32-bits));const char* candidate=NULL;
 int endpoint=-1;
 for(int i=1;i+1<argc;i++){
  if(!strcmp(argv[i],"-e"))endpoint=i+1;
  if(strcmp(argv[i],"-c"))continue;
  struct in_addr peer;if(inet_pton(AF_INET,argv[i+1],&peer)!=1)continue;
  if(peer.s_addr==address.s_addr||(peer.s_addr&mask)!=(address.s_addr&mask))continue;
  if(candidate&&strcmp(candidate,argv[i+1]))return; // Ambiguous subnet.
  candidate=argv[i+1];
 }
 if(!candidate||endpoint<0)return;
 const char* suffix=strchr(argv[endpoint],'~');if(!suffix)return;
 size_t hostLength=(size_t)(suffix-argv[endpoint]);if(!hostLength||hostLength>=16)return;
 char host[16];memcpy(host,argv[endpoint],hostLength);host[hostLength]=0;
 struct in_addr original;if(inet_pton(AF_INET,host,&original)!=1)return;
 // Only a peer explicitly advertised for this authenticated Steam session.
 int advertised=0;
 for(int i=1;i+1<argc;i++)if(!strcmp(argv[i],"-c")&&!strcmp(argv[i+1],host))advertised=1;
 if(!advertised)return;
 if(!strcmp(host,candidate))return;
 size_t length=strlen(candidate)+strlen(suffix)+1;char* replacement=malloc(length);
 if(!replacement)return;
 snprintf(replacement,length,"%s%s",candidate,suffix);argv[endpoint]=replacement;
 fputs("Quest streaming: preferring advertised USB peer\n",stderr);
}
// Scope the glibc decoder adapter to the player. Android's shell and other
// guest utilities must never load a library linked against libstdc++/glibc.
int main(int argc,char** argv){
 char path[PATH_MAX],preload[PATH_MAX];ssize_t n=readlink("/proc/self/exe",path,sizeof(path)-1);
 if(n<0||(size_t)n>=sizeof(path)-6){perror("streaming launcher path");return 127;}path[n]=0;strcat(path,".real");
 if(!strcmp(strrchr(path,'/')+1,"vrlink.real")){
  prefer_usb(argc,argv);
  setenv("QUEST_VIDEO_STATEFUL","1",1);
  // Valve's Frame script adds 6 ms before submission. Android/OpenXR already
  // schedules and reprojects our output; do not stack that extra delay here.
  const char* delay=getenv("STEARLIGHT_VRLINK_SUBMIT_DELAY_US");
  setenv("VRLINK_SUBMIT_DELAY_US",delay?delay:"0",1);
 }
 const char* inherited=getenv("LD_PRELOAD");int length=snprintf(preload,sizeof(preload),"/opt/quest/libquest_video.so%s%s",inherited&&*inherited?":":"",inherited?inherited:"");
 if(length<0||(size_t)length>=sizeof(preload)){fputs("streaming preload path too long\n",stderr);return 127;}
 setenv("LD_PRELOAD",preload,1);setenv("EGL_PLATFORM","surfaceless",1);
 if(!getenv("QUEST_STREAM_SOCKET_BATCH"))setenv("QUEST_STREAM_SOCKET_BATCH","1",1);
 // The headless allocator must never attempt to become the KMS master.
 if(!getenv("DISPLAY")&&!getenv("WAYLAND_DISPLAY"))setenv("DISPLAY",":0",1);
 execv(path,argv);perror("Steam streaming player");return 127;
}
