/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef QUEST_ROBUST_H
#define QUEST_ROBUST_H
#include "tracee/tracee.h"
int quest_robust_enter(Tracee *tracee);
int quest_robust_sigsys(Tracee *tracee);
void quest_robust_exit(Tracee *tracee);
void quest_robust_death(Tracee *tracee);
#endif
