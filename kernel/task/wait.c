/* =============================================================================
 * ZeruX OS - Unified Wait Queues
 * File: kernel/task/wait.c
 * =============================================================================
 */

#include "wait.h"
#include "kheap.h"

void init_waitqueue_head(wait_queue_head_t *q) {
    if (q) {
        q->head = NULL;
        q->tail = NULL;
    }
}

/* Blocks the current task and adds it to the wait queue */
void wait_event_interruptible(wait_queue_head_t *q) {
    if (!q) return;
    
    task_t *curr = task_get_current();
    if (!curr) return;
    
    wait_queue_entry_t *entry = kmalloc(sizeof(wait_queue_entry_t));
    if (!entry) return;
    
    entry->task = curr;
    entry->next = NULL;
    
    /* CLI to protect queue modification */
    uint32_t flags;
    __asm__ __volatile__("pushf; cli; pop %0" : "=r"(flags));
    
    if (!q->head) {
        q->head = entry;
        q->tail = entry;
    } else {
        q->tail->next = entry;
        q->tail = entry;
    }
    
    /* Restore interrupts if they were enabled */
    if (flags & (1 << 9)) __asm__ __volatile__("sti");
    
    /* Block task */
    task_block();
}

/* Wakes up the first task in the wait queue */
void wake_up(wait_queue_head_t *q) {
    if (!q || !q->head) return;
    
    uint32_t flags;
    __asm__ __volatile__("pushf; cli; pop %0" : "=r"(flags));
    
    wait_queue_entry_t *entry = q->head;
    if (entry) {
        q->head = entry->next;
        if (!q->head) q->tail = NULL;
        
        task_unblock(entry->task);
        kfree(entry);
    }
    
    if (flags & (1 << 9)) __asm__ __volatile__("sti");
}

/* Wakes up all tasks in the wait queue */
void wake_up_all(wait_queue_head_t *q) {
    if (!q || !q->head) return;
    
    uint32_t flags;
    __asm__ __volatile__("pushf; cli; pop %0" : "=r"(flags));
    
    wait_queue_entry_t *entry = q->head;
    while (entry) {
        wait_queue_entry_t *next = entry->next;
        task_unblock(entry->task);
        kfree(entry);
        entry = next;
    }
    q->head = NULL;
    q->tail = NULL;
    
    if (flags & (1 << 9)) __asm__ __volatile__("sti");
}
