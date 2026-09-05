#pragma once
#include <stdint.h>
#include "process.h"

#define RIX_SIG_MIN 1u
#define RIX_SIG_MAX 64u
#define RIX_SIGKILL 9u
#define RIX_SIGSTOP 19u

int process_signal_send(pid_t pid, unsigned signal);
int process_signal_mask(pid_t pid, uint64_t mask);
int process_signal_pending(pid_t pid, uint64_t *pending);
int process_signal_take(pid_t pid, unsigned *signal);
