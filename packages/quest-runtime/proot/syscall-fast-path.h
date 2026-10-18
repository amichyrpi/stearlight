/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef QUEST_SYSCALL_FAST_PATH_H
#define QUEST_SYSCALL_FAST_PATH_H
#include <stddef.h>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/futex.h>
#include <linux/seccomp.h>

/* ARM64 FUTEX_WAKE, TCGETS and Adreno KGSL ioctls need no PRoot translation. Let the
 * kernel handle these directly. Waits whose expected owner TID is 0, 1 or 2
 * also need no robust-list adaptation: app tracees cannot be Android's init
 * or kernel process. If the actual word differs, the kernel returns EAGAIN.
 * All potentially guest-owned waits still reach the original filter.
 * Other ABIs/syscalls fall through unchanged. Android's inherited policy
 * remains in force. ARM64 syscall numbers: ioctl=29, futex=98. Mesa's pinned
 * freedreno/vulkan/msm_kgsl.h defines KGSL_IOC_TYPE=0x09. TCGETS=0x5401 is the
 * read-only termios query used by isatty during udev's mount-table reads.
 * TCGETS2, terminal setters and other translated requests still fall through. */
static const struct sock_filter quest_syscall_fast_path[] = {
 BPF_STMT(BPF_LD+BPF_W+BPF_ABS, offsetof(struct seccomp_data,arch)),
 BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, AUDIT_ARCH_AARCH64,0,16),
 BPF_STMT(BPF_LD+BPF_W+BPF_ABS, offsetof(struct seccomp_data,nr)),
 BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K,29,0,4),
 BPF_STMT(BPF_LD+BPF_W+BPF_ABS, offsetof(struct seccomp_data,args[1])),
 BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K,0x5401,11,0),
 BPF_STMT(BPF_ALU+BPF_AND+BPF_K,0xff00),
 BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K,0x0900,9,10),
 BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K,98,0,9),
 BPF_STMT(BPF_LD+BPF_W+BPF_ABS, offsetof(struct seccomp_data,args[1])),
 BPF_STMT(BPF_ALU+BPF_AND+BPF_K,FUTEX_CMD_MASK),
 BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K,FUTEX_WAKE,5,0),
 BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K,FUTEX_WAIT,1,0),
 BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K,FUTEX_WAIT_BITSET,0,4),
 BPF_STMT(BPF_LD+BPF_W+BPF_ABS, offsetof(struct seccomp_data,args[2])),
 BPF_STMT(BPF_ALU+BPF_AND+BPF_K,FUTEX_TID_MASK),
 BPF_JUMP(BPF_JMP+BPF_JGT+BPF_K,2,1,0),
 BPF_STMT(BPF_RET+BPF_K,SECCOMP_RET_ALLOW)
};
#endif
