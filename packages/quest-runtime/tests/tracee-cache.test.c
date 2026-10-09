#include <assert.h>
#include <stddef.h>
#include <sys/types.h>
typedef struct { pid_t pid; } Tracee;
#include "../proot/tracee-cache.h"

int main(void) {
 Tracee first={42},collision={42+1024},replacement={42},initial={0};
 quest_cache_tracee(&first);assert(quest_cached_tracee(42)==&first);
 quest_cache_tracee(&collision);
 assert(!quest_cached_tracee(42));assert(quest_cached_tracee(1066)==&collision);
 quest_uncache_tracee(&first);assert(quest_cached_tracee(1066)==&collision);
 quest_uncache_tracee(&collision);assert(!quest_cached_tracee(1066));
 quest_cache_tracee(&first);quest_uncache_tracee(&first);
 quest_cache_tracee(&replacement);assert(quest_cached_tracee(42)==&replacement);
 quest_cache_tracee(&initial);assert(!quest_cached_tracee(0));
 initial.pid=300;quest_cache_tracee(&initial);assert(quest_cached_tracee(300)==&initial);
 quest_uncache_tracee(&initial);assert(!quest_cached_tracee(300));
 assert(!quest_cached_tracee(-1));
 return 0;
}
