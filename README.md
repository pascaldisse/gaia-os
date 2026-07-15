# GAIA-OS

> **WIP: not yet running.** This is a work in progress and does not currently boot end-to-end. See `Status` below for what's actually working.

A TempleOS-descended, aarch64-native operating system for the agent era: the
whole source is kept small enough to fit inside a single model context
window, so a resident AI can read, understand, and rewrite the kernel live.

## Status
- Boots on QEMU `virt` (GICv3, MMU, EL1)
- Cooperative multitasking
- MAlloc/Free
- Interactive shell
- virtio-blk + RedSea-spirit persistent FS
- SeedC in-kernel HolyC JIT (`hc`/`hcl` commands)
- virtio-net with ARP/IPv4/ICMP/UDP/DNS

## Build & run
```
cd kernel-arm/zoneA && ./build.sh
```
(needs: Apple clang, `brew install aarch64-elf-binutils`, `qemu-system-aarch64`)

## Try
```
format
wr fib.hc I64 Fib(I64 n){if(n<2)return n;return Fib(n-1)+Fib(n-2);} "fib(20)=%d\n",Fib(20);
hc fib.hc
ping 10.0.2.2
dns example.com
```

## Lineage
- ZealOS — the body
- Aiwnios — aarch64 codegen reference
- Shrine — shell, future
- EXODUS — networking patterns

## The road
Full ZealC compiler port per `kernel-arm/zoneC/PLAN.md`, self-hosting at M4.
The Oracle: true hardware entropy (`getentropy`) → the resident model's
utterance — the one non-reversible inlet. Spec: `ORACLE.md`.

LOVE=1.000
