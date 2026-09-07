#include "object.h"
#include "kheap.h"
#include "libc.h" /* kprintf/kmalloc vs icin, mevcut libc.h yapi */

/* Not: Asagidaki kfree, kmalloc cagrilarini mevcut bellek 
 * yoneticisine uyarlayin (ZeruX'ta cogu kheap icinde).
 */
extern void *kmalloc(uint32_t size);
extern void kfree(void *p);

zx_kernel_object_t* zx_obj_create(zx_obj_type_t type, void *ptr, void (*destroy_cb)(void*)) {
    zx_kernel_object_t *obj = (zx_kernel_object_t*)kmalloc(sizeof(zx_kernel_object_t));
    if (!obj) return NULL;
    
    obj->type = type;
    obj->ref_count = 1;
    obj->ptr = ptr;
    obj->destroy = destroy_cb;
    return obj;
}

void zx_obj_ref(zx_kernel_object_t *obj) {
    if (obj) {
        obj->ref_count++;
    }
}

void zx_obj_deref(zx_kernel_object_t *obj) {
    if (obj && obj->ref_count > 0) {
        obj->ref_count--;
        if (obj->ref_count == 0) {
            if (obj->destroy && obj->ptr) {
                obj->destroy(obj->ptr);
            }
            kfree(obj);
        }
    }
}

void zx_handle_table_init(zx_handle_table_t *table) {
    if (!table) return;
    for (int i = 0; i < MAX_HANDLES_PER_PROCESS; i++) {
        table->objects[i] = NULL;
    }
}

uint32_t zx_handle_alloc(zx_handle_table_t *table, zx_kernel_object_t *obj) {
    if (!table || !obj) return 0xFFFFFFFF;

    /* Index 1'den baslar (0 Gecersizdir) */
    for (uint32_t i = 3; i < MAX_HANDLES_PER_PROCESS; i++) {
        if (table->objects[i] == NULL) {
            zx_obj_ref(obj);
            table->objects[i] = obj;
            return i;
        }
    }
    return 0xFFFFFFFF;
}

zx_kernel_object_t* zx_handle_get(zx_handle_table_t *table, uint32_t handle, zx_obj_type_t expected_type) {
    if (!table || handle == 0 || handle >= MAX_HANDLES_PER_PROCESS) return NULL;
    
    zx_kernel_object_t *obj = table->objects[handle];
    if (!obj) return NULL;
    
    if (expected_type != ZX_OBJ_UNKNOWN && obj->type != expected_type) {
        return NULL;
    }
    
    return obj;
}

int zx_handle_close(zx_handle_table_t *table, uint32_t handle) {
    if (!table || handle == 0 || handle >= MAX_HANDLES_PER_PROCESS) return -1;
    
    zx_kernel_object_t *obj = table->objects[handle];
    if (!obj) return -1;
    
    table->objects[handle] = NULL;
    zx_obj_deref(obj);
    return 0;
}

void zx_handle_table_destroy(zx_handle_table_t *table) {
    if (!table) return;
    for (int i = 1; i < MAX_HANDLES_PER_PROCESS; i++) {
        if (table->objects[i]) {
            zx_obj_deref(table->objects[i]);
            table->objects[i] = NULL;
        }
    }
}
