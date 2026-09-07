/* =============================================================================
 * ZeruX OS - Unified Wait Queues
 * File: kernel/include/wait.h
 * =============================================================================
 */

#ifndef WAIT_H
#define WAIT_H

#include <stdint.h>
#include <stddef.h>
#include "task.h"

/* Wait Queue Entry */
typedef struct wait_queue_entry {
    task_t *task;
    struct wait_queue_entry *next;
} wait_queue_entry_t;

/* Wait Queue Head */
typedef struct {
    wait_queue_entry_t *head;
    wait_queue_entry_t *tail;
} wait_queue_head_t;

#define INIT_WAIT_QUEUE_HEAD(name) { NULL, NULL }

/* Public API */
void init_waitqueue_head(wait_queue_head_t *q);
void wait_event_interruptible(wait_queue_head_t *q);
void wake_up(wait_queue_head_t *q);
void wake_up_all(wait_queue_head_t *q);

#endif /* WAIT_H */
