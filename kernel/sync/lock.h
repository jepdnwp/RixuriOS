#pragma once
#include <stdint.h>

typedef struct { volatile uint32_t value; } rix_spinlock_t;

void rix_spin_init(rix_spinlock_t *lock);
void rix_spin_lock(rix_spinlock_t *lock);
int rix_spin_trylock(rix_spinlock_t *lock);
void rix_spin_unlock(rix_spinlock_t *lock);
uint64_t rix_irq_save(void);
void rix_irq_restore(uint64_t flags);
void rix_spin_lock_irqsave(rix_spinlock_t *lock, uint64_t *flags);
void rix_spin_unlock_irqrestore(rix_spinlock_t *lock, uint64_t flags);
