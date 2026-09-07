#ifndef _KERNEL_OBJECT_H
#define _KERNEL_OBJECT_H

#include <stdint.h>
#include <stddef.h>

/* ZXAPI Object Manager (Phase 0)
 * Tum cekirdek objeleri bu yapi etrafinda sarmalanir. 
 * User-space sadece Handle id'lerini (1, 2, 3...) gorur.
 */

typedef enum {
    ZX_OBJ_UNKNOWN = 0,
    ZX_OBJ_FILE,         /* VFS dosya indexi / fd */
    ZX_OBJ_PROCESS,      /* task_t * */
    ZX_OBJ_THREAD,       /* task_t * */
    ZX_OBJ_MUTEX,        /* Mutex (ileride eklenecek) */
    ZX_OBJ_EVENT,        /* Kernel Event */
    ZX_OBJ_SHARED_MEM,   /* Paylasimli bellek pointer'i */
    ZX_OBJ_PIPE,         /* IPC Pipe */
    ZX_OBJ_SOCKET,       /* Ag soketi */
    ZX_OBJ_WINDOW        /* window_t * */
} zx_obj_type_t;

typedef struct {
    zx_obj_type_t type;
    uint32_t      ref_count;
    void         *ptr;
    void        (*destroy)(void *ptr); /* Objeyi temizleme fonksiyonu */
} zx_kernel_object_t;

#define MAX_HANDLES_PER_PROCESS 256

typedef struct {
    /* Handle ID'si dizin indeksidir. 
     * Ornegin objects[5] ise Handle = 5.
     * objects[0] reserve edilmistir (INVALID/NULL).
     */
    zx_kernel_object_t *objects[MAX_HANDLES_PER_PROCESS];
} zx_handle_table_t;

/* API */
zx_kernel_object_t* zx_obj_create(zx_obj_type_t type, void *ptr, void (*destroy_cb)(void*));
void zx_obj_ref(zx_kernel_object_t *obj);
void zx_obj_deref(zx_kernel_object_t *obj);

uint32_t zx_handle_alloc(zx_handle_table_t *table, zx_kernel_object_t *obj);
zx_kernel_object_t* zx_handle_get(zx_handle_table_t *table, uint32_t handle, zx_obj_type_t expected_type);
int zx_handle_close(zx_handle_table_t *table, uint32_t handle);

void zx_handle_table_init(zx_handle_table_t *table);
void zx_handle_table_destroy(zx_handle_table_t *table);

#endif /* _KERNEL_OBJECT_H */
