#pragma once
#include <stdint.h>
#include "lock.h"

/* Cooperative reader/writer lock.
 *
 * Same context model as rix_mutex_t: the blocking variants may yield
 * (task context only), the try variants are safe anywhere. Writers
 * are preferred: once a writer waits, new readers queue behind it, so
 * a steady reader stream cannot starve a writer. Recursive read
 * locking is harmless (counts); a read-holder must never write-lock
 * (self-deadlock, documented, not tracked per-task by design).
 * Recursive write-locking fails instead of deadlocking.
 *
 * 0 ok; -1 bad argument; 1 busy (try only); -2 recursive write lock;
 * -3 unlock without a matching hold.
 */
typedef struct {
    rix_spinlock_t guard;
    uint32_t readers;
    uint32_t waiting_writers;
    uint64_t writer;
    int write_locked;
} rix_rwlock_t;

void rix_rwlock_init(rix_rwlock_t *rw);
int rix_rwlock_read_lock(rix_rwlock_t *rw);
int rix_rwlock_read_trylock(rix_rwlock_t *rw);
int rix_rwlock_read_unlock(rix_rwlock_t *rw);
int rix_rwlock_write_lock(rix_rwlock_t *rw);
int rix_rwlock_write_trylock(rix_rwlock_t *rw);
int rix_rwlock_write_unlock(rix_rwlock_t *rw);
