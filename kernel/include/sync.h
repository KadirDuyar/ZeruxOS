#ifndef _KERNEL_SYNC_H
#define _KERNEL_SYNC_H

#include <stdint.h>
#include "task.h"
#include "wait.h"
#include "object.h"

typedef struct {
    uint32_t locked;
    uint32_t owner_pid;
    wait_queue_head_t wait_queue;
} zx_kernel_mutex_t;

typedef struct {
    int32_t count;
    int32_t max_count;
    wait_queue_head_t wait_queue;
} zx_kernel_semaphore_t;

typedef struct {
    uint8_t state;
    uint8_t manual_reset;
    wait_queue_head_t wait_queue;
} zx_kernel_event_t;

zx_kernel_object_t* k_mutex_create(uint8_t initial_owner);
int k_mutex_lock(zx_kernel_object_t *obj, uint32_t timeout_ms);
int k_mutex_unlock(zx_kernel_object_t *obj);

zx_kernel_object_t* k_sem_create(int32_t initial, int32_t max);
int k_sem_wait(zx_kernel_object_t *obj, uint32_t timeout_ms);
int k_sem_release(zx_kernel_object_t *obj, int32_t count, int32_t *prev_count);

zx_kernel_object_t* k_event_create(uint8_t manual_reset, uint8_t initial_state);
int k_event_wait(zx_kernel_object_t *obj, uint32_t timeout_ms);
int k_event_set(zx_kernel_object_t *obj);
int k_event_reset(zx_kernel_object_t *obj);

#endif
