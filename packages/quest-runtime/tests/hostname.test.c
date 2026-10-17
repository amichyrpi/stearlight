/* Run inside the installed Quest guest with Android's uname -r and -v as
 * arguments: libc and direct syscalls must agree without altering the kernel. */
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/utsname.h>
#include <unistd.h>

int main(int argc,char** argv) {
 assert(argc==3);
 struct utsname libc_name,raw;
 assert(uname(&libc_name)==0);
 assert(syscall(SYS_uname,&raw)==0);
 assert(!strcmp(raw.nodename,"frame"));
 assert(!strcmp(libc_name.nodename,raw.nodename));
 char hostname[256];assert(gethostname(hostname,sizeof(hostname))==0);
 assert(!strcmp(hostname,"frame"));
 assert(!strcmp(raw.sysname,"Linux")&&!strcmp(raw.machine,"aarch64"));
 assert(!strcmp(raw.release,argv[1])&&!strcmp(raw.version,argv[2]));
 errno=0;assert(syscall(SYS_uname,NULL)==-1&&errno==EFAULT);
 puts("QUEST_HOSTNAME_VERIFIED: frame via libc and syscall; kernel identity and EFAULT preserved");
 return 0;
}
