# GAIA-OS — ZealOS→aarch64 Port Map ("The Constitution")

**Goal:** A full, real operating system — ZealOS-descended, HolyC/ZealC to the bone —
booting **natively on aarch64** inside a VM (QEMU `virt` machine / Apple
Virtualization.framework), on virtio hardware. Ring 0. No userspace crutch.
First-class citizens: bash-style shell, git, networking. Agent-first: the whole
kernel fits in one model context and is rewritten live.

**Machine target:** QEMU `-M virt -cpu host -accel hvf` on Apple Silicon.
Devices: virtio-console, virtio-blk, virtio-net, virtio-gpu (framebuffer), GICv3,
ARM generic timer, PL011 UART (earliest boot printf).

## Donor repos (all cloned in this directory)
| Repo | Organ | Status |
|---|---|---|
| `zealos/` | Kernel (28,952 ln) + Compiler (22,067 ln) + System — the body | cloned, surveyed |
| `aiwnios/` | aarch64 HolyC codegen backend (`*_backend.c`) — the reference for Zone C | **built successfully on this Mac** |
| `shrine/` | `Apps/Lsh.HC` Unix-style shell + pkg manager | cloned |
| EXODUS (github.com/aiwnios/EXODUS) | HolyC networking code, host-FS mapping patterns | quarry on demand |

## Kernel zone classification (from asm/hardware survey, 2026-07-08)

### Zone A — full aarch64 rewrite (boot & CPU core)
- `KStart16.ZC`, `KStart32.ZC`, `KStart64.ZC` → replaced by single `KStartA64.ZC`:
  EL1 entry, MMU on, stack, BSS clear. No real-mode/16/32 archaeology on ARM — this *shrinks*.
- `KInterrupts.ZC`, `KExcept.ZC` → GICv3 + aarch64 exception vectors (VBAR_EL1)
- `Memory/PageTables.ZC` → aarch64 4KB granule translation tables, TTBR0/1, MAIR
- `KTask.ZC`, `Sched.ZC`, `MultiProc.ZC` → context switch (x30/sp/ELR/SPSR save-restore), PSCI for secondary cores
- `KernelA.HH` (4,568 ln) → register/struct defs: x86 seg/GDT/IDT parts replaced by aarch64 sysregs

### Zone B — replace with virtio (hardware, delete x86 legacy)
- `BlkDev/` (7,074 ln: AHCI, IDE, ISO…) → `virtio-blk` (+ keep FAT/RedSea FS logic, portable)
- `SerialDev/` (1,429 ln: PS/2 kbd/mouse…) → virtio-console + PL011; virtio-input later
- `PCIBIOS.ZC` → ECAM-based PCI config (MMIO, simpler than x86 port I/O)
- Display → virtio-gpu framebuffer (ZealOS VBE code adapts: it already draws to a linear FB)

### Zone C — compiler backend (the keystone)
- `Compiler/`: ZealC emits x86_64 → add aarch64 backend, cribbing Aiwnios's working
  aarch64 JIT (`aiwnios/*aarch64*` files). Fixed-width 4-byte instrs, simpler relocs than x86.
- Bootstrapping: cross-compile kernel from hosted Aiwnios first (it runs on this Mac),
  self-hosting inside GAIA-OS as milestone M4.

### Zone D — plain-HolyC rewrite of optimized asm primitives (fast, low-risk)
- `KMathA.ZC`, `StrA.ZC`, `KUtils.ZC`, `KHashA/B.ZC` asm blocks → portable HolyC first,
  NEON later if ever needed.

### Zone E — portable, carry over nearly untouched
- Everything else: `StrPrint`, `KDataTypes`, `Job`, `KLoad`, `EdLite`, `FontStd`,
  `Memory/MAllocFree` (1 asm block), + the entire `System/` (45k ln) and `Apps/` above the kernel.
- `shrine/Apps/Lsh.HC` → port to ZealC as the shell; real bash/git via network+FS once M3 lands.

## Milestones
- **M0** — port map + repo scaffold (this document) ✅ 2026-07-08
- **M1** — boot: QEMU virt, EL1, MMU on, PL011 prints `LOVE=1.000`, timer tick, exceptions sane
- **M2** — kernel core: MAlloc, tasks, scheduler, virtio-console interactive prompt
- **M3** — devices: virtio-blk + RedSea/FAT, virtio-net + ZealOS TCP/IP, virtio-gpu DolDoc UI
  - in-kernel HolyC JIT compiler (SeedC, `kernel-arm/zoneA/hc.c` — I64 subset, recursion, JIT to executable memory) ✅ 2026-07-10 — verified Fib(20)=6765 in QEMU
  - virtio-net + ARP/IPv4/ICMP/UDP/DNS (`kernel-arm/zoneA/net.c`, polled, slirp world 10.0.2.15) ✅ 2026-07-10
- **M4** — self-hosting: ZealC-on-aarch64 compiles the kernel inside the OS; Lsh; git; pkg
- **LOVE** — fixed at center throughout. It's not a milestone; it's the invariant.

## Worker assignments (summoned 2026-07-08)
- **terry** → Zone A: boot & CPU core design for QEMU virt
- **whale-codex** → Zone C: Aiwnios aarch64 backend → ZealC compiler port analysis
- **jareth** → Zone B: virtio driver set design in HolyC/ZealC
