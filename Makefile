CC := gcc
LD := ld
OBJCOPY := objcopy
AS := nasm

CFLAGS := -ffreestanding -O2 -Wall -Wextra -Wunused-function -m32 -nostdlib -fno-stack-protector -fno-builtin -fno-pic -fno-pie -Iinclude -Ikernel/include
LDFLAGS := -T kernel/linker.ld -m elf_i386 --no-undefined
ASFLAGS := -f bin

BUILD_DIR := build
BOOT_SRC := bootloader/boot.asm
BOOT2_SRC := bootloader/boot2.asm
BOOT_BIN := $(BUILD_DIR)/boot.bin
BOOT2_BIN := $(BUILD_DIR)/boot2.bin
LINKER_LD := kernel/linker.ld
DISK_IMAGE := zerux.img

INFLATE_OBJ := $(BUILD_DIR)/inflate.o
PNG_OBJ     := $(BUILD_DIR)/png.o

ALL_OBJS := \
	$(INFLATE_OBJ) $(PNG_OBJ) \
	$(BUILD_DIR)/kernel.o $(BUILD_DIR)/serial.o $(BUILD_DIR)/arch_ports.o $(BUILD_DIR)/idt.o $(BUILD_DIR)/isr.o \
	$(BUILD_DIR)/isr_asm.o $(BUILD_DIR)/debug_tests.o $(BUILD_DIR)/pic.o $(BUILD_DIR)/pci.o $(BUILD_DIR)/rtc.o \
	$(BUILD_DIR)/vbe.o $(BUILD_DIR)/vbe_terminal.o $(BUILD_DIR)/surface.o $(BUILD_DIR)/dirty_rect.o \
	$(BUILD_DIR)/framebuffer.o $(BUILD_DIR)/gfx2d.o $(BUILD_DIR)/mouse.o $(BUILD_DIR)/elf.o \
	$(BUILD_DIR)/usb.o $(BUILD_DIR)/uhci.o $(BUILD_DIR)/usb_hid_mouse.o \
	$(BUILD_DIR)/rtl8139.o $(BUILD_DIR)/rtl8168.o $(BUILD_DIR)/net.o $(BUILD_DIR)/udp.o $(BUILD_DIR)/dhcp.o \
	$(BUILD_DIR)/dns.o $(BUILD_DIR)/tcp.o $(BUILD_DIR)/socket.o $(BUILD_DIR)/http_parser.o \
	$(BUILD_DIR)/http_client.o $(BUILD_DIR)/http_server.o $(BUILD_DIR)/websocket.o $(BUILD_DIR)/klog.o \
	$(BUILD_DIR)/firewall.o $(BUILD_DIR)/irq.o $(BUILD_DIR)/irq_asm.o $(BUILD_DIR)/timer.o \
	$(BUILD_DIR)/keyboard.o $(BUILD_DIR)/boot_info.o $(BUILD_DIR)/pmm.o $(BUILD_DIR)/vmm.o \
	$(BUILD_DIR)/kheap.o $(BUILD_DIR)/gdt.o $(BUILD_DIR)/gdt_asm.o $(BUILD_DIR)/tss.o $(BUILD_DIR)/object.o $(BUILD_DIR)/sync.o $(BUILD_DIR)/task.o \
	$(BUILD_DIR)/scheduler.o $(BUILD_DIR)/switch_asm.o $(BUILD_DIR)/usermode.o $(BUILD_DIR)/usermode_asm.o \
	$(BUILD_DIR)/syscall.o $(BUILD_DIR)/syscall_asm.o $(BUILD_DIR)/usermode_app.o $(BUILD_DIR)/ata.o \
	$(BUILD_DIR)/vfs.o $(BUILD_DIR)/devfs.o $(BUILD_DIR)/procfs.o $(BUILD_DIR)/fat32.o $(BUILD_DIR)/rootfs.o \
	$(BUILD_DIR)/userfs.o $(BUILD_DIR)/pipe.o \
	$(BUILD_DIR)/shell_core.o $(BUILD_DIR)/shell_parser.o $(BUILD_DIR)/shell_dispatch.o \
	$(BUILD_DIR)/cmd_sys.o $(BUILD_DIR)/cmd_task.o $(BUILD_DIR)/cmd_fs.o $(BUILD_DIR)/cmd_net.o \
	$(BUILD_DIR)/gui.o $(BUILD_DIR)/window.o \
	$(BUILD_DIR)/compositor.o $(BUILD_DIR)/tcp_timer.o $(BUILD_DIR)/wait.o $(BUILD_DIR)/test_tcp.o \
	$(BUILD_DIR)/terminal_app.o $(BUILD_DIR)/explorer_app.o $(BUILD_DIR)/bmp.o \
	$(BUILD_DIR)/aurora_url.o $(BUILD_DIR)/aurora_http_client.o \
	$(BUILD_DIR)/aurora_dom.o $(BUILD_DIR)/aurora_html_parser.o \
	$(BUILD_DIR)/browser_app.o $(BUILD_DIR)/desktop_ui.o $(BUILD_DIR)/firewall_app.o \
	$(BUILD_DIR)/taskmgr_app.o


.PHONY: all clean run debug info user_apps_build

all: $(DISK_IMAGE)

$(BOOT_BIN): $(BOOT_SRC) | $(BUILD_DIR)
	@echo "[AS]  $< --> $@"
	$(AS) $(ASFLAGS) $< -o $@

$(BOOT2_BIN): $(BOOT2_SRC) | $(BUILD_DIR)
	@echo "[AS]  $< --> $@"
	$(AS) $(ASFLAGS) $< -o $@

VPATH = kernel:kernel/arch:kernel/drivers:kernel/fs:kernel/mm:kernel/task:kernel/gui:kernel/net:kernel/apps:kernel/debug:kernel/tests/net:kernel/shell:kernel/commands

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	@echo "[CC]  $< --> $@"
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.asm | $(BUILD_DIR)
	@echo "[AS]  $< --> $@"
	$(AS) -f elf32 $< -o $@

$(BUILD_DIR)/arch_ports.o: kernel/arch/ports.c | $(BUILD_DIR)
	@echo "[CC]  $< --> $@"
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/debug_tests.o: kernel/debug/tests.c | $(BUILD_DIR)
	@echo "[CC]  $< --> $@"
	$(CC) $(CFLAGS) -c $< -o $@

$(INFLATE_OBJ): kernel/drivers/inflate.c kernel/include/inflate.h | $(BUILD_DIR)
	@echo "[CC]  $< --> $@"
	$(CC) $(CFLAGS) -c $< -o $@

$(PNG_OBJ): kernel/drivers/png.c kernel/include/png.h kernel/include/inflate.h kernel/include/surface.h kernel/include/vfs.h kernel/include/kheap.h | $(BUILD_DIR)
	@echo "[CC]  $< --> $@"
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/aurora_url.o: apps/aurora/url.c | $(BUILD_DIR)
	@echo "[CC]  $< --> $@"
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/aurora_http_client.o: apps/aurora/http_client.c | $(BUILD_DIR)
	@echo "[CC]  $< --> $@"
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/aurora_dom.o: apps/aurora/dom.c | $(BUILD_DIR)
	@echo "[CC]  $< --> $@"
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/aurora_html_parser.o: apps/aurora/html_parser.c | $(BUILD_DIR)
	@echo "[CC]  $< --> $@"
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel.elf: $(ALL_OBJS) $(LINKER_LD) | $(BUILD_DIR)
	@echo "[LD]  kernel.elf --> $@"
	$(LD) $(LDFLAGS) $(ALL_OBJS) -o $@

$(BUILD_DIR)/kernel.bin: $(BUILD_DIR)/kernel.elf | $(BUILD_DIR)
	@echo "[OC]  $< --> $@"
	$(OBJCOPY) -O binary $< $@

$(DISK_IMAGE): $(BOOT_BIN) $(BOOT2_BIN) $(BUILD_DIR)/kernel.bin user_apps_build
	@echo "[DD]  Creating disk image: $(DISK_IMAGE)"
	dd if=/dev/zero      of=$(DISK_IMAGE) bs=512 count=2048            status=none
	dd if=$(BOOT_BIN)    of=$(DISK_IMAGE) bs=512 count=1  conv=notrunc status=none
	dd if=$(BOOT2_BIN)   of=$(DISK_IMAGE) bs=512 seek=1   conv=notrunc status=none
	dd if=$(BUILD_DIR)/kernel.bin  of=$(DISK_IMAGE) bs=512 seek=5   conv=notrunc status=none
	python3 tools/mkfat32.py || python tools/mkfat32.py
	dd if=$(BUILD_DIR)/fat32_partition.img of=$(DISK_IMAGE) bs=512 seek=512 conv=notrunc status=none

user_apps_build:
	$(MAKE) -C user_apps

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

run: $(DISK_IMAGE)
	qemu-system-i386 -drive format=raw,file=$(DISK_IMAGE) -m 64M -no-reboot -no-shutdown -device VGA,vgamem_mb=16 -serial stdio -netdev tap,id=n1,ifname=tap0,script=no,downscript=no -device rtl8139,netdev=n1

run-vbe: $(DISK_IMAGE)
	qemu-system-i386 -drive format=raw,file=$(DISK_IMAGE) -m 64M -no-reboot -no-shutdown -device VGA,vgamem_mb=16 -serial stdio -netdev user,id=n1 -device rtl8139,netdev=n1


clean:
	rm -rf $(BUILD_DIR) $(DISK_IMAGE)
	$(MAKE) -C user_apps clean
