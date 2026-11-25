#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
int main(int argc,char **argv) {
 (void)argc;
 unsetenv("LD_PRELOAD");unsetenv("VRCOMPOSITOR_LD_PRELOAD");
 // Let Mesa enable a submit thread when the compositor's Vulkan queues need it.
 unsetenv("MESA_VK_ENABLE_SUBMIT_THREAD");
 setenv("VK_ICD_FILENAMES","/opt/quest/quest_icd.json",1);
 setenv("QUEST_FRAME_FINAL_PASS","1",1);
 argv[0]="/opt/steamvr/bin/linuxarm64/vrcompositor";
 execv("/opt/steamvr/bin/linuxarm64/vrcompositor.bin",argv);
 perror("Quest compositor launcher");return 127;
}
