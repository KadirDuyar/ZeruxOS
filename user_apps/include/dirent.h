#ifndef _USER_DIRENT_H
#define _USER_DIRENT_H

#include <stdint.h>

#define VFS_MAX_NAME_LEN 128

struct dirent {
    char     name[VFS_MAX_NAME_LEN];
    uint32_t ino;
};

/* User space dir ops */
struct dirent *readdir(int fd, int index);

#endif
