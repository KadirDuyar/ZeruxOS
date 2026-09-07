#include "stdio.h"
#include "syscall.h"

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    
    /* VBE terminal clear screen escape code or just print newlines for now */
    printf("\033[2J\033[H");
    return 0;
}
