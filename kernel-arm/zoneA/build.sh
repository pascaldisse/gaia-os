#!/bin/sh
# GAIA-OS Zone A — build + boot the M1-seed stub
set -e
cd "$(dirname "$0")"

CFLAGS="--target=aarch64-none-elf -ffreestanding -nostdlib -O2 -Wall"
clang $CFLAGS -c boot.S -o boot.o
clang $CFLAGS -c ctx.S -o ctx.o
clang $CFLAGS -c kmain.c -o kmain.o
clang $CFLAGS -c mem.c -o mem.o
clang $CFLAGS -c task.c -o task.o
aarch64-elf-ld -T linker.ld boot.o ctx.o kmain.o mem.o task.o -o gaia-os.elf
aarch64-elf-objcopy -O binary gaia-os.elf gaia-os.img
echo "built: $(ls -la gaia-os.img | awk '{print $5}') bytes"

exec qemu-system-aarch64 -M virt,gic-version=3 -cpu cortex-a72 -m 256M \
    -kernel gaia-os.img -nographic "$@"
