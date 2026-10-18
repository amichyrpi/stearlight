/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef QUEST_MOUNTINFO_CACHE_H
#define QUEST_MOUNTINFO_CACHE_H
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Owned by the shared guest binding list, not by a particular PID or cwd.
 * Keep the kernel mountinfo descriptor for its mount-namespace change events.
 * Generated files stay immutable: an already opened snapshot must survive
 * refreshing the cache, and a translated open must retain its pathname until
 * the kernel has actually opened it. */
struct quest_mountinfo_cache {
 const char *path;
 unsigned long long generation;
 int mount_fd;
 int invalid;
};

static int quest_mountinfo_cache_destroy(struct quest_mountinfo_cache *cache) {
 if (cache->mount_fd >= 0) close(cache->mount_fd);
 return 0;
}

static const char *quest_mountinfo_cached(TALLOC_CTX *reader, Bindings *bindings) {
 if (!bindings || !bindings->quest_mount_cache) return NULL;
 struct quest_mountinfo_cache *cache = bindings->quest_mount_cache;
 if (cache->invalid || cache->generation != bindings->quest_mount_generation || cache->mount_fd < 0) return NULL;
 struct pollfd event = { .fd = cache->mount_fd, .events = POLLPRI };
 /* mountinfo reports POLLPRI/POLLERR after a real mount change. Treat any
  * event or poll failure conservatively as a miss, including a stale fd. */
 if (poll(&event, 1, 0) != 0) { cache->invalid = 1; return NULL; }
 return talloc_reference(reader, cache->path);
}

static struct quest_mountinfo_cache *quest_mountinfo_begin(Bindings *bindings, int source_fd) {
 const char *enabled = getenv("PROOT_QUEST_ROBUST");
 if (!bindings || !enabled || strcmp(enabled, "1")) return NULL;
 struct quest_mountinfo_cache *cache = talloc_zero(bindings, struct quest_mountinfo_cache);
 if (!cache) return NULL;
 cache->mount_fd = fcntl(source_fd, F_DUPFD_CLOEXEC, 0);
 if (cache->mount_fd < 0) { talloc_free(cache); return NULL; }
 cache->generation = bindings->quest_mount_generation;
 talloc_set_destructor(cache, quest_mountinfo_cache_destroy);
 return cache;
}

static void quest_mountinfo_publish(TALLOC_CTX *reader, Bindings *bindings,
        struct quest_mountinfo_cache *cache, const char *path) {
 if (!cache) return;
 cache->path = path;
 if (!talloc_reference(reader, path)) {
  /* Preserve this request's file even if retaining it for reuse fails. */
  talloc_steal(reader, cache);
  return;
 }
 struct quest_mountinfo_cache *previous = bindings->quest_mount_cache;
 bindings->quest_mount_cache = cache;
 if (previous) talloc_free(previous);
}
#endif
