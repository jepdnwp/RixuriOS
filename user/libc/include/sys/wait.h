#pragma once
#include <sys/types.h>

/* Wait-status inspection over the RixuriOS native status word.
 *
 * The kernel reports the raw 64-bit exit code passed to process exit
 * (see process_exit); it never encodes signal deaths today because
 * userspace signal-frame delivery does not exist yet. The macros below
 * therefore describe that reality: WIFEXITED is always true and the exit
 * code is the low 8 bits, matching how the shell consumes status.
 * Signal-encoded statuses remain UNSUPPORTED until handler delivery lands.
 *
 * WNOHANG matches RIX_WAITPID_NOHANG. wait()/waitpid() themselves keep
 * their native <unistd.h> spelling (rix status word); these macros are
 * the portable way to read the word. */

#define WNOHANG 1
#define WEXITSTATUS(status) ((int)((status) & 0xff))
#define WIFEXITED(status) (1)
#define WIFSIGNALED(status) (0)
#define WTERMSIG(status) (0)
#define WIFSTOPPED(status) (0)
#define WSTOPSIG(status) (0)
