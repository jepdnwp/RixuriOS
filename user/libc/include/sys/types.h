#pragma once
#include <stddef.h>
#include <stdint.h>

/* POSIX base types over the RixuriOS native ABI.
 *
 * Ownership: pure type aliases, no lifetime.
 * Divergence: pid_t is 64-bit (native rix_pid_t is uint64_t); values in
 * practice fit both. off_t is shared with <unistd.h> through the
 * RIXURI_OFF_T_DEFINED guard so either include order is safe. */

typedef int64_t pid_t;
typedef int64_t ssize_t;
#ifndef RIXURI_OFF_T_DEFINED
#define RIXURI_OFF_T_DEFINED
typedef int64_t off_t;
#endif
typedef uint32_t mode_t;
typedef uint32_t uid_t;
typedef uint32_t gid_t;
typedef uint64_t dev_t;
typedef uint64_t ino_t;
typedef uint64_t nlink_t;
