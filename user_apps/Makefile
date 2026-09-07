CC = gcc
LD = ld
AS = nasm

CFLAGS = -m32 -ffreestanding -O2 -Wall -Wextra -Iinclude -fno-pie -fno-pic -fno-stack-protector -fno-builtin
LDFLAGS = -m elf_i386 -T linker.ld -nostdlib
ASFLAGS = -f elf32

LIBC_OBJS = libc/crt0.o libc/syscalls.o libc/stdio.o libc/string.o

# ZXAPI Core Objects
API_SRCS = $(wildcard api/core/*.c) $(wildcard api/memory/*.c) $(wildcard api/process/*.c) $(wildcard api/ipc/*.c) $(wildcard api/io/*.c) $(wildcard api/gui/*.c) $(wildcard api/network/transport/*.c) $(wildcard api/system/*.c)
API_OBJS = $(API_SRCS:.c=.o)

SRCS := $(wildcard bin/*.c)
APPS := $(SRCS:.c=.elf)

all: $(APPS)
	@echo "Built $(words $(APPS)) user-space app(s): $(APPS)"

api/core/%.o: api/core/%.c
	$(CC) $(CFLAGS) -c $< -o $@

api/memory/%.o: api/memory/%.c
	$(CC) $(CFLAGS) -c $< -o $@
	$(CC) $(CFLAGS) -c $< -o $@

libc/%.o: libc/%.c
	$(CC) $(CFLAGS) -c $< -o $@

libc/%.o: libc/%.asm
	$(AS) $(ASFLAGS) $< -o $@

bin/%.o: bin/%.c
	$(CC) $(CFLAGS) -c $< -o $@

bin/%.elf: bin/%.o $(LIBC_OBJS) $(API_OBJS)
	$(LD) $(LDFLAGS) $(LIBC_OBJS) $(API_OBJS) $< -o $@

clean:
	rm -f libc/*.o bin/*.o bin/*.elf api/core/*.o api/memory/*.o api/process/*.o api/ipc/*.o api/io/*.o api/gui/*.o api/network/transport/*.o api/system/*.o
