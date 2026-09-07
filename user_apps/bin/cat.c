#include "stdio.h"
#include "syscall.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: cat <filename>\n");
        return -1;
    }
    
    int fd = open(argv[1], 0);
    if (fd < 0) {
        printf("cat: %s: No such file or directory\n", argv[1]);
        return -1;
    }
    
    char buf[128];
    while (1) {
        int bytes = read(fd, buf, 127);
        if (bytes <= 0) break;
        buf[bytes] = '\0';
        printf("%s", buf);
    }
    
    close(fd);
    printf("\n");
    return 0;
}
