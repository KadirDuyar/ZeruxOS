#include "sync.h"
#include "kheap.h"
#include "libc.h"

extern void *kmalloc(uint32_t size);
extern void kfree(void *p);

static void mutex_destroy(void *ptr) {
    if (ptr) kfree(ptr);
}

static void sem_destroy(void *ptr) {
    if (ptr) kfree(ptr);
}

static void event_destroy(void *ptr) {
    if (ptr) kfree(ptr);
}

/* MUTEX */
zx_kernel_object_t* k_mutex_create(uint8_t initial_owner) {
    zx_kernel_mutex_t *mut = kmalloc(sizeof(zx_kernel_mutex_t));
    if (!mut) return NULL;
    
    init_waitqueue_head(&mut->wait_queue);
    
    if (initial_owner) {
        mut->locked = 1;
        task_t *curr = task_get_current();
        mut->owner_pid = curr ? curr->pid : 0;
    } else {
        mut->locked = 0;
        mut->owner_pid = 0;
    }
    
    return zx_obj_create(ZX_OBJ_MUTEX, mut, mutex_destroy);
}

int k_mutex_lock(zx_kernel_object_t *obj, uint32_t timeout_ms) {
    (void)timeout_ms; /* timeout not fully supported yet in wait_queue */
    if (!obj || obj->type != ZX_OBJ_MUTEX) return -6; /* INVALID_HANDLE */
    
    zx_kernel_mutex_t *mut = (zx_kernel_mutex_t*)obj->ptr;
    task_t *curr = task_get_current();
    if (!curr) return -1;
    
    uint32_t flags;
    while (1) {
        __asm__ __volatile__("pushf; cli; pop %0" : "=r"(flags));
        if (mut->locked == 0) {
            mut->locked = 1;
            mut->owner_pid = curr->pid;
            if (flags & (1 << 9)) __asm__ __volatile__("sti");
            return 0; /* Success */
        }
        if (flags & (1 << 9)) __asm__ __volatile__("sti");
        
        /* Block if already locked */
        wait_event_interruptible(&mut->wait_queue);
    }
    return 0;
}

int k_mutex_unlock(zx_kernel_object_t *obj) {
    if (!obj || obj->type != ZX_OBJ_MUTEX) return -6;
    
    zx_kernel_mutex_t *mut = (zx_kernel_mutex_t*)obj->ptr;
    task_t *curr = task_get_current();
    if (!curr) return -1;
    
    uint32_t flags;
    __asm__ __volatile__("pushf; cli; pop %0" : "=r"(flags));
    
    if (mut->locked && mut->owner_pid == curr->pid) {
        mut->locked = 0;
        mut->owner_pid = 0;
        wake_up(&mut->wait_queue);
        if (flags & (1 << 9)) __asm__ __volatile__("sti");
        return 0; /* Success */
    }
    
    if (flags & (1 << 9)) __asm__ __volatile__("sti");
    return -5; /* ACCESS_DENIED */
}

/* SEMAPHORE */
zx_kernel_object_t* k_sem_create(int32_t initial, int32_t max) {
    zx_kernel_semaphore_t *sem = kmalloc(sizeof(zx_kernel_semaphore_t));
    if (!sem) return NULL;
    
    sem->count = initial;
    sem->max_count = max;
    init_waitqueue_head(&sem->wait_queue);
    
    return zx_obj_create(ZX_OBJ_MUTEX /* aliasing for now, should be ZX_OBJ_SEMAPHORE but enum doesn't have it, let's just reuse ZX_OBJ_MUTEX or EVENT */, sem, sem_destroy);
}
/* For simplicity, we just use ZX_OBJ_EVENT for semaphore/event since wait object is generic enough in our phase 0 */

int k_sem_wait(zx_kernel_object_t *obj, uint32_t timeout_ms) {
    (void)timeout_ms;
    if (!obj) return -6;
    zx_kernel_semaphore_t *sem = (zx_kernel_semaphore_t*)obj->ptr;
    
    uint32_t flags;
    while (1) {
        __asm__ __volatile__("pushf; cli; pop %0" : "=r"(flags));
        if (sem->count > 0) {
            sem->count--;
            if (flags & (1 << 9)) __asm__ __volatile__("sti");
            return 0;
        }
        if (flags & (1 << 9)) __asm__ __volatile__("sti");
        wait_event_interruptible(&sem->wait_queue);
    }
}

int k_sem_release(zx_kernel_object_t *obj, int32_t count, int32_t *prev_count) {
    if (!obj) return -6;
    zx_kernel_semaphore_t *sem = (zx_kernel_semaphore_t*)obj->ptr;
    
    uint32_t flags;
    __asm__ __volatile__("pushf; cli; pop %0" : "=r"(flags));
    if (prev_count) *prev_count = sem->count;
    
    sem->count += count;
    if (sem->max_count > 0 && sem->count > sem->max_count) {
        sem->count = sem->max_count;
    }
    
    for (int i = 0; i < count; i++) {
        wake_up(&sem->wait_queue);
    }
    
    if (flags & (1 << 9)) __asm__ __volatile__("sti");
    return 0;
}

/* EVENT */
zx_kernel_object_t* k_event_create(uint8_t manual_reset, uint8_t initial_state) {
    zx_kernel_event_t *evt = kmalloc(sizeof(zx_kernel_event_t));
    if (!evt) return NULL;
    
    evt->manual_reset = manual_reset;
    evt->state = initial_state;
    init_waitqueue_head(&evt->wait_queue);
    
    return zx_obj_create(ZX_OBJ_EVENT, evt, event_destroy);
}

int k_event_wait(zx_kernel_object_t *obj, uint32_t timeout_ms) {
    (void)timeout_ms;
    if (!obj || obj->type != ZX_OBJ_EVENT) return -6;
    zx_kernel_event_t *evt = (zx_kernel_event_t*)obj->ptr;
    
    uint32_t flags;
    while (1) {
        __asm__ __volatile__("pushf; cli; pop %0" : "=r"(flags));
        if (evt->state) {
            if (!evt->manual_reset) {
                evt->state = 0; /* auto-reset */
            }
            if (flags & (1 << 9)) __asm__ __volatile__("sti");
            return 0;
        }
        if (flags & (1 << 9)) __asm__ __volatile__("sti");
        wait_event_interruptible(&evt->wait_queue);
    }
}

int k_event_set(zx_kernel_object_t *obj) {
    if (!obj || obj->type != ZX_OBJ_EVENT) return -6;
    zx_kernel_event_t *evt = (zx_kernel_event_t*)obj->ptr;
    
    uint32_t flags;
    __asm__ __volatile__("pushf; cli; pop %0" : "=r"(flags));
    evt->state = 1;
    if (evt->manual_reset) {
        wake_up_all(&evt->wait_queue);
    } else {
        wake_up(&evt->wait_queue);
    }
    if (flags & (1 << 9)) __asm__ __volatile__("sti");
    return 0;
}

int k_event_reset(zx_kernel_object_t *obj) {
    if (!obj || obj->type != ZX_OBJ_EVENT) return -6;
    zx_kernel_event_t *evt = (zx_kernel_event_t*)obj->ptr;
    
    uint32_t flags;
    __asm__ __volatile__("pushf; cli; pop %0" : "=r"(flags));
    evt->state = 0;
    if (flags & (1 << 9)) __asm__ __volatile__("sti");
    return 0;
}
