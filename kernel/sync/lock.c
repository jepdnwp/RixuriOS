#include "lock.h"

#ifndef RIX_HOST_TEST
static inline uint64_t read_rflags(void){uint64_t v;__asm__ volatile("pushfq; popq %0":"=r"(v)::"memory");return v;}
static inline void cli(void){__asm__ volatile("cli":::"memory");}
static inline void sti(void){__asm__ volatile("sti":::"memory");}
#endif

void rix_spin_init(rix_spinlock_t *lock){if(lock)lock->value=0;}
void rix_spin_lock(rix_spinlock_t *lock){if(!lock)return;while(__atomic_exchange_n(&lock->value,1u,__ATOMIC_ACQUIRE)){while(__atomic_load_n(&lock->value,__ATOMIC_RELAXED))__asm__ volatile("pause":::"memory");}}
int rix_spin_trylock(rix_spinlock_t *lock){return lock&&__atomic_exchange_n(&lock->value,1u,__ATOMIC_ACQUIRE)==0;}
void rix_spin_unlock(rix_spinlock_t *lock){if(lock)__atomic_store_n(&lock->value,0u,__ATOMIC_RELEASE);}
#ifdef RIX_HOST_TEST
uint64_t rix_irq_save(void){return 0;}
void rix_irq_restore(uint64_t flags){(void)flags;}
#else
uint64_t rix_irq_save(void){uint64_t f=read_rflags();cli();return f;}
void rix_irq_restore(uint64_t flags){if(flags&(1ULL<<9))sti();else cli();}
#endif
void rix_spin_lock_irqsave(rix_spinlock_t *lock,uint64_t *flags){uint64_t f=rix_irq_save();if(flags)*flags=f;rix_spin_lock(lock);}
void rix_spin_unlock_irqrestore(rix_spinlock_t *lock,uint64_t flags){rix_spin_unlock(lock);rix_irq_restore(flags);}
