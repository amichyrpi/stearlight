#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
// Steam validates both ends of its CEF loopback transport. Read the actual
// socket inodes, descriptors, UID and parent PID without a shell/fork pipeline
// under PRoot. All other lsof queries use the original GNU binary.
struct endpoint { unsigned local,remote,lp,rp; uint64_t inode; };
static struct endpoint endpoints[256];
static unsigned count;
static int table(unsigned port){
 FILE* f=fopen("/proc/net/tcp","r");if(!f)return -1;
 char* line=NULL;size_t length=0;count=0;
 while(getline(&line,&length,f)>0){
  struct endpoint e;unsigned state,uid;
  if(sscanf(line," %*u: %x:%x %x:%x %x %*s %*s %*s %u %*u %"SCNu64,&e.local,&e.lp,&e.remote,&e.rp,&state,&uid,&e.inode)!=7)continue;
  if(uid!=getuid()||!e.inode||(e.lp!=port&&e.rp!=port)||
     (e.local!=0x0100007f&&e.remote!=0x0100007f))continue;
  if(count==256){free(line);fclose(f);return -1;}endpoints[count++]=e;
 }
 free(line);fclose(f);return count;
}
static void address(unsigned ip,unsigned port){printf("%u.%u.%u.%u:%u",ip&255,(ip>>8)&255,(ip>>16)&255,ip>>24,port);}
static int output(void){
 DIR* proc=opendir("/proc");if(!proc)return -1;struct dirent* row;int owners=0;
 while((row=readdir(proc))){char* end;unsigned long pid=strtoul(row->d_name,&end,10);if(!pid||*end)continue;
  char path[512];snprintf(path,sizeof(path),"/proc/%lu",pid);struct stat st;if(stat(path,&st)||st.st_uid!=getuid())continue;
  snprintf(path,sizeof(path),"/proc/%lu/fd",pid);DIR* fd=opendir(path);if(!fd)continue;
  struct dirent* item;unsigned printed=0,parent=0;
  while((item=readdir(fd))){if(item->d_name[0]=='.')continue;char link[256];
   snprintf(path,sizeof(path),"/proc/%lu/fd/%s",pid,item->d_name);
   ssize_t n=readlink(path,link,sizeof(link)-1);if(n<=0)continue;link[n]=0;uint64_t inode;
   if(sscanf(link,"socket:[%"SCNu64"]",&inode)!=1)continue;
   for(unsigned i=0;i<count;i++)if(inode==endpoints[i].inode){
    if(!printed){snprintf(path,sizeof(path),"/proc/%lu/stat",pid);FILE* f=fopen(path,"r");char data[4096];char state;
     if(!f||!fgets(data,sizeof(data),f)){if(f)fclose(f);continue;}fclose(f);char* close=strrchr(data,')');
     if(!close||sscanf(close+1," %c %u",&state,&parent)!=2)continue;
     printf("p%lu\nR%u\nu%u\n",pid,parent,st.st_uid);printed=1;owners++;
    }
    putchar('n');address(endpoints[i].local,endpoints[i].lp);if(endpoints[i].remote){fputs("->",stdout);address(endpoints[i].remote,endpoints[i].rp);}putchar('\n');
   }
  }closedir(fd);
 }closedir(proc);return owners;
}
int main(int argc,char** argv){
 if(argc==6&&!strcmp(argv[1],"-P")&&!strcmp(argv[2],"-F")&&!strcmp(argv[3],"upnR")&&!strcmp(argv[4],"-i")&&!strncmp(argv[5],"TCP@127.0.0.1:",14)){
  char* end;unsigned long port=strtoul(argv[5]+14,&end,10);
  if(port>0&&port<65536&&!*end){for(int i=0;i<6;i++){int n=table(port);if(n<0)break;if(n&&output()>0)return 0;usleep(40000);}}
 }
 argv[0]="/opt/quest/lsof";execv(argv[0],argv);perror("lsof fallback");return 127;
}
