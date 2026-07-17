# GAIA-OS

A TempleOS-descended, aarch64-native operating system for the agent era: the
whole source is kept small enough to fit inside a single model context
window, so a resident AI can read, understand, and rewrite the kernel live.

love=1

## Boot it

```
cd kernel-arm/zoneA && ./build.sh
```

(needs: Apple clang, `brew install aarch64-elf-binutils`, `qemu-system-aarch64`)

Seconds later you are at `T:/>` — a ~50KB bare-metal kernel on QEMU `virt`
(EL1, own MMU tables, GICv3, virtio). `Ctrl-A x` exits.

## What works (every line gate-tested)

**Kernel (zone A, `kernel-arm/zoneA`)**
- Boot, MMU, GICv3 interrupts, timer, cooperative multitasking, MAlloc/Free
- Interactive shell over serial
- virtio-blk + RedSea-spirit persistent filesystem
- virtio-net: ARP/IPv4/ICMP/UDP/DNS/TCP — `ping`, `dns`, `wget` from the prompt
- SeedC in-kernel HolyC-subset JIT (`hc`, `hcl`)
- ramfb bring-up: first HolyC-drawn pixels (graphics track begun)

**Compiler (zone C, `kernel-arm/zoneC`)**
- Terry's real compiler front end (Shrine donor tree) driving a from-scratch
  **AArch64 back end**: HolyC → IC → A64, JIT and AOT
- 10 debugging rounds, all root-caused against the x86 donor contract
  (assignment lvalues, node-relative branch deltas, PC-relative strings,
  fn-ptr deref identity, operand-clobber-safe binop loads)
- **AOT with named imports**: `IC_CALL_IMPORT` emits a literal-island call;
  donor `IET_IMM_I64` records patch with a raw 64-bit write, verbatim
  TempleOS loader semantics

**The chain, proven end to end in the booted OS:**

```
T:/> zc
zc: GaiaMod=100                                  ← Shrine-compiled compute
T:/> say
GAIA-OS: love=1 — spoken by Shrine/zoneC         ← module → kernel fn-ptr
T:/> aot
AOT-linked module: love=1, the Temple grows      ← imports resolved BY NAME
aot: ok
```

HolyC source → Shrine front end → zoneC A64 back end → AOT module with an
import table → kernel resolver patches `KPutS` by name at runtime → icache
flush → the Temple speaks. Terry's loader design, alive on ARM.

## Hosted Temple (`aiwnios/`, gitignored donor)

The full TempleOS experience runs natively on Apple Silicon via aiwnios,
reskinned as GAIA-OS (`kernel-arm/gaia-skin/`): Oracle on the home page
(God Word F7 / God Passage / God Song / God Doodle), After Egypt, the games.
`kernel-arm/tools/TempleDbg.HC` gives agents a file-driven debug channel —
drop HolyC into `dbg/cmd.HC`, read the screen back as a 640×480 BMP
(the canonical resolution: ~410 tokens per look).

## Road

M4: the compiler moves inside — GAIA-OS compiles its own kernel at `T:/>`.
Then the Temple's face (Gr/DolDoc on ramfb) on our own bare metal.

## Lineage

Terry A. Davis (TempleOS, public domain) · Shrine (minexew) · aiwnios (nrootconauto) ·
ZealOS. Built by GAIA agents under Pascal Disse. An offering, not an enclosure.
