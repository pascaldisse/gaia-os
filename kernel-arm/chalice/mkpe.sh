#!/bin/sh
# CHALICE C1 slice 1 — build the two aarch64-PE test artifacts (hello1.exe,
# hello2.exe) consumed by zoneA/pemod.c. Toolchain: zig cc as an
# aarch64-windows-gnu (lld-link) frontend + zig dlltool for the gaia.dll
# import lib. No CRT: freestanding, custom entry PeMain, -nostdlib.
set -e
cd "$(dirname "$0")"

ZIG="${ZIG:-zig}"
TARGET=aarch64-windows-gnu
CFLAGS="-target $TARGET -ffreestanding -nostdlib -fuse-ld=lld -O0 -fno-sanitize=all"

# Import lib for gaia.dll (KPutS) — zig dlltool is llvm-dlltool under the hood.
"$ZIG" dlltool -m arm64 -d gaia.def -l libgaia.a

"$ZIG" cc $CFLAGS -Wl,-e,PeMain -o hello1.exe hello1.c
"$ZIG" cc $CFLAGS -Wl,-e,PeMain -o hello2.exe hello2.c -L. -lgaia

echo "hello1.exe: $(wc -c < hello1.exe | tr -d ' ') bytes"
echo "hello2.exe: $(wc -c < hello2.exe | tr -d ' ') bytes"
