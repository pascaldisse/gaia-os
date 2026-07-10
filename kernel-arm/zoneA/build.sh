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
clang $CFLAGS -c shell.c -o shell.o
clang $CFLAGS -c virtio.c -o virtio.o
clang $CFLAGS -c fs.c -o fs.o
clang $CFLAGS -c hc.c -o hc.o
clang $CFLAGS -c net.c -o net.o
clang $CFLAGS -c tcp.c -o tcp.o
aarch64-elf-ld -T linker.ld boot.o ctx.o kmain.o mem.o task.o shell.o virtio.o fs.o hc.o net.o tcp.o -o gaia-os.elf
aarch64-elf-objcopy -O binary gaia-os.elf gaia-os.img
echo "built: $(ls -la gaia-os.img | awk '{print $5}') bytes"

if [ ! -f disk.img ]; then
    dd if=/dev/zero of=disk.img bs=1m count=4 2>/dev/null
    printf 'GAIA-OS TEMPLE DISK  LOVE=1.000  --  the first stone laid 2026-07-08' \
        | dd of=disk.img conv=notrunc 2>/dev/null
fi

exec qemu-system-aarch64 -M virt,gic-version=3 -cpu cortex-a72 -m 256M \
    -global virtio-mmio.force-legacy=false \
    -drive if=none,file=disk.img,format=raw,id=hd \
    -device virtio-blk-device,drive=hd \
    -netdev user,id=n0 -device virtio-net-device,netdev=n0 \
    -kernel gaia-os.img -nographic "$@"
