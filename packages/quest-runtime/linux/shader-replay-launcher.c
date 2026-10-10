#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

// Android limits the number of unmanaged app children. Steam's desktop
// worker count can cause it to kill the PRoot session during shader replay.
int main(int argc,char** argv){
 char path[PATH_MAX];ssize_t n=readlink("/proc/self/exe",path,sizeof(path)-6);
 if(n<0){perror("quest-shader-replay-launcher path");return 127;}
 path[n]=0;strcat(path,".real");
 char** args=calloc((size_t)argc+3,sizeof(char*));if(!args)return 127;
 args[0]=path;int count=1;
 for(int i=1;i<argc;i++){
  if(!strcmp(argv[i],"--num-threads")){if(i+1<argc)i++;continue;}
  if(!strncmp(argv[i],"--num-threads=",14))continue;
  args[count++]=argv[i];
 }
 args[count++]="--num-threads";args[count++]="1";args[count]=NULL;
 // Shader compilation needs no headset surface adapter.
 setenv("VK_ICD_FILENAMES","/opt/quest/turnip_icd.json",1);
 setenv("VK_DRIVER_FILES","/opt/quest/turnip_icd.json",1);
 execv(path,args);perror("quest-shader-replay-launcher exec");return 127;
}
