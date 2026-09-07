/* =============================================================================
 * ZeruX OS — System Call (int 0x80) Dispatcher & Core Handlers
 * File: kernel/arch/syscall.c
 * =============================================================================
 *
 * Ring 3 (User Mode) uygulamalarından gelen `int 0x80` kesmelerini karşılayan
 * ve EAX yazmacındaki sistem çağrı numarasına göre ilgili çekirdek fonksiyonuna
 * dallanan ana C dispatcher.
 * =============================================================================
 */

#include "syscall.h"
#include "idt.h"
#include "serial.h"
#include "vga.h"
#include "task.h"
#include "scheduler.h"
#include "vfs.h"
#include "pipe.h"
#include "keyboard.h"
#include "socket.h"
#include "serial.h"
#include "pmm.h"
#include "vmm.h"

extern void syscall_stub(void);

typedef int32_t (*syscall_func_t)(uint32_t arg1, uint32_t arg2, uint32_t arg3);
static int32_t sys_close_handle_handler(uint32_t arg1, uint32_t arg2, uint32_t arg3);
static syscall_func_t syscall_table[MAX_SYSCALLS];

/* =============================================================================
 * Security Validation Primitives (Ring 3 Pointer Checks)
 * =============================================================================
 */
static int is_user_range_valid(page_directory_t *pdir, uint32_t addr, uint32_t len) {
    if (!pdir || len == 0) return 0;
    if (addr < 0x08048000 || (uint64_t)addr + len > 0xC0000000) return 0;
    
    uint32_t start_page = addr & ~0xFFF;
    uint32_t end_page = (addr + len - 1) & ~0xFFF;
    
    for (uint32_t p = start_page; p <= end_page; p += 4096) {
        uint32_t pd_idx = p >> 22;
        uint32_t pt_idx = (p >> 12) & 0x3FF;
        
        pd_entry_t pde = pdir->entries[pd_idx];
        if (!(pde & VMM_FLAG_PRESENT)) return 0;
        
        page_table_t *pt = (page_table_t*)(pde & 0xFFFFF000);
        pt_entry_t pte = pt->entries[pt_idx];
        if (!(pte & VMM_FLAG_PRESENT) || !(pte & VMM_FLAG_USER)) return 0;
    }
    return 1;
}

static int strnlen_user(page_directory_t *pdir, const char *str, uint32_t max_len) {
    if (!str) return -1;
    uint32_t len = 0;
    while (len < max_len) {
        if (len == 0 || (((uint32_t)str + len) & 0xFFF) == 0) {
            if (!is_user_range_valid(pdir, (uint32_t)(str + len), 1)) return -1;
        }
        if (str[len] == '\0') return len;
        len++;
    }
    return -1;
}

/* =============================================================================
 * Core Syscall Implementations
 * =============================================================================
 */

/* SYS_WRITE (1): fd, buf, len */
/* SYS_GETPID (2): Return Current Task PID */
static int32_t sys_getpid_handler(uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    (void)arg1; (void)arg2; (void)arg3;
    task_t *curr = task_get_current();
    return curr ? (int32_t)curr->pid : -1;
}

/* SYS_YIELD (3): Voluntarily Relinquish CPU to Next Task */
static int32_t sys_yield_handler(uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    (void)arg1; (void)arg2; (void)arg3;
    schedule();
    return 0;
}

/* SYS_SLEEP (4): Non-Blocking Task Sleep Queue */
static int32_t sys_sleep_handler(uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    (void)arg2; (void)arg3;
    task_sleep_ms(arg1);
    return 0;
}

/* SYS_EXIT (5): Task Termination & Zombie Status */
static int32_t sys_exit_handler(uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    (void)arg2; (void)arg3;
    task_exit((int32_t)arg1);
    return 0;
}

/* SYS_OPEN (6): path, mode */
static int32_t sys_open_handler(uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    (void)arg3;
    const char *path = (const char*)arg1;
    int mode = (int)arg2;
    
    task_t *curr = task_get_current();
    if (!curr) return -1;

    if (strnlen_user(curr->page_directory, path, VFS_MAX_PATH_LEN) < 0) return -1; // -EFAULT

    int kernel_fd = vfs_open(path, mode);
    if (kernel_fd < 0) return -1;

    /* Create FILE object wrapping kernel_fd */
    zx_kernel_object_t *obj = zx_obj_create(ZX_OBJ_FILE, (void*)kernel_fd, NULL);
    if (!obj) { vfs_close(kernel_fd); return -1; }
    
    uint32_t handle = zx_handle_alloc(&curr->handle_table, obj);
    zx_obj_deref(obj);
    
    if (handle == 0xFFFFFFFF) { vfs_close(kernel_fd); return -1; }
    return (int32_t)handle;
}

/* SYS_READ (7): handle, buf, len */
static int32_t sys_read_handler(uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    uint32_t handle_id = arg1;
    char *buf = (char*)arg2;
    size_t len = (size_t)arg3;

    if (!buf || len == 0) return 0;
    task_t *curr = task_get_current();
    if (!curr) return -1;

    /* Legacy STDIN fallback */
    if (handle_id == 0) {
        return vfs_read(curr->fds[0], buf, len); /* Use old fd for now */
    }

    zx_kernel_object_t *obj = zx_handle_get(&curr->handle_table, handle_id, ZX_OBJ_FILE);
    if (!obj) return -1;

    int kernel_fd = (int)obj->ptr;
    if (kernel_fd < 0) return -1;

    if (!is_user_range_valid(curr->page_directory, (uint32_t)buf, len)) return -1;

    return vfs_read(kernel_fd, buf, len);
}

/* SYS_WRITE (1): handle, buf, len */
static int32_t sys_write_handler(uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    uint32_t handle_id = arg1;
    const char *buf = (const char*)arg2;
    size_t len = (size_t)arg3;

    if (!buf || len == 0) return 0;
    task_t *curr = task_get_current();
    if (!curr) return -1;
    
    if (!is_user_range_valid(curr->page_directory, (uint32_t)buf, len)) return -1;

    /* Legacy STDOUT/STDERR fallback */
    if (handle_id == 1 || handle_id == 2) {
        int kernel_fd = curr->fds[handle_id];
        if (kernel_fd == -11 || kernel_fd == -12) {
            for (size_t i = 0; i < len; i++) {
                serial_putchar(buf[i]);
                vga_putchar(buf[i]);
            }
            return len;
        }
        return vfs_write(kernel_fd, buf, len);
    }

    zx_kernel_object_t *obj = zx_handle_get(&curr->handle_table, handle_id, ZX_OBJ_FILE);
    if (!obj) return -1;
    
    int kernel_fd = (int)obj->ptr;
    if (kernel_fd < 0) return -1;

    return vfs_write(kernel_fd, buf, len);
}

/* SYS_CLOSE (8): handle */
static int32_t sys_close_handler(uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    (void)arg2; (void)arg3;
    uint32_t handle_id = arg1;
    task_t *curr = task_get_current();
    if (!curr) return -1;

    /* Disallow closing 0, 1, 2 directly through this unless we map them. */
    if (handle_id < 3) return -1;
    
    /* We can actually use sys_close_handle_handler logic directly here.
       But let's also close the vfs file. */
    zx_kernel_object_t *obj = zx_handle_get(&curr->handle_table, handle_id, ZX_OBJ_FILE);
    if (obj) {
        int kernel_fd = (int)obj->ptr;
        if (kernel_fd >= 0) vfs_close(kernel_fd);
        /* Mark obj->ptr as invalid so it's not closed twice if multiple refs exist */
        obj->ptr = (void*)-1; 
    }

    return sys_close_handle_handler(arg1, 0, 0);
}

/* SYS_PIPE (9): fds[2] */
static int32_t sys_pipe_handler(uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    (void)arg2; (void)arg3;
    int *fds = (int*)arg1;
    if (!fds) return -1;
    
    task_t *curr = task_get_current();
    if (!curr) return -1;
    
    if (!is_user_range_valid(curr->page_directory, (uint32_t)fds, 2 * sizeof(int))) return -1; // -EFAULT
    
    /* İki boş Task FD bul */
    int tfd0 = -1, tfd1 = -1;
    for (int i = 0; i < TASK_MAX_FDS; i++) {
        if (curr->fds[i] == -1) {
            if (tfd0 == -1) tfd0 = i;
            else if (tfd1 == -1) { tfd1 = i; break; }
        }
    }
    
    if (tfd0 == -1 || tfd1 == -1) return -1;
    
    /* VFS/Pipe Kernel FDs yarat */
    int kfd_read, kfd_write;
    if (pipe_create(&kfd_read, &kfd_write) < 0) return -1;
    
    curr->fds[tfd0] = kfd_read;
    curr->fds[tfd1] = kfd_write;
    
    fds[0] = tfd0;
    fds[1] = tfd1;
    
    return 0;
}

/* =============================================================================
 * Socket Syscalls (Phase 2 Networking)
 * =============================================================================
 */
static int32_t sys_socket_handler(uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    task_t *curr = task_get_current();
    if (!curr) return -1;
    
    int task_fd = -1;
    for (int i = 0; i < TASK_MAX_FDS; i++) {
        if (curr->fds[i] == -1) {
            task_fd = i;
            break;
        }
    }
    if (task_fd == -1) return -1;
    
    int kernel_fd = socket((int)arg1, (int)arg2, (int)arg3);
    if (kernel_fd < 0) return -1;
    
    curr->fds[task_fd] = kernel_fd;
    return task_fd;
}

static int32_t sys_bind_handler(uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    task_t *curr = task_get_current();
    if (!curr || arg1 >= TASK_MAX_FDS || curr->fds[arg1] < 0) return -1;
    if (!is_user_range_valid(curr->page_directory, arg2, arg3)) return -1;
    return bind(curr->fds[arg1], (const struct sockaddr *)arg2, arg3);
}

static int32_t sys_listen_handler(uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    (void)arg3;
    task_t *curr = task_get_current();
    if (!curr || arg1 >= TASK_MAX_FDS || curr->fds[arg1] < 0) return -1;
    return listen(curr->fds[arg1], (int)arg2);
}

static int32_t sys_accept_handler(uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    task_t *curr = task_get_current();
    if (!curr || arg1 >= TASK_MAX_FDS || curr->fds[arg1] < 0) return -1;
    
    if (arg2 && !is_user_range_valid(curr->page_directory, arg2, sizeof(struct sockaddr))) return -1;
    if (arg3 && !is_user_range_valid(curr->page_directory, arg3, sizeof(uint32_t))) return -1;
    
    int task_fd = -1;
    for (int i = 0; i < TASK_MAX_FDS; i++) {
        if (curr->fds[i] == -1) {
            task_fd = i;
            break;
        }
    }
    if (task_fd == -1) return -1;
    
    int kernel_fd = accept(curr->fds[arg1], (struct sockaddr *)arg2, (uint32_t *)arg3);
    if (kernel_fd < 0) return -1;
    
    curr->fds[task_fd] = kernel_fd;
    return task_fd;
}

static int32_t sys_connect_handler(uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    task_t *curr = task_get_current();
    if (!curr || arg1 >= TASK_MAX_FDS || curr->fds[arg1] < 0) return -1;
    if (!is_user_range_valid(curr->page_directory, arg2, arg3)) return -1;
    return connect(curr->fds[arg1], (const struct sockaddr *)arg2, arg3);
}

/* SYS_SPAWN (6): path, argv */
#include "elf.h"
static int32_t sys_spawn_handler(uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    (void)arg3;
    const char *path = (const char*)arg1;
    const char **argv = (const char**)arg2;
    
    task_t *curr = task_get_current();
    if (curr && strnlen_user(curr->page_directory, path, VFS_MAX_PATH_LEN) < 0) return -1;
    
    int argc = 0;
    if (argv) {
        if (curr) {
            while (1) {
                if (!is_user_range_valid(curr->page_directory, (uint32_t)&argv[argc], sizeof(char*))) return -1;
                if (argv[argc] == NULL) break;
                if (strnlen_user(curr->page_directory, argv[argc], 1024) < 0) return -1;
                argc++;
                if (argc >= 128) return -1; /* Sanity limit */
            }
        } else {
            while (argv[argc] != NULL) argc++;
        }
    }
    
    return elf32_load_and_exec(path, argc, argv);
}

/* SYS_READDIR (11): fd, index */
#include "vfs.h"
static int32_t sys_readdir_handler(uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    int fd = (int)arg1;
    uint32_t index = (uint32_t)arg2;
    struct dirent *user_dir = (struct dirent*)arg3;
    
    task_t *curr = task_get_current();
    if (!curr || fd < 0 || fd >= TASK_MAX_FDS || curr->fds[fd] < 0) return -1;
    
    if (!is_user_range_valid(curr->page_directory, (uint32_t)user_dir, sizeof(struct dirent))) return -1;
    
    int global_fd = curr->fds[fd];
    vfs_node_t *node = vfs_get_node(global_fd);
    if (!node || (node->flags & VFS_DIRECTORY) == 0) return -1;
    
    struct dirent *dir = vfs_readdir(global_fd, index);
    if (!dir) return -1;
    
    /* Copy the kernel dirent to the user-provided buffer safely */
    *user_dir = *dir;
    return 1; /* Success */
}

/* SYS_WAITPID (11): pid */
static int32_t sys_waitpid_handler(uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    (void)arg2; (void)arg3;
    return task_waitpid((uint32_t)arg1);
}

/* SYS_SBRK (10): increment */

/* SYS_VIRTUAL_ALLOC (21): (addr, size, alloc_type, protect) */
static int32_t sys_virtual_alloc_handler(uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    uint32_t req_addr = arg1;
    uint32_t size = arg2;
    (void)arg3; /* alloc_type, ignoring for now as we always commit */
    

    task_t *curr = task_get_current();
    if (!curr) return -1;
    
    if (size == 0) return -87; /* ZX_ERROR_INVALID_PARAMETER */

    uint32_t num_pages = (size + 0xFFF) / 4096;
    
    /* If addr is NULL, find free space at heap_end */
    uint32_t start_addr = req_addr;
    if (start_addr == 0) {
        start_addr = (curr->heap_end + 0xFFF) & ~0xFFF;
        curr->heap_end = start_addr + (num_pages * 4096);
    } else {
        start_addr = start_addr & ~0xFFF; /* page align */
    }

    /* Max alloc limit 256MB per process check here if needed */
    
    for (uint32_t i = 0; i < num_pages; i++) {
        uint32_t vaddr = start_addr + (i * 4096);
        void *phys = pmm_alloc_block();
        if (!phys) return -8; /* ZX_ERROR_NO_MEMORY */
        vmm_map_page(curr->page_directory, vaddr, (uint32_t)phys, VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_USER);
        
        uint8_t *p = (uint8_t*)phys;
        for (int j = 0; j < 4096; j++) p[j] = 0;
    }
    
    return (int32_t)start_addr;
}

/* SYS_VIRTUAL_FREE (22): (addr, size, free_type) */
static int32_t sys_virtual_free_handler(uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    uint32_t addr = arg1;
    uint32_t size = arg2;
    (void)arg3; /* free_type */

    task_t *curr = task_get_current();
    if (!curr) return -1;
    
    if (addr == 0 || size == 0) return -87;
    
    uint32_t start_addr = addr & ~0xFFF;
    uint32_t num_pages = (size + 0xFFF) / 4096;
    
    for (uint32_t i = 0; i < num_pages; i++) {
        uint32_t vaddr = start_addr + (i * 4096);
        /* Idealy we should get physical address and pmm_free_block() it */
        /* But vmm_unmap_page currently might not free physical.
           Let's assume vmm_unmap_page handles it or leaves it leaked for now 
           since pmm_free_block needs the phys addr. */
        vmm_unmap_page(curr->page_directory, vaddr);
    }
    
    return 0; /* ZX_SUCCESS */
}

/* SYS_VIRTUAL_PROTECT (23): (addr, size, new_protect, old_protect_ptr) */

/* SYS_THREAD (25): Thread API Backend */
static int32_t sys_thread_handler(uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    (void)arg2; (void)arg3;
    uint32_t action = arg1;
    task_t *curr = task_get_current();
    if (!curr) return -1;

    if (action == 0) { /* create: arg2=start_addr, arg3=parameter (creation_flags ignored for now) */
        /* Gercek kthread_create sonrasi objeye sarip handle donulmeli. Simdilik Stub. */
        zx_kernel_object_t *obj = zx_obj_create(ZX_OBJ_THREAD, NULL, NULL);
        if(!obj) return -8; /* ZX_ERROR_NO_MEMORY */
        uint32_t handle = zx_handle_alloc(&curr->handle_table, obj);
        zx_obj_deref(obj);
        return (int32_t)handle;
    } 
    /* exit, suspend, resume vb eklenecek */
    return 0;
}

/* SYS_SYNC (26): Mutex / Semaphore / Wait Backend */
static int32_t sys_sync_handler(uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    uint32_t action = arg1;
    task_t *curr = task_get_current();
    if (!curr) return -1;

    extern zx_kernel_object_t* k_mutex_create(uint8_t);
    extern int k_mutex_lock(zx_kernel_object_t*, uint32_t);
    extern int k_mutex_unlock(zx_kernel_object_t*);
    
    extern zx_kernel_object_t* k_sem_create(int32_t, int32_t);
    extern int k_sem_wait(zx_kernel_object_t*, uint32_t);
    extern int k_sem_release(zx_kernel_object_t*, int32_t, int32_t*);

    extern zx_kernel_object_t* k_event_create(uint8_t, uint8_t);
    extern int k_event_wait(zx_kernel_object_t*, uint32_t);

    if (action == 0) { /* CREATE_MUTEX: arg2=initial_owner, arg3=name */
        zx_kernel_object_t *obj = k_mutex_create(arg2);
        if(!obj) return -8;
        uint32_t handle = zx_handle_alloc(&curr->handle_table, obj);
        zx_obj_deref(obj);
        return (int32_t)handle;
    }
    if (action == 1) { /* RELEASE_MUTEX: arg2=handle */
        zx_kernel_object_t *obj = zx_handle_get(&curr->handle_table, arg2, ZX_OBJ_MUTEX);
        if(!obj) return -6;
        return k_mutex_unlock(obj);
    }
    if (action == 2) { /* CREATE_SEMAPHORE: arg2=initial, arg3=max (actually we need a struct or ignore max for now in syscall to fit 3 args) */
        /* Temporary: treat arg2 as initial, ignore max or encode it */
        zx_kernel_object_t *obj = k_sem_create(arg2, arg2);
        if(!obj) return -8;
        uint32_t handle = zx_handle_alloc(&curr->handle_table, obj);
        zx_obj_deref(obj);
        return (int32_t)handle;
    }
    if (action == 3) { /* RELEASE_SEMAPHORE: arg2=handle, arg3=release_count */
        /* Eventual pointer handling for prev_count omitted */
        zx_kernel_object_t *obj = zx_handle_get(&curr->handle_table, arg2, ZX_OBJ_UNKNOWN); /* Using unknown because of enum reuse */
        if(!obj) return -6;
        return k_sem_release(obj, arg3, NULL);
    }
    if (action == 4) { /* WAIT_SINGLE_OBJECT: arg2=handle, arg3=ms */
        zx_kernel_object_t *obj = zx_handle_get(&curr->handle_table, arg2, ZX_OBJ_UNKNOWN);
        if(!obj) return -6; /* ZX_ERROR_INVALID_HANDLE */
        
        if (obj->type == ZX_OBJ_MUTEX) return k_mutex_lock(obj, arg3);
        if (obj->type == ZX_OBJ_EVENT) return k_event_wait(obj, arg3);
        /* If sem, also EVENT in our stub enum */
        return k_sem_wait(obj, arg3);
    }
    return 0;
}

/* SYS_IPC (27): SharedMem / Pipe / Event Backend */
static int32_t sys_ipc_handler(uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    uint32_t action = arg1;
    task_t *curr = task_get_current();
    if (!curr) return -1;

    extern zx_kernel_object_t* k_event_create(uint8_t, uint8_t);
    extern int k_event_set(zx_kernel_object_t*);
    extern int k_event_reset(zx_kernel_object_t*);

    if (action == 0) { /* SHM_CREATE: arg2=name, arg3=size */
        zx_kernel_object_t *obj = zx_obj_create(ZX_OBJ_SHARED_MEM, NULL, NULL);
        if(!obj) return -8;
        uint32_t handle = zx_handle_alloc(&curr->handle_table, obj);
        zx_obj_deref(obj);
        return (int32_t)handle;
    }
    if (action == 3) { /* PIPE_CREATE: arg2=name */
        zx_kernel_object_t *obj = zx_obj_create(ZX_OBJ_PIPE, NULL, NULL);
        if(!obj) return -8;
        uint32_t handle = zx_handle_alloc(&curr->handle_table, obj);
        zx_obj_deref(obj);
        return (int32_t)handle;
    }
    if (action == 7) { /* EVENT_CREATE: arg2=manual_reset, arg3=initial_state */
        zx_kernel_object_t *obj = k_event_create(arg2, arg3);
        if(!obj) return -8;
        uint32_t handle = zx_handle_alloc(&curr->handle_table, obj);
        zx_obj_deref(obj);
        return (int32_t)handle;
    }
    if (action == 8) { /* EVENT_SET: arg2=handle */
        zx_kernel_object_t *obj = zx_handle_get(&curr->handle_table, arg2, ZX_OBJ_EVENT);
        if(!obj) return -6;
        return k_event_set(obj);
    }
    if (action == 9) { /* EVENT_RESET: arg2=handle */
        zx_kernel_object_t *obj = zx_handle_get(&curr->handle_table, arg2, ZX_OBJ_EVENT);
        if(!obj) return -6;
        return k_event_reset(obj);
    }
    return 0; /* Stub */
}

static int32_t sys_virtual_protect_handler(uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    (void)arg1; (void)arg2; (void)arg3;
    /* Not fully implemented yet. Returns success. */
    return 0;
}

static int32_t sys_sbrk_handler(uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    (void)arg2; (void)arg3;
    int increment = (int)arg1;
    task_t *curr = task_get_current();
    if (!curr) return -1;
    
    uint32_t old_end = curr->heap_end;
    if (increment == 0) return old_end;
    
    uint32_t new_end = old_end + increment;
    
    /* Enforce 128 MB max heap limit to prevent memory exhaustion */
    if (new_end > 0x10000000) return -1;
    
    uint32_t old_page_end = (old_end + 0xFFF) & ~0xFFF;
    uint32_t new_page_end = (new_end + 0xFFF) & ~0xFFF;
    
    if (new_page_end > old_page_end) {
        for (uint32_t addr = old_page_end; addr < new_page_end; addr += 4096) {
            void *phys = pmm_alloc_block();
            if (!phys) return -1; /* Out of physical memory */
            
            vmm_map_page(curr->page_directory, addr, (uint32_t)phys, VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_USER);
            
            /* Zero the newly mapped physical page */
            uint8_t *p = (uint8_t*)phys;
            for (int i = 0; i < 4096; i++) p[i] = 0;
        }
    }
    
    curr->heap_end = new_end;
    return old_end;
}

/* SYS_CREAT (19): /disk/fat0 kök dizininde yeni dosya oluştur
 * arg1 = kullanıcı adres alanındaki tam yol string'i (ör: "/disk/fat0/DOSYA.TXT")
 * Dönüş: 0 başarı, -1 hata
 */
#include "fat32.h"

/* SYS_CLOSE_HANDLE (20): Phase 0 Object Manager */
static int32_t sys_close_handle_handler(uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    (void)arg2; (void)arg3;
    uint32_t handle = arg1;
    task_t *curr = task_get_current();
    if (!curr) return -1; /* -ZX_ERROR_ACCESS_DENIED */
    
    extern int zx_handle_close(zx_handle_table_t *table, uint32_t handle);
    int res = zx_handle_close(&curr->handle_table, handle);
    if (res < 0) {
        return -6; /* -ZX_ERROR_INVALID_HANDLE */
    }
    return 0; /* ZX_SUCCESS */
}
static int32_t sys_creat_handler(uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    (void)arg2; (void)arg3;
    const char *user_path = (const char *)arg1;

    task_t *curr = task_get_current();
    if (!curr) return -1;
    if (strnlen_user(curr->page_directory, user_path, VFS_MAX_PATH_LEN) < 0) return -1;

    /* Sadece /disk/fat0/ önekiyle gelen dosya adlarına izin ver */
    const char *prefix = "/disk/fat0/";
    int pi = 0;
    while (prefix[pi] && user_path[pi] == prefix[pi]) pi++;
    if (prefix[pi] != '\0') return -1;   /* Önek eşleşmedi */

    const char *fname = user_path + pi;  /* "DOSYA.TXT" gibi kısa ad */

    /* Alt dizin girişimi yasak (güvenlik) */
    for (const char *p = fname; *p; p++) {
        if (*p == '/') return -1;
    }
    if (fname[0] == '\0') return -1;

    uint32_t root_cluster = fat32_get_root_cluster();
    if (root_cluster == 0) return -1;

    /* Dosya zaten varsa da 0 döner (idempotent) */
    uint32_t clust = fat32_create(root_cluster, fname);
    return (clust != 0) ? 0 : -1;
}

/* =============================================================================
 * syscall_init() — IDT 0x80 Kapısını DPL=3 Olarak Kaydet ve Tabloyu Doldur
 * =============================================================================
 */
/* SYS_DEVICE_EXT (28): Device IoControl Backend */
static int32_t sys_device_ext_handler(uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    uint32_t handle_id = arg1;
    uint32_t control_code = arg2;
    uint32_t args_ptr = arg3;
    task_t *curr = task_get_current();
    if (!curr) return -1;
    (void)handle_id; (void)control_code; (void)args_ptr;
    return 0;
}

/* SYS_GUI (29): Window, Message ve Graphics Backend */
static int32_t sys_gui_handler(uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    uint32_t action = arg1;
    task_t *curr = task_get_current();
    if (!curr) return -1;

    extern int32_t wm_create_window(const char*, int32_t, int32_t, int32_t, int32_t);
    extern void* wm_get_window(int32_t);

    if (action == 0) { 
        uint32_t *cs = (uint32_t*)arg2;
        if(!cs) return -87; 
        const char *title = (const char*)cs[2]; 
        int32_t x = (int32_t)cs[4];
        int32_t y = (int32_t)cs[5];
        int32_t w = (int32_t)cs[6];
        int32_t h = (int32_t)cs[7];

        int32_t win_id = wm_create_window(title ? title : "Window", x, y, w, h);
        if (win_id < 0) return -8; 
        
        void *win_ptr = wm_get_window(win_id);
        zx_kernel_object_t *obj = zx_obj_create(ZX_OBJ_WINDOW, win_ptr, NULL);
        if(!obj) return -8;
        uint32_t handle = zx_handle_alloc(&curr->handle_table, obj);
        zx_obj_deref(obj);
        return (int32_t)handle;
    }
    if (action == 3) { return 0; }
    if (action == 7) { return (int32_t)arg2; }
    return 0;
}

/* SYS_TIME (30): Time and RTC Backend */
static int32_t sys_time_handler(uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    (void)arg3;
    if (arg1 == 0) { 
        extern uint32_t timer_get_ticks(void); 
        return timer_get_ticks();
    }
    if (arg1 == 1) { 
        if (!arg2) return -87;
        uint16_t *t = (uint16_t*)arg2;
        t[0] = 2026; t[1] = 9; t[2] = 5; 
        return 0;
    }
    return -1;
}

/* SYS_INFO (31): System Information Backend */
static int32_t sys_info_handler(uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    (void)arg3;
    if (arg1 == 0) {
        if (!arg2) return -87;
        uint32_t *info = (uint32_t*)arg2;
        char *ver = (char*)info;
        ver[0] = 'Z'; ver[1] = 'e'; ver[2] = 'r'; ver[3] = 'u'; ver[4] = 'X'; ver[5] = ' '; 
        ver[6] = '1'; ver[7] = '.'; ver[8] = '0'; ver[9] = '\0';
        info[8] = 128 * 1024 * 1024; 
        info[9] = 64 * 1024 * 1024;  
        extern uint32_t timer_get_ticks(void);
        info[10] = timer_get_ticks() / 1000; 
        return 0;
    }
    return -1;
}

/* SYS_ENV (32): Environment Backend */
static int32_t sys_env_handler(uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    (void)arg1; (void)arg2; (void)arg3;
    return -1;
}

void syscall_init(void) {
    for (int i = 0; i < MAX_SYSCALLS; i++) {
        syscall_table[i] = NULL;
    }

    /* Syscall handler'larını kaydet */
    syscall_table[SYS_WRITE]  = sys_write_handler;
    syscall_table[SYS_GETPID] = sys_getpid_handler;
    syscall_table[SYS_YIELD]  = sys_yield_handler;
    syscall_table[SYS_SLEEP]  = sys_sleep_handler;
    syscall_table[SYS_EXIT]   = sys_exit_handler;
    syscall_table[SYS_SPAWN]  = sys_spawn_handler;
    syscall_table[SYS_WAITPID]= sys_waitpid_handler;
    syscall_table[SYS_OPEN]   = sys_open_handler;
    syscall_table[SYS_READ]   = sys_read_handler;
    syscall_table[SYS_CLOSE]  = sys_close_handler;
    syscall_table[SYS_READDIR]= sys_readdir_handler;
    syscall_table[SYS_PIPE]   = sys_pipe_handler;
    syscall_table[SYS_SBRK]   = sys_sbrk_handler;
    
    // Move sockets up by 2 indices (14 to 18)
    syscall_table[14]         = sys_socket_handler;
    syscall_table[15]         = sys_bind_handler;
    syscall_table[16]         = sys_listen_handler;
    syscall_table[17]         = sys_accept_handler;
    syscall_table[18]         = sys_connect_handler;
    syscall_table[19]         = sys_creat_handler;
    syscall_table[20]         = sys_close_handle_handler;
    syscall_table[21]         = sys_virtual_alloc_handler;
    syscall_table[22]         = sys_virtual_free_handler;
    syscall_table[23]         = sys_virtual_protect_handler;
    syscall_table[25]         = sys_thread_handler;
    syscall_table[26]         = sys_sync_handler;
    syscall_table[27]         = sys_ipc_handler;
    syscall_table[28]         = sys_device_ext_handler;
    syscall_table[29]         = sys_gui_handler;
    syscall_table[30]         = sys_time_handler;
    syscall_table[31]         = sys_info_handler;
    syscall_table[32]         = sys_env_handler;

    /* IDT Gate 128 (0x80) yükle — KRİTİK: DPL=3 (IDT_FLAGS_USER_INT = 0xEE) */
    idt_set_gate(0x80, (uint32_t)syscall_stub, 0x08, IDT_FLAGS_USER_INT);

    serial_printf("===========================================\n");
    serial_printf(" ZeruX OS — System Call Engine (int 0x80)\n");
    serial_printf("===========================================\n");
    serial_printf("[SYSCALL] Registered IDT Gate 0x80 (128) with DPL=3 (User Accessible).\n");
    serial_printf("[SYSCALL] Registered handlers: SYS_WRITE(1), SYS_GETPID(2), SYS_YIELD(3), SYS_SLEEP(4), SYS_EXIT(5)\n");
    serial_printf("===========================================\n\n");
}

/* =============================================================================
 * syscall_handler_c() — Assembly Stub Tarafından Çağrılan Ana C Dispatcher
 * =============================================================================
 */
int32_t syscall_handler_c(registers_t *regs) {
    if (!regs) return -1;

    uint32_t syscall_num = regs->eax;
    uint32_t arg1 = regs->ebx;
    uint32_t arg2 = regs->ecx;
    uint32_t arg3 = regs->edx;

    if (syscall_num == 0 || syscall_num >= MAX_SYSCALLS || syscall_table[syscall_num] == NULL) {
        serial_printf("[SYSCALL ERROR] Invalid or unhandled syscall number %u (Task PID %u)!\n",
                      syscall_num, sys_getpid_handler(0,0,0));
        regs->eax = (uint32_t)-1;
        return -1;
    }

    int32_t ret = syscall_table[syscall_num](arg1, arg2, arg3);

    /* Dönen değeri EAX yazmacına yaz — Assembly `popa` ile User Mode'a aktarılır */
    regs->eax = (uint32_t)ret;
    return ret;
}




