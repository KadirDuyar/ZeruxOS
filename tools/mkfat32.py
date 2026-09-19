#!/usr/bin/env python3
"""
ZeruX OS — FAT32 Disk Image Builder Tool
Creates a 100% valid FAT32 Sector Layout starting at LBA 256 of zerux.img:
  - Sector 256: FAT32 Volume Boot Record (VBR / BPB)
  - Sector 288: FAT1 Table
  - Sector 320: FAT2 Table
  - Sector 352: Data Area (Cluster 2 = Root Directory)
  - Dynamically appends files to data clusters and updates FAT & Root Dir.
"""

import sys
import struct
import os
import glob

def build_fat32_sectors():
    # 512-byte sectors array (65536 sectors = 32 MB)
    sectors = bytearray(512 * 65536)

    # 1. Sector 0 (Relative 0 -> Absolute 256): FAT32 VBR / BPB
    bpb = bytearray(512)
    bpb[0:3] = b'\xEB\x58\x90'
    bpb[3:11] = b'ZERUXOS '
    struct.pack_into('<H', bpb, 11, 512)     # bytes per sector
    bpb[13] = 1                              # sectors per cluster
    struct.pack_into('<H', bpb, 14, 32)      # reserved sector count
    bpb[16] = 2                              # num FATs
    struct.pack_into('<H', bpb, 17, 0)       # root entry count
    struct.pack_into('<H', bpb, 19, 0)       # total sectors 16
    bpb[21] = 0xF8                           # media type
    struct.pack_into('<H', bpb, 22, 0)       # fat16 size
    struct.pack_into('<H', bpb, 24, 63)      # sectors per track
    struct.pack_into('<H', bpb, 26, 255)     # num heads
    struct.pack_into('<I', bpb, 28, 256)     # hidden sectors (LBA offset 256)
    struct.pack_into('<I', bpb, 32, 65536)   # total sectors 32 (32 MB)
    struct.pack_into('<I', bpb, 36, 512)     # fat32 size (512 sectors)
    struct.pack_into('<H', bpb, 40, 0)       # ext flags
    struct.pack_into('<H', bpb, 42, 0)       # fs version
    struct.pack_into('<I', bpb, 44, 2)       # root cluster
    struct.pack_into('<H', bpb, 48, 1)       # fs info sector
    struct.pack_into('<H', bpb, 50, 6)       # backup boot sector
    bpb[66] = 0x29                           # boot signature
    struct.pack_into('<I', bpb, 67, 0x12345678) # volume id
    bpb[71:82] = b'ZERUX DISK '
    bpb[82:90] = b'FAT32   '
    bpb[510:512] = b'\x55\xAA'               # Boot signature
    sectors[0:512] = bpb

    fat1_offset = 32 * 512
    fat2_offset = (32 + 512) * 512
    root_offset = (32 + 512*2) * 512

    # FAT 0, 1 reserved. Cluster 2 is Root Dir.
    struct.pack_into('<I', sectors, fat1_offset + 0*4, 0x0FFFFFF8)
    struct.pack_into('<I', sectors, fat1_offset + 1*4, 0x0FFFFFFF)
    struct.pack_into('<I', sectors, fat1_offset + 2*4, 0x0FFFFFFF)
    struct.pack_into('<I', sectors, fat2_offset + 0*4, 0x0FFFFFF8)
    struct.pack_into('<I', sectors, fat2_offset + 1*4, 0x0FFFFFFF)
    struct.pack_into('<I', sectors, fat2_offset + 2*4, 0x0FFFFFFF)
    
    current_cluster = 3
    root_entry_idx = 0
    bin_entry_idx = 0

    # Create BIN directory cluster at current_cluster
    BIN_DIR_CLUSTER = current_cluster
    current_cluster += 1
    struct.pack_into('<I', sectors, fat1_offset + BIN_DIR_CLUSTER*4, 0x0FFFFFFF)
    struct.pack_into('<I', sectors, fat2_offset + BIN_DIR_CLUSTER*4, 0x0FFFFFFF)
    
    # Add BIN to root directory
    entry_offset = root_offset + root_entry_idx * 32
    entry = bytearray(32)
    entry[0:11] = b'BIN        '
    entry[11] = 0x10  # DIRECTORY attribute
    struct.pack_into('<H', entry, 20, (BIN_DIR_CLUSTER >> 16) & 0xFFFF)
    struct.pack_into('<H', entry, 26, BIN_DIR_CLUSTER & 0xFFFF)
    struct.pack_into('<I', entry, 28, 0)
    sectors[entry_offset : entry_offset + 32] = entry
    root_entry_idx += 1

    # Add . and .. to BIN directory
    bin_dir_offset = (1056 + (BIN_DIR_CLUSTER - 2)) * 512
    
    # . entry
    dot_entry = bytearray(32)
    dot_entry[0:11] = b'.          '
    dot_entry[11] = 0x10
    struct.pack_into('<H', dot_entry, 20, (BIN_DIR_CLUSTER >> 16) & 0xFFFF)
    struct.pack_into('<H', dot_entry, 26, BIN_DIR_CLUSTER & 0xFFFF)
    sectors[bin_dir_offset : bin_dir_offset + 32] = dot_entry
    bin_entry_idx += 1

    # .. entry (parent is root, cluster 0 in FAT32)
    dotdot_entry = bytearray(32)
    dotdot_entry[0:11] = b'..         '
    dotdot_entry[11] = 0x10
    struct.pack_into('<H', dotdot_entry, 20, 0)
    struct.pack_into('<H', dotdot_entry, 26, 0)
    sectors[bin_dir_offset + 32 : bin_dir_offset + 64] = dotdot_entry
    bin_entry_idx += 1

    def add_file(filename_83, data, is_bin=False):
        nonlocal current_cluster, root_entry_idx, bin_entry_idx
        size = len(data)
        num_clusters = (size + 511) // 512
        if num_clusters == 0: num_clusters = 1

        start_cluster = current_cluster

        max_data_sectors = (len(sectors) // 512) - 1056  # sectors available after the data area starts
        if (start_cluster - 2) + num_clusters > max_data_sectors:
            raise RuntimeError(
                f"mkfat32.py: disk image is full — '{filename_83}' ({size} bytes) doesn't fit. "
                f"Increase `sectors = bytearray(512 * 1024)` near the top of build_fat32_sectors() "
                f"(and the matching BPB total-sector fields) to make room for more /BIN apps."
            )

        # Write FAT chain
        for i in range(num_clusters):
            c = current_cluster + i
            next_c = c + 1 if i < num_clusters - 1 else 0x0FFFFFFF
            struct.pack_into('<I', sectors, fat1_offset + c*4, next_c)
            struct.pack_into('<I', sectors, fat2_offset + c*4, next_c)
            
            # Write data
            data_offset = (1056 + (c - 2)) * 512
            chunk = data[i*512 : (i+1)*512]
            sectors[data_offset : data_offset + len(chunk)] = chunk

        # Write root entry — FAT32 standard 32-byte directory entry layout:
        # Offset  0-10: Name (8.3 format, space-padded)
        # Offset 11:    Attributes
        # Offset 18-19: DIR_LstAccDate  (last access date)
        # Offset 20-21: DIR_FstClusHI   (first cluster HIGH) ← offset 20
        # Offset 22-23: DIR_WrtTime
        # Offset 24-25: DIR_WrtDate
        # Offset 26-27: DIR_FstClusLO   (first cluster LOW)  ← offset 26
        # Offset 28-31: DIR_FileSize    (file size)           ? offset 28
        if is_bin:
            dir_offset = (1056 + (BIN_DIR_CLUSTER - 2)) * 512
            entry_offset = dir_offset + bin_entry_idx * 32
        else:
            entry_offset = root_offset + root_entry_idx * 32
            
        entry = bytearray(32)
        entry[0:11] = filename_83.encode('ascii').ljust(11, b' ')[:11]
        entry[11] = 0x20  # ARCHIVE attribute
        struct.pack_into('<H', entry, 20, (start_cluster >> 16) & 0xFFFF)  # FstClusHI at offset 20
        struct.pack_into('<H', entry, 26, start_cluster & 0xFFFF)          # FstClusLO at offset 26
        struct.pack_into('<I', entry, 28, size)                             # FileSize  at offset 28
        sectors[entry_offset : entry_offset + 32] = entry

        current_cluster += num_clusters
        if is_bin:
            bin_entry_idx += 1
        else:
            root_entry_idx += 1

    add_file("WELCOME TXT", b'Hello from REAL FAT32 Disk Sector via Primary ATA Driver!\n\x00')
    add_file("NOTES   TXT", b'ZeruX OS Real Sector-Level FAT32 BPB & Cluster Chain Verified.\n\x00')
    
    # Inject user_apps into BIN
    def inject_bin_file(src, name83):
        try:
            with open(src, "rb") as f:
                data = f.read()
            add_file(name83, data, is_bin=True)
            print(f"Included {src} as {name83} in FAT32 /BIN.")
        except Exception as e:
            print(f"Could not read {src}, skipping. " + str(e))

    def make_83_name(path):
        """Derive a FAT 8.3 short name from an arbitrary filename, e.g.
        'user_apps/bin/whoami.elf' -> 'WHOAMI  ELF'. Non-alphanumeric
        characters are dropped and the base name is truncated to 8 chars
        (real FAT32 would use a ~1 tail on collision; for this hobby-OS
        build we just detect and skip collisions loudly instead, since
        source filenames are under the developer's control)."""
        base, ext = os.path.splitext(os.path.basename(path))
        ext = ''.join(c for c in ext.lstrip('.').upper() if c.isalnum())[:3]
        base = ''.join(c for c in base.upper() if c.isalnum() or c in "_-")[:8]
        if not base:
            base = "FILE"
        return (base.ljust(8) + ext.ljust(3))

    # Auto-discovery: every .elf produced by `make -C user_apps` gets
    # embedded automatically — drop a new app in user_apps/bin/*.c, run
    # the build, and it shows up in /BIN with no edits here.
    bin_src_dir = "user_apps/bin"
    elf_paths = sorted(glob.glob(os.path.join(bin_src_dir, "*.elf")))
    if not elf_paths:
        print(f"WARNING: no .elf files found in {bin_src_dir}/ — "
              f"did 'make -C user_apps' run before mkfat32.py?")

    used_names = set()
    for elf_path in elf_paths:
        name83 = make_83_name(elf_path)
        if name83 in used_names:
            print(f"WARNING: 8.3 name collision on '{name83}' for {elf_path} — "
                  f"skipped (rename the source .c file so its first 8 chars are unique).")
            continue
        used_names.add(name83)
        inject_bin_file(elf_path, name83)

    # Inject WebOS Files
    def inject_web_file(src, name83):
        try:
            with open(src, "rb") as f:
                data = f.read()
            add_file(name83, data)
            print(f"Included {src} as {name83} in FAT32.")
        except Exception as e:
            print(f"Could not read {src}, skipping. " + str(e))

    inject_web_file("web/index.html", "INDEX   HTM")
    inject_web_file("web/style.css",  "STYLE   CSS")
    inject_web_file("web/desktop.js", "DESKTOP JS ")
    inject_web_file(r"assets/Masaüsüt.png",         "WALLPAPRPNG")
    inject_web_file(r"assets/Aurora.png",            "AURORA  PNG")
    inject_web_file(r"assets/dosyaYönetici.png",     "EXPLORERPNG")
    inject_web_file(r"assets/firewall.png",          "FIREWALLPNG")
    inject_web_file(r"assets/terminal.png",          "TERMINALPNG")
    inject_web_file(r"assets/Baslat.png",            "START   PNG")
    inject_web_file(r"assets/Boot.png",              "BOOT    PNG")
    inject_web_file(r"assets/Kapat.png",             "SHUTDOWNPNG")
    inject_web_file(r"assets/YenidenBaşlat.png",     "RESTART PNG")

    return sectors

if __name__ == '__main__':
    data = build_fat32_sectors()
    with open('build/fat32_partition.img', 'wb') as f:
        f.write(data)
    print(f"Generated FAT32 Partition Image ({len(data)} bytes, {len(data)//512} sectors).")
