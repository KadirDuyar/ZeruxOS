/* =============================================================================
 * ZeruX OS — IPC Pipe (Boru Hattı) Header
 * File: kernel/include/pipe.h
 * =============================================================================
 */

#ifndef PIPE_H
#define PIPE_H

#include <stdint.h>
#include "vfs.h"

#define PIPE_BUF_SIZE 4096

typedef struct {
    uint8_t buffer[PIPE_BUF_SIZE];
    uint32_t head;
    uint32_t tail;
    uint32_t size;
    vfs_node_t *read_node;
    vfs_node_t *write_node;
} pipe_t;

int32_t pipe_create(int32_t *fd_read, int32_t *fd_write);

#endif /* PIPE_H */
