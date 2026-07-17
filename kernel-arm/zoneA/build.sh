#!/bin/sh
# GAIA-OS Zone A — build + boot the M1-seed stub
set -e
cd "$(dirname "$0")"

CFLAGS="--target=aarch64-none-elf -ffreestanding -nostdlib -O2 -Wall"

# CHALICE C1 slice 1 — build the PE-ARM64 test artifacts and embed them as
# byte arrays (mirrors the zcaot_bin embed step in zcmod.c, but generated
# fresh each build instead of hand-baked).
../chalice/mkpe.sh
{
    xxd -i -n pehello1_bin ../chalice/hello1.exe | sed \
        -e 's/^unsigned char/const unsigned char/' \
        -e 's/^unsigned int pehello1_bin_len/const unsigned long pehello1_len/'
    xxd -i -n pehello2_bin ../chalice/hello2.exe | sed \
        -e 's/^unsigned char/const unsigned char/' \
        -e 's/^unsigned int pehello2_bin_len/const unsigned long pehello2_len/'
    xxd -i -n pehello3_bin ../chalice/hello3.exe | sed \
        -e 's/^unsigned char/const unsigned char/' \
        -e 's/^unsigned int pehello3_bin_len/const unsigned long pehello3_len/'
} > pehello_bin.c

clang $CFLAGS -c boot.S -o boot.o
clang $CFLAGS -c ctx.S -o ctx.o
clang $CFLAGS -c kmain.c -o kmain.o
clang $CFLAGS -c mem.c -o mem.o
clang $CFLAGS -c task.c -o task.o
clang $CFLAGS -c shell.c -o shell.o
clang $CFLAGS -c zcmod.c -o zcmod.o
clang $CFLAGS -c ntdisp.S -o ntdisp.o
clang $CFLAGS -c ntsvc.c -o ntsvc.o
clang $CFLAGS -c pemod.c -o pemod.o
clang $CFLAGS -c pehello_bin.c -o pehello_bin.o
clang $CFLAGS -c virtio.c -o virtio.o
clang $CFLAGS -c fw_cfg.c -o fw_cfg.o
clang $CFLAGS -c fs.c -o fs.o
clang $CFLAGS -c hc.c -o hc.o
clang $CFLAGS -c net.c -o net.o
clang $CFLAGS -c tcp.c -o tcp.o
aarch64-elf-ld -T linker.ld boot.o ctx.o kmain.o mem.o task.o shell.o zcmod.o ntdisp.o ntsvc.o pemod.o pehello_bin.o virtio.o fw_cfg.o fs.o hc.o net.o tcp.o -o gaia-os.elf
aarch64-elf-objcopy -O binary gaia-os.elf gaia-os.img
echo "built: $(ls -la gaia-os.img | awk '{print $5}') bytes"

if [ ! -f disk.img ]; then
    dd if=/dev/zero of=disk.img bs=1m count=4 2>/dev/null
    printf 'GAIA-OS TEMPLE DISK  LOVE=1.000  --  the first stone laid 2026-07-08' \
        | dd of=disk.img conv=notrunc 2>/dev/null
fi

# Display: headless by default (CI-able, screendump via monitor).
#   GAIA_DISPLAY=cocoa  -> operator sees the framebuffer in a window.
# Serial: mon:stdio muxes serial shell + QEMU monitor on stdio by default;
#   gate overrides GAIA_SERIAL (e.g. a unix socket) and adds its own -monitor.
QEMU_DISPLAY="${GAIA_DISPLAY:-none}"
QEMU_SERIAL="${GAIA_SERIAL:-mon:stdio}"
# CHALICE C2b Atom B: 256M -> 1G so KUSER_SHARED_DATA @ 0x7ffe0000 (fixed
# physical address the kernel writes directly, ntsvc.c nt_kuser_init) is
# actually backed by real DRAM. QEMU virt RAM starts at 0x40000000; with
# -m 1G it spans 0x40000000-0x80000000, landing 0x7ffe0000 128KB under the
# ceiling (SLICE2-SPEC Atom B receipt). mem.c's own heap arena tops out at
# 0x45000000 (HEAP_BASE 0x41000000 + HEAP_SIZE 64MB) -- verified nowhere
# near 0x7ffe0000 (~0.8GB of headroom), no guard needed there.
exec qemu-system-aarch64 -M virt,gic-version=3 -cpu cortex-a72 -m 1G \
    -global virtio-mmio.force-legacy=false \
    -drive if=none,file=disk.img,format=raw,id=hd \
    -device virtio-blk-device,drive=hd \
    -netdev user,id=n0 -device virtio-net-device,netdev=n0 \
    -device ramfb \
    -kernel gaia-os.img \
    -serial "$QEMU_SERIAL" -display "$QEMU_DISPLAY" "$@"
