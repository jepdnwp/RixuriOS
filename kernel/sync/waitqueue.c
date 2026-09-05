#include "waitqueue.h"
#include <stddef.h>

void rix_waitqueue_init(rix_waitqueue_t *queue){
    if(!queue)return;
    rix_spin_init(&queue->lock);queue->count=0;
    for(uint32_t i=0;i<RIX_WQ_MAX_WAITERS;i++){queue->waiters[i].id=0;queue->waiters[i].state=RIX_WAIT_UNUSED;}
}

rix_waiter_t *rix_waitqueue_prepare(rix_waitqueue_t *queue,uint64_t id){
    if(!queue)return NULL;
    rix_spin_lock(&queue->lock);
    for(uint32_t i=0;i<RIX_WQ_MAX_WAITERS;i++){
        if(queue->waiters[i].state==RIX_WAIT_UNUSED){
            queue->waiters[i].id=id;queue->waiters[i].state=RIX_WAIT_READY;queue->count++;rix_spin_unlock(&queue->lock);return &queue->waiters[i];
        }
    }
    rix_spin_unlock(&queue->lock);return NULL;
}

int rix_waitqueue_block(rix_waitqueue_t *queue,rix_waiter_t *waiter){
    if(!queue||!waiter)return -1;
    rix_spin_lock(&queue->lock);
    if(waiter->state!=RIX_WAIT_READY){rix_spin_unlock(&queue->lock);return -1;}
    waiter->state=RIX_WAIT_BLOCKED;rix_spin_unlock(&queue->lock);return 0;
}

void rix_waitqueue_wake_one(rix_waitqueue_t *queue){
    if(!queue)return;
    rix_spin_lock(&queue->lock);
    for(uint32_t i=0;i<RIX_WQ_MAX_WAITERS;i++)if(queue->waiters[i].state==RIX_WAIT_BLOCKED){queue->waiters[i].state=RIX_WAIT_WOKEN;break;}
    rix_spin_unlock(&queue->lock);
}

void rix_waitqueue_wake_all(rix_waitqueue_t *queue){
    if(!queue)return;
    rix_spin_lock(&queue->lock);
    for(uint32_t i=0;i<RIX_WQ_MAX_WAITERS;i++)if(queue->waiters[i].state==RIX_WAIT_BLOCKED)queue->waiters[i].state=RIX_WAIT_WOKEN;
    rix_spin_unlock(&queue->lock);
}

void rix_waitqueue_remove(rix_waitqueue_t *queue,rix_waiter_t *waiter){
    if(!queue||!waiter)return;
    rix_spin_lock(&queue->lock);
    if(waiter->state!=RIX_WAIT_UNUSED){waiter->id=0;waiter->state=RIX_WAIT_UNUSED;if(queue->count)queue->count--;}
    rix_spin_unlock(&queue->lock);
}
