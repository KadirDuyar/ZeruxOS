#include "stdio.h"
#include "syscall.h"
#include "dirent.h"

int main(int argc, char **argv) {
    const char *path = "/";
    
    if (argc > 1) {
        path = argv[1];
    }
    
    int fd = open(path, 0);
    if (fd < 0) {
        printf("ls: cannot open directory '%s'\n", path);
        return -1;
    }
    
    printf("\n--- [%s] ---\n", path);
    int index = 0;
    while (1) {
        struct dirent *ent = readdir(fd, index);
        if (!ent) break;
        if (ent->name[0] != '\0') {
            printf("  %s\n", ent->name);
        }
        index++;
    }
    printf("\n");
    
    close(fd);
    return 0;
}
