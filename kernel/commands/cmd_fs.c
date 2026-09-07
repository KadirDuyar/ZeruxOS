/* =============================================================================
 * ZeruX OS — Filesystem Shell Commands
 * File: kernel/commands/cmd_fs.c
 * =============================================================================
 */

#include "shell_cmd.h"
#include "vfs.h"
#include "userfs.h"
#include "fat32.h"
#include "pci.h"
#include "elf.h"
#include "task.h"
#include "serial.h"
#include "keyboard.h"
#include "vga.h"
#include "libc.h"

char g_cwd[256] = "/";

/* Join base path + child name into out (max 256 chars) */
void path_join(const char *base, const char *child, char *out, size_t out_sz) {
    size_t blen = kstrlen(base);
    int need_slash = (blen > 0 && base[blen - 1] != '/');
    size_t i = 0;
    while (i < out_sz - 1 && base[i]) { out[i] = base[i]; i++; }
    if (need_slash && i < out_sz - 1) { out[i++] = '/'; }
    size_t j = 0;
    while (i < out_sz - 1 && child[j]) { out[i++] = child[j++]; }
    out[i] = '\0';
}

/* Resolve a path: if starts with '/' it's absolute, else join with g_cwd */
void resolve_path(const char *input, char *out, size_t out_sz) {
    if (input[0] == '/') {
        size_t i = 0;
        while (i < out_sz - 1 && input[i]) { out[i] = input[i]; i++; }
        out[i] = '\0';
    } else {
        path_join(g_cwd, input, out, out_sz);
    }
}

int is_fat32_cwd(void) {
    return (kstrncmp(g_cwd, "/disk/fat0", 10) == 0);
}

int is_user_cwd(void) {
    return (kstrncmp(g_cwd, "/user", 5) == 0);
}

uint32_t fat32_cwd_cluster(void) {
    vfs_node_t *node = vfs_lookup(g_cwd);
    if (!node) return fat32_get_root_cluster();
    return node->inode;
}

int cmd_pwd(int argc, char **argv, char *out, uint32_t max) {
    (void)argc; (void)argv;
    cmd_printf(out, max, "%s\n", g_cwd);
    return 0;
}

int cmd_cd(int argc, char **argv, char *out, uint32_t max) {
    const char *target = (argc > 1) ? argv[1] : "/";

    if (kstrcmp(target, "/") == 0 || target[0] == '\0') {
        g_cwd[0] = '/'; g_cwd[1] = '\0';
        return 0;
    }

    if (kstrcmp(target, "..") == 0) {
        int len = (int)kstrlen(g_cwd);
        if (len <= 1) return 0; /* Already at root */
        if (g_cwd[len - 1] == '/') len--;
        while (len > 0 && g_cwd[len - 1] != '/') len--;
        if (len == 0) len = 1;
        g_cwd[len] = '\0';
        return 0;
    }

    char resolved[256];
    resolve_path(target, resolved, sizeof(resolved));

    int fd = vfs_open(resolved, 0);
    if (fd < 0) {
        cmd_printf(out, max, "cd: no such directory: '%s'\n", resolved);
        return -1;
    }
    vfs_close(fd);

    size_t i = 0;
    while (i < 255 && resolved[i]) { g_cwd[i] = resolved[i]; i++; }
    if (i > 1 && g_cwd[i - 1] == '/') i--;
    g_cwd[i] = '\0';
    return 0;
}

int cmd_ls(int argc, char **argv, char *out, uint32_t max) {
    char target[256];
    if (argc > 1 && argv[1][0] != '-') {
        resolve_path(argv[1], target, sizeof(target));
    } else {
        size_t ci = 0;
        while (ci < 255 && g_cwd[ci]) { target[ci] = g_cwd[ci]; ci++; }
        target[ci] = '\0';
    }

    int fd = vfs_open(target, 0);
    if (fd < 0) {
        cmd_printf(out, max, "ls: cannot access '%s': No such directory\n", target);
        return -1;
    }

    cmd_printf(out, max, "\n--- [%s] ---\n", target);
    uint32_t idx = 0;
    struct dirent *d = NULL;
    while ((d = vfs_readdir(fd, idx++)) != NULL) {
        cmd_printf(out, max, "  %s\n", d->name);
    }
    vfs_close(fd);
    cmd_printf(out, max, "\n");
    return 0;
}

int cmd_cat(int argc, char **argv, char *out, uint32_t max) {
    if (argc < 2) {
        cmd_printf(out, max, "Usage: cat <path>\n");
        return -1;
    }

    char resolved[256];
    resolve_path(argv[1], resolved, sizeof(resolved));

    int fd = vfs_open(resolved, 0);
    if (fd < 0) {
        cmd_printf(out, max, "cat: cannot open '%s': No such file\n", resolved);
        return -1;
    }

    char buf[512] = {0};
    int32_t bytes = vfs_read(fd, buf, sizeof(buf) - 1);
    vfs_close(fd);

    if (bytes >= 0) {
        cmd_printf(out, max, "\n--- [VFS CAT: %s] ---\n%s----------------------------------\n", resolved, buf);
        return 0;
    }
    cmd_printf(out, max, "cat: read error on '%s'\n", resolved);
    return -1;
}

int cmd_mkdir(int argc, char **argv, char *out, uint32_t max) {
    if (argc < 2) {
        cmd_printf(out, max, "Usage: mkdir <dirname>\n");
        return -1;
    }
    const char *name = argv[1];

    if (is_fat32_cwd()) {
        uint32_t dir_cluster = fat32_cwd_cluster();
        uint32_t res = fat32_mkdir(dir_cluster, name);
        if (res == 0) cmd_printf(out, max, "mkdir: failed (disk full or duplicate)\n");
        else          cmd_printf(out, max, "mkdir: created '%s' on disk\n", name);
    } else if (is_user_cwd()) {
        uint32_t parent = userfs_root_inode();
        if (kstrncmp(g_cwd, "/user/", 6) == 0) {
            int inode = userfs_find(0, g_cwd + 6);
            if (inode >= 0) parent = (uint32_t)inode;
        }
        int inode = userfs_mkdir(parent, name);
        if (inode < 0) cmd_printf(out, max, "mkdir: '%s' already exists\n", name);
        else           cmd_printf(out, max, "mkdir: created '%s'\n", name);
    } else {
        cmd_printf(out, max, "mkdir: not supported here (use /disk/fat0 or /user)\n");
        return -1;
    }
    return 0;
}

int cmd_touch(int argc, char **argv, char *out, uint32_t max) {
    if (argc < 2) {
        cmd_printf(out, max, "Usage: touch <filename>\n");
        return -1;
    }
    const char *name = argv[1];

    if (is_fat32_cwd()) {
        uint32_t dir_cluster = fat32_cwd_cluster();
        uint32_t res = fat32_create(dir_cluster, name);
        if (res == 0) cmd_printf(out, max, "touch: failed to create '%s' on disk\n", name);
        else          cmd_printf(out, max, "touch: created '%s' on disk\n", name);
    } else if (is_user_cwd()) {
        uint32_t parent = userfs_root_inode();
        if (kstrncmp(g_cwd, "/user/", 6) == 0) {
            int inode = userfs_find(0, g_cwd + 6);
            if (inode >= 0) parent = (uint32_t)inode;
        }
        int inode = userfs_create(parent, name);
        if (inode < 0) cmd_printf(out, max, "touch: failed to create '%s'\n", name);
        else           cmd_printf(out, max, "touch: created '%s'\n", name);
    } else {
        cmd_printf(out, max, "touch: not supported here (use /disk/fat0 or /user)\n");
        return -1;
    }
    return 0;
}

int cmd_rm(int argc, char **argv, char *out, uint32_t max) {
    if (argc < 2) {
        cmd_printf(out, max, "Usage: rm <filename>\n");
        return -1;
    }
    const char *name = argv[1];

    if (is_fat32_cwd()) {
        uint32_t dir_cluster = fat32_cwd_cluster();
        int res = fat32_unlink(dir_cluster, name);
        if (res < 0) cmd_printf(out, max, "rm: '%s': No such file or directory\n", name);
        else         cmd_printf(out, max, "rm: removed '%s' from disk\n", name);
    } else if (is_user_cwd()) {
        uint32_t parent = userfs_root_inode();
        if (kstrncmp(g_cwd, "/user/", 6) == 0) {
            int pinode = userfs_find(0, g_cwd + 6);
            if (pinode >= 0) parent = (uint32_t)pinode;
        }
        int inode = userfs_find(parent, name);
        if (inode < 0) cmd_printf(out, max, "rm: '%s': No such file or directory\n", name);
        else { userfs_rm((uint32_t)inode); cmd_printf(out, max, "rm: removed '%s'\n", name); }
    } else {
        cmd_printf(out, max, "rm: not supported here (use /disk/fat0 or /user)\n");
        return -1;
    }
    return 0;
}

int cmd_edit(int argc, char **argv, char *out, uint32_t max) {
    if (argc < 2) {
        cmd_printf(out, max, "Usage: edit <filename>\n");
        return -1;
    }
    const char *name = argv[1];

    if (!is_fat32_cwd() && !is_user_cwd()) {
        cmd_printf(out, max, "edit: not supported here (use /disk/fat0 or /user)\n");
        return -1;
    }

    char full_path[256];
    path_join(g_cwd, name, full_path, sizeof(full_path));

    if (is_fat32_cwd()) {
        uint32_t dir_cluster = fat32_cwd_cluster();
        fat32_create(dir_cluster, name);
    } else {
        uint32_t parent = userfs_root_inode();
        if (kstrncmp(g_cwd, "/user/", 6) == 0) {
            int pinode = userfs_find(0, g_cwd + 6);
            if (pinode >= 0) parent = (uint32_t)pinode;
        }
        int uinode = userfs_find(parent, name);
        if (uinode < 0) userfs_create(parent, name);
    }

    static char edit_buf[4096];
    uint32_t edit_pos = 0;
    for (uint32_t i = 0; i < 4096; i++) edit_buf[i] = 0;

    int vfs_fd = vfs_open(full_path, 0);
    if (vfs_fd >= 0) {
        edit_pos = vfs_read(vfs_fd, edit_buf, 4095);
        vfs_close(vfs_fd);
        edit_buf[edit_pos] = '\0';
    }

    serial_printf("--- edit: %s ---\n", name);
    serial_printf("Press 'q' to Quit (Discard). Press Ctrl+Q to Save & Exit.\n---\n");
    if (edit_pos > 0) serial_printf("%s", edit_buf);

    while (1) {
        char c = 0;
        if (keyboard_has_char()) c = keyboard_getchar();
        else if (serial_has_char()) c = serial_getchar();
        if (c == 0) { task_sleep_ms(10); continue; }

        if (c == 'q') {
            serial_printf("\n[EDIT] Aborted. Changes discarded.\n");
            return 0;
        }
        if (c == 0x11) break; /* Ctrl+Q -> Save */
        if (c == '\r') c = '\n';

        if (c == '\b' || c == 0x7F) {
            if (edit_pos > 0) {
                edit_pos--;
                edit_buf[edit_pos] = '\0';
                serial_putchar('\b'); serial_putchar(' '); serial_putchar('\b');
                vga_putchar('\b');
            }
        } else if ((c >= 32 && c <= 126) || c == '\n' || c == '\t') {
            if (edit_pos < 4095) {
                edit_buf[edit_pos++] = c;
                edit_buf[edit_pos] = '\0';
                if (c == '\n') serial_printf("\n");
                else { serial_putchar(c); vga_putchar(c); }
            }
        }
    }

    int wfd = vfs_open(full_path, 1);
    if (wfd >= 0) {
        vfs_write(wfd, edit_buf, edit_pos);
        vfs_close(wfd);
        cmd_printf(out, max, "\n[EDIT] '%s' saved (%u bytes).\n", name, edit_pos);
    } else {
        cmd_printf(out, max, "\n[EDIT] Failed to open '%s' for saving.\n", name);
    }
    return 0;
}

int cmd_lspci(int argc, char **argv, char *out, uint32_t max) {
    (void)argc; (void)argv; (void)out; (void)max;
    pci_dump_devices();
    return 0;
}

int cmd_run(int argc, char **argv, char *out, uint32_t max) {
    if (argc < 2) {
        cmd_printf(out, max, "Usage: run <path> (e.g. run /disk/fat0/BIN/HELLO.ELF)\n");
        return -1;
    }

    char resolved[256];
    resolve_path(argv[1], resolved, sizeof(resolved));

    int pid = elf32_load_and_exec(resolved, argc - 1, (const char**)&argv[1]);
    if (pid > 0) {
        cmd_printf(out, max, "[SHELL] Waiting for PID %d to exit...\n", pid);
        int exit_code = task_waitpid(pid);
        cmd_printf(out, max, "[SHELL] PID %d exited with code %d.\n", pid, exit_code);
        return exit_code;
    }
    cmd_printf(out, max, "run: failed to execute '%s'\n", resolved);
    return -1;
}
