/* =============================================================================
 * ZeruX OS — First Ring 3 User Mode App (ELF Executable)
 * File: user_app/hello.c
 * =============================================================================
 */

/* Minimal System Call Stub for Write */
void sys_write(int fd, const char *str, int len) {
    __asm__ __volatile__(
        "int $0x80"
        :
        : "a"(1), "b"(fd), "c"(str), "d"(len)
        : "memory"
    );
}

int sys_open(const char *path, int mode) {
    int ret;
    __asm__ __volatile__(
        "int $0x80"
        : "=a"(ret)
        : "a"(6), "b"(path), "c"(mode)
        : "memory"
    );
    return ret;
}

int sys_read(int fd, char *buf, int len) {
    int ret;
    __asm__ __volatile__(
        "int $0x80"
        : "=a"(ret)
        : "a"(7), "b"(fd), "c"(buf), "d"(len)
        : "memory"
    );
    return ret;
}

void sys_close(int fd) {
    __asm__ __volatile__(
        "int $0x80"
        :
        : "a"(8), "b"(fd)
        : "memory"
    );
}

/* Minimal System Call Stub for Exit */
void sys_exit(int code) {
    __asm__ __volatile__(
        "int $0x80"
        :
        : "a"(5), "b"(code)
        : "memory"
    );
    while (1);
}

int sys_pipe(int fds[2]) {
    int ret;
    __asm__ __volatile__("int $0x80" : "=a"(ret) : "a"(9), "b"(fds) : "memory");
    return ret;
}

void* sys_sbrk(int increment) {
    void* ret;
    __asm__ __volatile__("int $0x80" : "=a"(ret) : "a"(10), "b"(increment) : "memory");
    return ret;
}

/* Entry point */
void _start(void) {
    char *msg = "\n\n*** HELLO FROM FAT32 ELF EXECUTABLE! ***\n"
                "This program was loaded from disk into isolated User Memory!\n\n";
    
    sys_write(1, msg, 107);

    /* Test SBRK (Dynamic Memory Allocation) */
    char *sbrk_msg = "[APP] Testing SBRK (Dynamic Memory)...\n";
    sys_write(1, sbrk_msg, 39);
    
    char *dyn_mem = (char*)sys_sbrk(4096); /* Request 4KB */
    if ((int)dyn_mem != -1) {
        /* Write to dynamic memory to ensure it's mapped and writable */
        char *test_str = "SBRK Memory Works!\n";
        for(int i=0; i<20; i++) dyn_mem[i] = test_str[i];
        sys_write(1, "[APP] Read from SBRK: ", 22);
        sys_write(1, dyn_mem, 19);
    } else {
        sys_write(1, "[APP] SBRK Failed!\n", 19);
    }

    /* Test VFS File Descriptors */
    char *open_msg = "\n[APP] Opening /disk/fat0/WELCOME.TXT...\n";
    sys_write(1, open_msg, 42);

    int fd = sys_open("/disk/fat0/WELCOME.TXT", 0);
    if (fd >= 0) {
        char buf[128];
        for(int i=0; i<128; i++) buf[i] = 0;

        int bytes = sys_read(fd, buf, 127);
        if (bytes > 0) {
            sys_write(1, "[APP] Read from file: ", 22);
            sys_write(1, buf, bytes);
            sys_write(1, "\n", 1);
        }
        sys_close(fd);
    }
    
    /* Test IPC Pipe */
    char *pipe_msg = "\n[APP] Testing IPC Pipe...\n";
    sys_write(1, pipe_msg, 27);
    
    int pipe_fds[2];
    if (sys_pipe(pipe_fds) == 0) {
        char *write_str = "Hello through IPC Pipe!";
        sys_write(pipe_fds[1], write_str, 24);
        
        char read_buf[32];
        for(int i=0; i<32; i++) read_buf[i] = 0;
        
        int bytes = sys_read(pipe_fds[0], read_buf, 31);
        if (bytes > 0) {
            sys_write(1, "[APP] Read from Pipe: ", 22);
            sys_write(1, read_buf, bytes);
            sys_write(1, "\n\n", 2);
        }
        sys_close(pipe_fds[0]);
        sys_close(pipe_fds[1]);
    }

    /* Test STDIN (Keyboard) */
    char *input_msg = "[APP] Enter your name: ";
    sys_write(1, input_msg, 23);
    
    char name_buf[64];
    for(int i=0; i<64; i++) name_buf[i] = 0;
    
    /* Read from stdin (FD 0) */
    int bytes = sys_read(0, name_buf, 63);
    
    sys_write(1, "\n[APP] Nice to meet you, ", 25);
    sys_write(1, name_buf, bytes);
    sys_write(1, "!\n\n", 3);

    sys_exit(0);
}
