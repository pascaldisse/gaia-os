#!/bin/sh
# CHALICE C4-5 — build the two i386 (Machine 0x14C) PE32 guest artifacts:
#   hello8_x86.exe — zero-import toolchain probe (ret=42), C4-1
#   hello9_x86.exe — real kernel32 import table (GetStdHandle/WriteFile,
#                    "hello9: the door opens\n", ret=9), the C4-5 done-line
# Deliberately a SEPARATE script from mkpe.sh (collision law: mkpe.sh is
# owned by the parallel kernel lane; this file only ever builds *_x86).
# Same conventions: zig cc as an i386-windows-gnu (lld, mingw-mode)
# frontend, no CRT, freestanding, custom entry PeMain, -nostdlib.
#
# Working flag set for zig i386 freestanding no-CRT PE32 builds (proven):
#   -target x86-windows-gnu -ffreestanding -nostdlib -fuse-ld=lld -O0
#   -fno-sanitize=all -Wl,-e,PeMain [-lkernel32 for the import-table exe]
# Notes: zig spells i386 "x86" in the triple (same target hello8 used in
# mkpe.sh); `-e PeMain` is auto-decorated to _PeMain by lld's mingw driver;
# -lkernel32 resolves __imp__GetStdHandle@4/__imp__WriteFile@20 from zig's
# bundled mingw-w64 import def, producing a genuine PE32 Import Directory.
set -e
cd "$(dirname "$0")"

# Every varying value parameterized with a default (IRON law).
ZIG="${ZIG:-zig}"
TARGET32="${TARGET32:-x86-windows-gnu}"
ENTRY="${ENTRY:-PeMain}"
CFLAGS32="${CFLAGS32:--target $TARGET32 -ffreestanding -nostdlib -fuse-ld=lld -O0 -fno-sanitize=all}"

"$ZIG" cc $CFLAGS32 -Wl,-e,"$ENTRY" -o hello8_x86.exe hello8_x86.c
"$ZIG" cc $CFLAGS32 -Wl,-e,"$ENTRY" -o hello9_x86.exe hello9_x86.c -lkernel32

echo "hello8_x86.exe: $(wc -c < hello8_x86.exe | tr -d ' ') bytes"
echo "hello9_x86.exe: $(wc -c < hello9_x86.exe | tr -d ' ') bytes"
