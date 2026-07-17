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

# CHALICE C2a slice 2 Atom A — hello3.exe: NT service dispatch (ntstub.S) +
# TEB/PEB trapdoor walk. No DLL import (dispatch goes through TEB, not IAT).
"$ZIG" cc $CFLAGS -Wl,-e,PeMain -o hello3.exe hello3.c ntstub.S

# CHALICE C3a Atom 3A — mini.dll: no-import DLL test payload (exports
# MiniAdd) + hello4.exe: NtOpenFile/NtCreateSection/NtMapViewOfSection
# loader-path gate. mini.dll needs a real (if unused) entry point --
# MiniEntry, "returns 1" per SLICE3-SPEC — lld-link's default DLL entry is
# a CRT symbol we don't have (-nostdlib), so it's set explicitly same as
# the exe entries above.
"$ZIG" cc $CFLAGS -shared -Wl,-e,MiniEntry -o mini.dll minidll.c
"$ZIG" cc $CFLAGS -Wl,-e,PeMain -o hello4.exe hello4.c ntstub.S

# CHALICE C3b Atom 3B — fake-ntdll.dll (dress-rehearsal ntdll: unixlib
# wiring + LdrInitializeThunk/RtlUserThreadStart/KiUser*/etc. exports,
# needs ntstub.S linked in for its own NtContinue call) + hello5.exe
# (Phase-0 boot target: plain entry, NtWriteFile + NtTerminateProcess via
# the same TEB-dispatch stubs as hello3/hello4).
"$ZIG" cc $CFLAGS -shared -Wl,-e,FakeNtdllEntry -o fake-ntdll.dll fake-ntdll.c ntstub.S
"$ZIG" cc $CFLAGS -Wl,-e,PeMain -o hello5.exe hello5.c ntstub.S

echo "hello1.exe: $(wc -c < hello1.exe | tr -d ' ') bytes"
echo "hello2.exe: $(wc -c < hello2.exe | tr -d ' ') bytes"
echo "hello3.exe: $(wc -c < hello3.exe | tr -d ' ') bytes"
echo "hello4.exe: $(wc -c < hello4.exe | tr -d ' ') bytes"
echo "mini.dll:   $(wc -c < mini.dll | tr -d ' ') bytes"
echo "hello5.exe: $(wc -c < hello5.exe | tr -d ' ') bytes"
echo "fake-ntdll.dll: $(wc -c < fake-ntdll.dll | tr -d ' ') bytes"
