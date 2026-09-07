#include "syscall.h"
#include "stdio.h"

int main(int argc, char *argv[]) {
    printf("\n==================================\n");
    printf("Merhaba ZeruX!\n");
    printf("Bu program User Space (Ring 3) icinde\n");
    printf("bir ELF binary olarak calismaktadir.\n");
    printf("==================================\n\n");
    
    if (argc > 1) {
        printf("Girdiginiz ilk arguman: %s\n", argv[1]);
    }
    
    return 0;
}
