/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef QUEST_TRACEE_CACHE_H
#define QUEST_TRACEE_CACHE_H

/* Non-owning cache. A collision falls back to PRoot's authoritative list. */
static Tracee *quest_pid_cache[1024];
static inline Tracee *quest_cached_tracee(pid_t pid) {
 Tracee *value = pid > 0 ? quest_pid_cache[(unsigned)pid % 1024] : NULL;
 return value && value->pid == pid ? value : NULL;
}
static inline void quest_cache_tracee(Tracee *tracee) {
 if (tracee->pid > 0) quest_pid_cache[(unsigned)tracee->pid % 1024] = tracee;
}
static inline void quest_uncache_tracee(Tracee *tracee) {
 if (tracee->pid > 0 && quest_pid_cache[(unsigned)tracee->pid % 1024] == tracee)
  quest_pid_cache[(unsigned)tracee->pid % 1024] = NULL;
}
#endif
