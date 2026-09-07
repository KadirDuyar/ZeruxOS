# ZeruX OS

ZeruX OS is an independent, 32-bit (x86) protected-mode operating system written from scratch in C and Assembly. It features a custom bootloader, a cooperative window manager, network stack, and user-space applications.

> **Note:** Development is ongoing. The system is constantly evolving with new features and drivers.

## Features

- **Bootloader:** Custom 2-stage MBR bootloader (Real Mode -> Protected Mode -> Kernel).
- **Graphics:** VESA BIOS Extensions (VBE) with dynamic resolution fallback (1024x768 or 800x600 at 32bpp/24bpp) and a 3MB linear shadow framebuffer.
- **GUI & Window Manager:** Compositor with dirty-rectangle tracking, z-order window management, and singleton app support.
- **Input:** PS/2 Mouse & Keyboard, plus USB UHCI Boot-Protocol Mouse support (with dynamic conflict resolution).
- **Storage:** IDE/ATA PIO disk driver with FAT32 file system support. Includes a RAM-based `/user` virtual file system.
- **Networking:** RTL8139 driver, DHCP client, UDP/TCP stack, and virtual network support (QEMU SLIRP or TAP).
- **User Space:** Ring 3 execution, modular interactive shell, and ELF executable loading.
- **Applications:** Explorer, Terminal, Task Manager, Firewall (GUI windows).

## Screenshots

![ZeruX OS Desktop](resim1_placeholder.png)
*(Desktop environment with Taskbar and Icons)*

![ZeruX OS Applications](resim2_placeholder.png)
*(Window Manager running Firewall, Terminal, and Explorer)*

## Building and Running

You need a cross-compiler toolchain (`i686-elf-gcc`, `nasm`) and `qemu-system-i386`.

```bash
# Clean the build directory
make clean

# Compile kernel, bootloader, user apps and generate zerux.img
make all

# Run in QEMU with User-Mode Networking (SLIRP)
make run-vbe
```

## Structure
- `bootloader/`: Stage 1 and Stage 2 assembly bootloaders.
- `kernel/`: Core OS (Memory management, VFS, Drivers, GUI, Networking).
- `user_apps/`: Ring 3 applications compiled to ELF.
- `tools/`: Build scripts (e.g., `mkfat32.py`).

