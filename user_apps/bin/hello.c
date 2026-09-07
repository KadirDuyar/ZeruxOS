#include "stdio.h"
#include "syscall.h"

int main(int argc, char **argv) {
    printf("Hello from ZeruX User Space!\n");
    printf("Number of arguments: %d\n", argc);
    for (int i = 0; i < argc; i++) {
        printf("  argv[%d] = %s\n", i, argv[i]);
    }
    return 0;
}
