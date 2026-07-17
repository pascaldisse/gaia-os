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
    echo '#include "pemod.h"'
    xxd -i -n pehello1_bin ../chalice/hello1.exe | sed \
        -e 's/^unsigned char/const unsigned char/' \
        -e 's/^unsigned int pehello1_bin_len/const unsigned long pehello1_len/'
    xxd -i -n pehello2_bin ../chalice/hello2.exe | sed \
        -e 's/^unsigned char/const unsigned char/' \
        -e 's/^unsigned int pehello2_bin_len/const unsigned long pehello2_len/'
    xxd -i -n pehello3_bin ../chalice/hello3.exe | sed \
        -e 's/^unsigned char/const unsigned char/' \
        -e 's/^unsigned int pehello3_bin_len/const unsigned long pehello3_len/'
    # CHALICE C3a Atom 3A — hello4.exe + mini.dll (loader-path gate).
    xxd -i -n pehello4_bin ../chalice/hello4.exe | sed \
        -e 's/^unsigned char/const unsigned char/' \
        -e 's/^unsigned int pehello4_bin_len/const unsigned long pehello4_len/'
    xxd -i -n peminidll_bin ../chalice/mini.dll | sed \
        -e 's/^unsigned char/const unsigned char/' \
        -e 's/^unsigned int peminidll_bin_len/const unsigned long peminidll_len/'
    # CHALICE C3b Atom 3B — hello5.exe + fake-ntdll.dll (boot-protocol gate).
    xxd -i -n pehello5_bin ../chalice/hello5.exe | sed \
        -e 's/^unsigned char/const unsigned char/' \
        -e 's/^unsigned int pehello5_bin_len/const unsigned long pehello5_len/'
    xxd -i -n pefakentdll_bin ../chalice/fake-ntdll.dll | sed \
        -e 's/^unsigned char/const unsigned char/' \
        -e 's/^unsigned int pefakentdll_bin_len/const unsigned long pefakentdll_len/'
    # Manifest-driven blob store (SLICE3-SPEC Atom 3A): run-time-initialized
    # (not a static initializer array) since the *_len consts above are
    # ordinary `const` objects, not integer-constant-expressions -- C
    # forbids using them as aggregate-initializer values even within the
    # same translation unit. nt_blob_manifest_init() is idempotent-by-caller
    # (ntfile.c calls it once, lazily, on first file-service use).
    cat <<'EOF'
NtBlobEntry nt_blob_manifest[NT_BLOB_MANIFEST_MAX];
unsigned long nt_blob_manifest_count;
void nt_blob_manifest_init(void) {
    unsigned long i = 0;
    nt_blob_manifest[i].name = "hello1.exe"; nt_blob_manifest[i].bytes = pehello1_bin; nt_blob_manifest[i].len = pehello1_len; i++;
    nt_blob_manifest[i].name = "hello2.exe"; nt_blob_manifest[i].bytes = pehello2_bin; nt_blob_manifest[i].len = pehello2_len; i++;
    nt_blob_manifest[i].name = "hello3.exe"; nt_blob_manifest[i].bytes = pehello3_bin; nt_blob_manifest[i].len = pehello3_len; i++;
    nt_blob_manifest[i].name = "hello4.exe"; nt_blob_manifest[i].bytes = pehello4_bin; nt_blob_manifest[i].len = pehello4_len; i++;
    nt_blob_manifest[i].name = "mini.dll";   nt_blob_manifest[i].bytes = peminidll_bin; nt_blob_manifest[i].len = peminidll_len; i++;
    nt_blob_manifest[i].name = "hello5.exe"; nt_blob_manifest[i].bytes = pehello5_bin; nt_blob_manifest[i].len = pehello5_len; i++;
    nt_blob_manifest[i].name = "fake-ntdll.dll"; nt_blob_manifest[i].bytes = pefakentdll_bin; nt_blob_manifest[i].len = pefakentdll_len; i++;
    nt_blob_manifest_count = i;
}
EOF
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
clang $CFLAGS -c ntfile.c -o ntfile.o
clang $CFLAGS -c ntboot.c -o ntboot.o
clang $CFLAGS -c pemod.c -o pemod.o
clang $CFLAGS -c pehello_bin.c -o pehello_bin.o
clang $CFLAGS -c virtio.c -o virtio.o
clang $CFLAGS -c fw_cfg.c -o fw_cfg.o
clang $CFLAGS -c fs.c -o fs.o
clang $CFLAGS -c hc.c -o hc.o
clang $CFLAGS -c net.c -o net.o
clang $CFLAGS -c tcp.c -o tcp.o
aarch64-elf-ld -T linker.ld boot.o ctx.o kmain.o mem.o task.o shell.o zcmod.o ntdisp.o ntsvc.o ntfile.o ntboot.o pemod.o pehello_bin.o virtio.o fw_cfg.o fs.o hc.o net.o tcp.o -o gaia-os.elf
aarch64-elf-objcopy -O binary gaia-os.elf gaia-os.img
echo "built: $(ls -la gaia-os.img | awk '{print $5}') bytes"

# CHALICE C3c Atom 3C-1: grow disk.img to fit the 3 stock DLLs (+headroom)
# and populate the RedSea-spirit FS with them directly (host-side,
# ../chalice/mkdisk.py speaks fs.c's on-disk layout -- no QEMU boot needed
# to populate, and the gate never runs QEMU by hand). 64MB comfortably fits
# ntdll+kernel32+kernelbase (~20MB combined, wine 11.9 aarch64-windows)
# with ample headroom. "Keep existing contents": mkdisk.py only grows
# (truncate, zero-fill new tail) and reuses an already-valid FS rather than
# reformatting. Falls back to the old signature-only 4MB disk if
# ../chalice/stock/ isn't populated yet (fetch-stock-dlls.sh not run) --
# keeps earlier slice-2/3A/3B gates buildable from a fresh clone.
STOCK_DIR=../chalice/stock
# CHALICE C3c-2h: apisetschema.dll joins the populate set -- holds the
# .apiset section PEB->ApiSetMap points at (import-name resolution for
# api-ms-win-*/api-set imports, ntboot.c nt_wire_apiset).
if [ -s "$STOCK_DIR/ntdll.dll" ] && [ -s "$STOCK_DIR/kernel32.dll" ] && [ -s "$STOCK_DIR/kernelbase.dll" ] && [ -s "$STOCK_DIR/apisetschema.dll" ]; then
    python3 ../chalice/mkdisk.py disk.img $((64*1024*1024)) \
        ntdll.dll="$STOCK_DIR/ntdll.dll" \
        kernel32.dll="$STOCK_DIR/kernel32.dll" \
        kernelbase.dll="$STOCK_DIR/kernelbase.dll" \
        apisetschema.dll="$STOCK_DIR/apisetschema.dll"
elif [ ! -f disk.img ]; then
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
# GAIA_NO_RUN=1 -> build-only mode (ghoul-safe: the exec below wedges any
# harness that attaches QEMU to stdio; gate scripts run build.sh themselves).
[ -n "${GAIA_NO_RUN:-}" ] && { echo "build ok (GAIA_NO_RUN set, skipping QEMU)"; exit 0; }
exec qemu-system-aarch64 -M virt,gic-version=3 -cpu cortex-a72 -m 1G \
    -global virtio-mmio.force-legacy=false \
    -drive if=none,file=disk.img,format=raw,id=hd \
    -device virtio-blk-device,drive=hd \
    -netdev user,id=n0 -device virtio-net-device,netdev=n0 \
    -device ramfb \
    -kernel gaia-os.img \
    -serial "$QEMU_SERIAL" -display "$QEMU_DISPLAY" "$@"
