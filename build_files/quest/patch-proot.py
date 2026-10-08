#!/usr/bin/env python3
"""Apply the Quest app-policy adapter to the pinned PRoot source tree."""
from pathlib import Path
import shutil,sys
ROOT=Path(__file__).resolve().parents[2]
def replace(path,old,new):
 text=path.read_text()
 if text.count(old)!=1:raise RuntimeError(f'Unexpected PRoot source: {path}: {old[:80]}')
 path.write_text(text.replace(old,new))
def patch(source):
 src=Path(source)/'src'
 for name in ['robust.c','robust.h']:
  shutil.copyfile(ROOT/'packages/quest-runtime/proot'/name,src/'tracee'/name)
 replace(src/'extension/ashmem_memfd/ashmem_memfd.c','#include <stdlib.h>','#include <stdlib.h>\n#include <string.h>')
 # All guest processes share this PRoot IPC namespace. Use anonymous memfd
 # storage instead of the obsolete ashmem device denied to ordinary apps.
 path=src/'extension/sysvipc/sysvipc_shm.c'
 text=path.read_text()
 start=text.index('\tint fd = open("/dev/ashmem", O_RDWR, 0);',text.index('static int sysvipc_shm_do_allocate'))
 end=text.index('\n#else',start)
 text=text[:start]+'''\tchar name[64];
\tsnprintf(name, sizeof(name), "quest_sysv_%X", shmid);
\tint fd = syscall(__NR_memfd_create, name, 1 /* MFD_CLOEXEC */);
\tif (fd < 0) return -errno;
\tif (ftruncate(fd, size) < 0) {
\t\tint error = errno;
\t\tclose(fd);
\t\treturn -error;
\t}
\treturn fd;'''+text[end:]
 path.write_text(text)
 replace(src/'tracee/event.c','\t\t\t\tPTRACE_O_TRACEEXIT);','\t\t\t\tPTRACE_O_TRACEEXIT | PTRACE_O_EXITKILL);')
 replace(src/'tracee/event.c','\t\tsignal(SIGPIPE, SIG_DFL);','\t\tsignal(SIGPIPE, SIG_DFL);\n\t\t/* ART worker masks must not become Linux process masks. */\n\t\tsigset_t empty_mask;\n\t\tsigemptyset(&empty_mask);\n\t\tsigprocmask(SIG_SETMASK, &empty_mask, NULL);')
 replace(src/'tracee/tracee.h','\t/* Unique tracee identifier. */','\tword_t quest_robust_head;\n\tbool quest_robust_retry;\n\n\t/* Unique tracee identifier. */')
 replace(src/'GNUmakefile','\ttracee/tracee.o', '\ttracee/robust.o\t\t\\\n\ttracee/tracee.o')
 for name in ['syscall/enter.c','syscall/exit.c','tracee/event.c','tracee/seccomp.c']:
  path=src/name
  replace(path,'#include "tracee/mem.h"','#include "tracee/mem.h"\n#include "tracee/robust.h"')
 replace(src/'tracee/seccomp.c','\tstatus = notify_extensions(tracee, SIGSYS_OCC, 0, 0);','\tif (quest_robust_sigsys(tracee)) return 0;\n\n\tstatus = notify_extensions(tracee, SIGSYS_OCC, 0, 0);')
 replace(src/'syscall/enter.c','\t/* Translate input arguments. */','\tif (quest_robust_enter(tracee)) return 0;\n\n\t/* Translate input arguments. */')
 replace(src/'syscall/seccomp.c','static FilteredSysnum proot_sysnums[] = {','static FilteredSysnum proot_sysnums[] = {\n\t{ PR_futex, FILTER_SYSEXIT },\n\t{ PR_get_robust_list, FILTER_SYSEXIT },\n\t{ PR_set_robust_list, FILTER_SYSEXIT },\n\t{ PR_setuid, FILTER_SYSEXIT },\n\t{ PR_setgid, FILTER_SYSEXIT },\n\t{ PR_setreuid, FILTER_SYSEXIT },\n\t{ PR_setregid, FILTER_SYSEXIT },\n\t{ PR_setresuid, FILTER_SYSEXIT },\n\t{ PR_setresgid, FILTER_SYSEXIT },')
 replace(src/'syscall/exit.c','\t/* Translate output arguments:', '\tquest_robust_exit(tracee);\n\n\t/* Translate output arguments:')
 replace(src/'tracee/event.c','\t\tcase SIGTRAP | PTRACE_EVENT_VFORK_DONE << 8:\n\t\tcase SIGTRAP | PTRACE_EVENT_EXEC  << 8:\n\t\tcase SIGTRAP | PTRACE_EVENT_EXIT  << 8:', '\t\tcase SIGTRAP | PTRACE_EVENT_EXIT << 8:\n\t\t\tquest_robust_death(tracee);\n\t\t\tsignal = 0;\n\t\t\tif (tracee->last_restart_how) tracee->restart_how = tracee->last_restart_how;\n\t\t\tbreak;\n\n\t\tcase SIGTRAP | PTRACE_EVENT_EXEC << 8:\n\t\t\ttracee->quest_robust_head = 0;\n\t\t\t/* fall through */\n\t\tcase SIGTRAP | PTRACE_EVENT_VFORK_DONE << 8:')
if __name__=='__main__':patch(sys.argv[1])
