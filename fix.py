import sys
import re

with open('kernel/arch/syscall.c', 'r', encoding='utf-8') as f:
    content = f.read()

# 1. Remove the first sys_write_handler definition which is around line 75
content = re.sub(r'/\* SYS_WRITE \(1\): fd, buf, len \*/\s*static int32_t sys_write_handler.*?return vfs_write\(kernel_fd, buf, len\);\s*\}', '', content, flags=re.DOTALL, count=1)

# 2. Fix sys_write_handler (the second one) to use serial_putchar or serial_printf
content = content.replace('serial_write(buf[i]);', 'serial_putchar(buf[i]);')
content = content.replace('vbe_terminal_putchar(buf[i]);', '')

# 3. Add sys_close_handle_handler prototype at the top
if 'int32_t sys_close_handle_handler(' not in content[:1000]:
    content = content.replace('static syscall_func_t syscall_table[MAX_SYSCALLS];', 'static syscall_func_t syscall_table[MAX_SYSCALLS];\nstatic int32_t sys_close_handle_handler(uint32_t arg1, uint32_t arg2, uint32_t arg3);')

# 4. Add the missing handlers before syscall_init
missing_handlers = \"\"\"
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
        ver[6] = '1'; ver[7] = '.'; ver[8] = '0'; ver[9] = '\\0';
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
\"\"\"

content = content.replace('void syscall_init(void) {', missing_handlers + '\\nvoid syscall_init(void) {')

with open('kernel/arch/syscall.c', 'w', encoding='utf-8') as f:
    f.write(content)

