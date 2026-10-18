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
 for name in ['robust.c','robust.h','tracee-cache.h','syscall-fast-path.h','mountinfo-cache.h']:
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
 # Steam/SDL repeatedly open mountinfo while discovering controllers. Cache
 # the immutable rewritten file per shared binding namespace. Binding edits
 # and real kernel mount events invalidate it immediately.
 replace(src/'path/binding.h','typedef CIRCLEQ_HEAD(bindings, binding) Bindings;', '''typedef struct bindings {
 struct binding *cqh_first;
 struct binding *cqh_last;
 unsigned long long quest_mount_generation;
 struct quest_mountinfo_cache *quest_mount_cache;
} Bindings;''')
 path=src/'path/binding.c'
 for macro in ['CIRCLEQ_INSERT_AFTER_(tracee, previous, binding, side)',
               'CIRCLEQ_INSERT_BEFORE_(tracee, next, binding, side)',
               'CIRCLEQ_INSERT_HEAD_(tracee, binding, side)']:
  text=path.read_text();start=text.index('#define '+macro);end=text.index('\n',start)
  text=text[:end+1]+'\tHEAD(tracee, side)->quest_mount_generation++;\t\\\n'+text[end+1:]
  path.write_text(text)
 replace(path,'\tCIRCLEQ_REMOVE((tracee)->fs->bindings.name, binding, link.name);\\',
              '\t(tracee)->fs->bindings.name->quest_mount_generation++;\t\\\n\tCIRCLEQ_REMOVE((tracee)->fs->bindings.name, binding, link.name);\\')
 path=src/'extension/mountinfo/mountinfo.c'
 replace(path,'#include <sys/queue.h>', '#include "tracee/mountinfo-cache.h"\n#include "tracee/reg.h"\n#include <sys/queue.h>')
 replace(path,'if (target_tracee == NULL) {','if (target_tracee == NULL || target_tracee->terminated) {')
 replace(path,'\t\t/* Check if our root is under "/data"  */', '''\t\tBindings *bindings = target_tracee->fs->bindings.guest;
\t\tSysnum request = get_sysnum(tracee, ORIGINAL);
\t\tword_t flags = peek_reg(tracee, ORIGINAL, request == PR_open ? SYSARG_2 : SYSARG_3);
\t\tbool cache_read = (flags & O_ACCMODE) == O_RDONLY && !(flags & (O_CREAT | O_TRUNC | O_EXCL));
\t\tconst char *cached = cache_read ? quest_mountinfo_cached(tracee->ctx, bindings) : NULL;
\t\tif (cached) { strncpy(path, cached, PATH_MAX); return; }

\t\t/* Check if our root is under "/data"  */''')
 replace(path,'\t\tconst char *new_path = create_temp_file(tracee->ctx, "mountinfo");', '''\t\tstruct quest_mountinfo_cache *cache = cache_read ? quest_mountinfo_begin(bindings, fileno(real_mountinfo_fp)) : NULL;
\t\tconst char *new_path = create_temp_file(cache ? (TALLOC_CTX *) cache : tracee->ctx, "mountinfo");
\t\tif (!new_path) { if (cache) talloc_free(cache); fclose(real_mountinfo_fp); return; }''')
 replace(path,'\t\tif (new_mountinfo_fp == NULL) {','\t\tif (new_mountinfo_fp == NULL) {\n\t\t\tif (cache) talloc_free(cache);')
 replace(path,'\t\tfclose(new_mountinfo_fp);', '\t\tint write_failed = ferror(new_mountinfo_fp);\n\t\tif (fclose(new_mountinfo_fp)) write_failed = 1;')
 replace(path,'\t\tfclose(real_mountinfo_fp);\n\n\t\t/* Redirect', '\t\tint read_failed = ferror(real_mountinfo_fp);\n\t\tfclose(real_mountinfo_fp);\n\n\t\t/* Redirect')
 replace(path,'\t\tif (found_line) {\n\t\t\tstrncpy(path, new_path, PATH_MAX);\n\t\t}', '''\t\tif (found_line && !write_failed && !read_failed) {
\t\t\tquest_mountinfo_publish(tracee->ctx, bindings, cache, new_path);
\t\t\tstrncpy(path, new_path, PATH_MAX);
\t\t} else if (cache) talloc_free(cache);''')
 replace(src/'tracee/event.c','\t\t\t\tPTRACE_O_TRACEEXIT);','\t\t\t\tPTRACE_O_TRACEEXIT | PTRACE_O_EXITKILL);')
 replace(src/'tracee/event.c','\t\tsignal(SIGPIPE, SIG_DFL);','\t\tsignal(SIGPIPE, SIG_DFL);\n\t\t/* ART worker masks must not become Linux process masks. */\n\t\tsigset_t empty_mask;\n\t\tsigemptyset(&empty_mask);\n\t\tsigprocmask(SIG_SETMASK, &empty_mask, NULL);')
 replace(src/'tracee/tracee.h','\t/* Unique tracee identifier. */','\tword_t quest_robust_head;\n\tbool quest_robust_retry;\n\n\t/* Unique tracee identifier. */')
 # Hundreds of browser threads make list scans on every ptrace stop expensive.
 # Cache only positive PIDs; the initial configured tracee changes from PID 0
 # after fork. Destruction invalidates the cache before a PID can be reused.
 path=src/'tracee/tracee.c'
 replace(path,'static Tracees tracees;', '''#include "tracee/tracee-cache.h"
static Tracees tracees;
static bool quest_have_terminated;
''')
 replace(path,'\tLIST_REMOVE(tracee, link);', '''\tquest_uncache_tracee(tracee);
\tLIST_REMOVE(tracee, link);''')
 replace(path,'\ttracee->vpid = next_vpid++;', '''\ttracee->vpid = next_vpid++;
\tquest_cache_tracee(tracee);''')
 replace(path,'''\tLIST_FOREACH(tracee, &tracees, link) {
\t\tif (tracee->pid == pid) {''', '''\tTracee *cached = quest_cached_tracee(pid);
\tif (cached != NULL) {
\t\tTALLOC_FREE(cached->ctx);
\t\tcached->ctx = talloc_new(cached);
\t\treturn cached;
\t}
\tLIST_FOREACH(tracee, &tracees, link) {
\t\tif (tracee->pid == pid) {
\t\t\tquest_cache_tracee(tracee);''')
 replace(path,'\ttracee->terminated = true;', '\ttracee->terminated = true;\n\tquest_have_terminated = true;')
 replace(path,'''void free_terminated_tracees()
{
\tTracee *next;''', '''void free_terminated_tracees()
{
\tTracee *next;
\tif (!quest_have_terminated) return;
\tquest_have_terminated = false;''')
 replace(src/'GNUmakefile','\ttracee/tracee.o', '\ttracee/robust.o\t\t\\\n\ttracee/tracee.o')
 for name in ['syscall/enter.c','syscall/exit.c','tracee/event.c','tracee/seccomp.c']:
  path=src/name
  replace(path,'#include "tracee/mem.h"','#include "tracee/mem.h"\n#include "tracee/robust.h"')
 replace(src/'tracee/seccomp.c','\tstatus = notify_extensions(tracee, SIGSYS_OCC, 0, 0);','\tif (quest_robust_sigsys(tracee)) return 0;\n\n\tstatus = notify_extensions(tracee, SIGSYS_OCC, 0, 0);')
 replace(src/'syscall/enter.c','\t/* Translate input arguments. */','\tif (quest_robust_enter(tracee)) return 0;\n\n\t/* Translate input arguments. */')
 replace(src/'syscall/seccomp.c','static FilteredSysnum proot_sysnums[] = {','static FilteredSysnum proot_sysnums[] = {\n\t{ PR_futex, FILTER_SYSEXIT },\n\t{ PR_get_robust_list, FILTER_SYSEXIT },\n\t{ PR_set_robust_list, FILTER_SYSEXIT },\n\t{ PR_setuid, FILTER_SYSEXIT },\n\t{ PR_setgid, FILTER_SYSEXIT },\n\t{ PR_setreuid, FILTER_SYSEXIT },\n\t{ PR_setregid, FILTER_SYSEXIT },\n\t{ PR_setresuid, FILTER_SYSEXIT },\n\t{ PR_setresgid, FILTER_SYSEXIT },')
 replace(src/'syscall/seccomp.c','\t{ PR_futex, FILTER_SYSEXIT },','\t{ PR_uname, FILTER_SYSEXIT },\n\t{ PR_futex, FILTER_SYSEXIT },')
 replace(src/'syscall/seccomp.c','#define DEBUG_FILTER(', '#include "tracee/syscall-fast-path.h"\n\n#define DEBUG_FILTER(')
 replace(src/'syscall/seccomp.c','program->filter = talloc_array(NULL, struct sock_filter, 0);',
  'program->filter = talloc_memdup(NULL, quest_syscall_fast_path, sizeof(quest_syscall_fast_path));')
 replace(src/'syscall/exit.c','\t/* Translate output arguments:', '\tquest_robust_exit(tracee);\n\n\t/* Translate output arguments:')
 replace(src/'tracee/event.c','\t\tcase SIGTRAP | PTRACE_EVENT_VFORK_DONE << 8:\n\t\tcase SIGTRAP | PTRACE_EVENT_EXEC  << 8:\n\t\tcase SIGTRAP | PTRACE_EVENT_EXIT  << 8:', '\t\tcase SIGTRAP | PTRACE_EVENT_EXIT << 8:\n\t\t\tquest_robust_death(tracee);\n\t\t\tsignal = 0;\n\t\t\tif (tracee->last_restart_how) tracee->restart_how = tracee->last_restart_how;\n\t\t\tbreak;\n\n\t\tcase SIGTRAP | PTRACE_EVENT_EXEC << 8:\n\t\t\ttracee->quest_robust_head = 0;\n\t\t\t/* fall through */\n\t\tcase SIGTRAP | PTRACE_EVENT_VFORK_DONE << 8:')
if __name__=='__main__':patch(sys.argv[1])
