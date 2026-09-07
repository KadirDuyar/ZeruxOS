/* =============================================================================
 * ZeruX OS — RAM User Filesystem Header
 * File: kernel/include/userfs.h
 * =============================================================================
 */

#ifndef USERFS_H
#define USERFS_H

#include <stdint.h>
#include "vfs.h"

void       userfs_init(void);
int        userfs_mkdir(uint32_t parent_inode, const char *name);
int        userfs_create(uint32_t parent_inode, const char *name);
int        userfs_rm(uint32_t inode);
int        userfs_find(uint32_t parent_inode, const char *name);
int        userfs_is_dir(uint32_t inode);
int        userfs_write_file(uint32_t inode, const uint8_t *data, uint32_t len);
vfs_node_t* userfs_get_vnode(uint32_t inode);
uint32_t   userfs_root_inode(void);

#endif /* USERFS_H */
