#pragma once
#include <stdint.h>

/* lockdep-lite: rank-order checking for lock nesting.
 *
 * Each tracked lock class registers once with a rank. Acquiring must
 * observe strictly increasing ranks down the held stack; anything
 * else (inversion, equal ranks, recursion of a non-recursive lock)
 * prints a serial warning, bumps the violation counter, and returns
 * -2 WITHOUT recording the acquisition (warn-only: it never changes
 * locking behavior). Release must pop the stack top; anything else
 * warns the same way.
 *
 * Tracking is per CPU (weak lockdep_cpu_index defaults to 0; smp.c
 * overrides with the real index, same pattern as tss_cpu_index).
 * Only wired locks are checked; unwired locks are invisible, so a
 * clean report means "no inversion among wired locks", nothing more.
 *
 * 0 ok (or untracked class 0); -1 bad argument; -2 order violation
 * or release mismatch (already warned).
 */
#define RIX_LOCKDEP_MAX_CLASSES 32u
#define RIX_LOCKDEP_MAX_DEPTH 8u
#define RIX_LOCKDEP_CPUS 64u
#define RIX_LOCKDEP_UNTRACKED 0u

int rix_lockdep_register(const char *name, unsigned rank, unsigned *out_class);
int rix_lockdep_acquire(unsigned klass);
int rix_lockdep_release(unsigned klass);
unsigned rix_lockdep_violations(void);
const char *rix_lockdep_name(unsigned klass);
