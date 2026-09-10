#pragma once
#include <stdint.h>

/* Cooperative userspace threads.
 *
 * What works today (process-local, no kernel support):
 * - pthread_mutex_t is a process-local spinlock over __atomic builtins.
 *   It serializes threads that share an address space; it provides no
 *   priority inheritance and must never be held across fork().
 * - pthread_once_t gives one-time initialization with the same scope.
 *
 * What fails closed with ENOSYS:
 * - pthread_create/join/detach: the kernel has no thread-spawn or
 *   futex primitive (see Phase 06/23). Use fork() + waitpid() for
 *   concurrency, or pipes/shared-memory for coordination.
 *
 * pthread functions return an error NUMBER (0 on success), per POSIX,
 * using the RIX_* codes from <errno.h>. */

typedef unsigned long pthread_t;
typedef struct {
    volatile int locked;
} pthread_mutex_t;
typedef struct {
    volatile int done;
} pthread_once_t;

#define PTHREAD_MUTEX_INITIALIZER { 0 }
#define PTHREAD_ONCE_INIT { 0 }

int pthread_mutex_init(pthread_mutex_t *mutex, const void *attributes);
int pthread_mutex_lock(pthread_mutex_t *mutex);
int pthread_mutex_trylock(pthread_mutex_t *mutex);
int pthread_mutex_unlock(pthread_mutex_t *mutex);
int pthread_mutex_destroy(pthread_mutex_t *mutex);
int pthread_once(pthread_once_t *once, void (*function)(void));
int pthread_create(pthread_t *thread, const void *attributes,
                   void *(*start)(void *), void *argument);
int pthread_join(pthread_t thread, void **result);
int pthread_detach(pthread_t thread);
pthread_t pthread_self(void);
int pthread_equal(pthread_t left, pthread_t right);
