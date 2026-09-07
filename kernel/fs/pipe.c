/* =============================================================================
 * ZeruX OS — IPC Pipe (Boru Hattı) Implementation
 * File: kernel/fs/pipe.c
 * =============================================================================
 */

#include "pipe.h"
#include "kheap.h"
#include "task.h"
#include "scheduler.h"

/* Dairesel tampondan okuma işlemi */
static int32_t pipe_vfs_read(struct vfs_node *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)offset;
    if (!node || !node->device_data || !buffer || size == 0) return 0;
    
    pipe_t *pipe = (pipe_t*)node->device_data;
    
    /* Eğer pipe boşsa, non-blocking olarak hemen 0 dön veya bloke ol (şimdilik non-blocking gibi ama basit sleep yapabiliriz) */
    /* Bloklayan senaryoda: veri gelene kadar sleep(1) yap */
    uint32_t bytes_read = 0;
    while (bytes_read < size) {
        if (pipe->size > 0) {
            buffer[bytes_read++] = pipe->buffer[pipe->tail];
            pipe->tail = (pipe->tail + 1) % PIPE_BUF_SIZE;
            pipe->size--;
        } else {
            /* Veri yok, daha fazla okuyamıyorsak çık */
            if (bytes_read > 0) break;
            
            /* Hiç veri okumadık, biraz bekle (blocking behavior) */
            task_sleep_ms(10);
            /* TODO: Pipe kapatıldıysa -1 dönmeli (yazar uç kapatıldıysa) */
        }
    }
    return bytes_read;
}

/* Dairesel tampona yazma işlemi */
static int32_t pipe_vfs_write(struct vfs_node *node, uint32_t offset, uint32_t size, const uint8_t *buffer) {
    (void)offset;
    if (!node || !node->device_data || !buffer || size == 0) return 0;
    
    pipe_t *pipe = (pipe_t*)node->device_data;
    
    uint32_t bytes_written = 0;
    while (bytes_written < size) {
        if (pipe->size < PIPE_BUF_SIZE) {
            pipe->buffer[pipe->head] = buffer[bytes_written++];
            pipe->head = (pipe->head + 1) % PIPE_BUF_SIZE;
            pipe->size++;
        } else {
            /* Pipe dolu, yer açılana kadar bekle */
            task_sleep_ms(10);
        }
    }
    return bytes_written;
}

static void pipe_vfs_close(struct vfs_node *node) {
    if (!node || !node->device_data) return;
    
    /* TODO: Gerçekte pipe'ın hem okuma hem yazma ucu kapandığında kfree(pipe) yapılmalı.
     * Şimdilik basit tutmak için memory leak göze alıyoruz veya ref_count ekleyebiliriz. */
}

static vfs_node_ops_t pipe_ops = {
    .read = pipe_vfs_read,
    .write = pipe_vfs_write,
    .open = NULL,
    .close = pipe_vfs_close,
    .readdir = NULL,
    .finddir = NULL
};

/* public API: Pipe yaratır ve iki Kernel FD döndürür */
int32_t pipe_create(int32_t *fd_read, int32_t *fd_write) {
    pipe_t *pipe = (pipe_t*)kzalloc(sizeof(pipe_t));
    if (!pipe) return -1;
    
    vfs_node_t *r_node = (vfs_node_t*)kzalloc(sizeof(vfs_node_t));
    vfs_node_t *w_node = (vfs_node_t*)kzalloc(sizeof(vfs_node_t));
    
    if (!r_node || !w_node) {
        if (pipe) kfree(pipe);
        if (r_node) kfree(r_node);
        if (w_node) kfree(w_node);
        return -1;
    }
    
    /* Read Node */
    r_node->flags = VFS_PIPE;
    r_node->ops = &pipe_ops;
    r_node->device_data = pipe;
    pipe->read_node = r_node;
    
    /* Write Node */
    w_node->flags = VFS_PIPE;
    w_node->ops = &pipe_ops;
    w_node->device_data = pipe;
    pipe->write_node = w_node;
    
    /* Kernel FD Tahsisi */
    int r_fd = vfs_alloc_fd(r_node, 0); /* Read flags */
    int w_fd = vfs_alloc_fd(w_node, 0); /* Write flags */
    
    if (r_fd < 0 || w_fd < 0) {
        /* Cleanup on failure */
        return -1;
    }
    
    *fd_read = r_fd;
    *fd_write = w_fd;
    return 0;
}
