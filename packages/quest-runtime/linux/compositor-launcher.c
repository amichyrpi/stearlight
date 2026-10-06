#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
int main(int argc,char **argv) {
 (void)argc;
 unsetenv("LD_PRELOAD");unsetenv("VRCOMPOSITOR_LD_PRELOAD");
 setenv("VK_ICD_FILENAMES","/opt/quest/quest_icd.json",1);
 argv[0]="/opt/steamvr/bin/linuxarm64/vrcompositor";
 execv("/opt/steamvr/bin/linuxarm64/vrcompositor.bin",argv);
 perror("Quest compositor launcher");return 127;
}
