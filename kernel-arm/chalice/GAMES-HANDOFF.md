# GAMES-HANDOFF — where CHALICE actually stands on "get all the games running"

Purpose of this file: a strategic summary above the atom-by-atom log
(`HANDOFF.md`, per-atom detail, read its own top section first for the exact
current line). This file answers one question — how far is GAIA-OS from
running real Windows games, and what's left — for whoever picks this up next
(Pascal or any agent). Live 2026-07-24.

**Scope note**: this file covers ONLY the GAIA-OS/CHALICE bare-metal track (this
repo, `kernel-arm/`). There is a separate, unrelated project (Dark Souls PTDE
under macOS/Wine/CrossOver) in a different repo — not this one, not covered
here.

## The mission

GAIA-OS is a from-scratch, bare-metal ARM64 kernel (TempleOS-descended, see
root `README.md`). CHALICE is its Windows-compatibility subsystem: run real,
unmodified Windows PE executables — including actual games — on top of it,
without Linux or macOS underneath. The named game targets (`C3-SPEC.md`,
`HANDOFF.md` §NEXT): **Teardown** (needs OpenGL) and **MXO / The Matrix Online**
(needs DirectX 9, 32-bit x86). Hard law: no DRM in the Temple — both targets
were scanned clean before being named.

Three legs, all Pascal's design calls at each fork:

- **C4 — x86 door.** Get a 32-bit Windows process (ntdll → kernel32 →
  kernelbase → real syscalls) to load and run through Hangover's vendored
  FEX x86-on-ARM64 emulator, sitting as a PE DLL above this kernel's own NT
  syscall floor. Needed because ~99% of target games are 32-bit x86.
- **C3 — graphics.** A GPU/display path: Venus protocol over virtio-gpu →
  KosmicKrisp (Vulkan-on-Metal) → DXVK (D3D9→Vulkan, for MXO) + Zink
  (OpenGL→Vulkan, for Teardown). Full recon+spec exists (`C3-SPEC.md`) —
  **zero implementation started**.
- **C5 — bare metal.** Boot GAIA-OS directly on real Apple Silicon hardware
  instead of QEMU (the "replace macOS" throne road). Full recon+spec exists
  (`C5-M1-SPEC.md`) — **zero implementation started**. Explicitly a later,
  separate milestone; does not block getting games running under QEMU first.

## C4 status — the one leg with real engineering mileage

Sixty-three atoms deep (`C4-1` through `C4-63`, all in `HANDOFF.md`/
`FEX-MAP.md`). Genuine, gate-tested progress, not aspirational:

**Achieved and holding, zero regression across 8+ subsequent atoms:**
a real, unmodified 32-bit PE (`hello8_x86.exe`) boots through this kernel's
own NT syscall floor, loads real stock Windows `ntdll.dll`/`kernel32.dll`/
`kernelbase.dll` (the actual Hangover-11.9 binaries, not stubs), runs its
entry code as genuinely-emulated x86 via the vendored FEX backend, and exits
cleanly: `pe10 fex` → `NtTerminateProcess(ExitStatus=42)` → shell reports
`ret=42`. This is the full WOW64 lifecycle working end to end for the first
time. Getting here required finding and fixing real bugs in this kernel's own
floor — PE32 export parsing, 32-bit process-parameters wiring, exception
classification so FEX's own self-healing engages, image-registration
notify gaps (three separate instances of the same donor-contract gap), a
stack-frame nesting bug in the syscall dispatcher, and a one-slot stack-arg
shift in this kernel's own syscall-hinge stub. Each is documented with its
own fix commit in `HANDOFF.md`.

**Current blocker — the next milestone, not yet reached:** `hello9_x86.exe`
(same test family, but calls real `GetStdHandle`/`WriteFile` through the
actual kernel32/kernelbase import table — the next proof-of-life step before
"real game does real I/O" is plausible) fails to boot. Eight further atoms
(`C4-56` through `C4-63`) narrowed this down hard, using three independent
methods (live runtime trace-diff, register/instruction-level disassembly at
the fault, and a full static sweep of the vendored FEXCore source for every
place it can zero its internal guest-instruction-pointer tracking field): the
guest's program counter, as tracked inside FEXCore's own vendored JIT
dispatch loop, reads zero at one specific point with no syscall or exception
boundary in between for this kernel's own instrumentation to hook. This is
not a floor bug this kernel's own C code can fix — it would need actual
live single-instruction tracing inside the vendored FEX JIT itself, which
this project does not have tooling for yet. Full technical detail:
`HANDOFF.md` §C4-58 through §C4-63, `FEX-MAP.md` same sections.

**What C4 needs to actually finish (not yet scoped in detail):**
1. Either build minimal JIT-level tracing/breakpoint tooling to pin the
   `State.rip`-zeroing instruction inside FEXCore's dispatch loop, or find
   an alternate route to prove real syscall-backed I/O works (e.g. a test
   binary shaped differently than hello9 that avoids whatever code path
   triggers this) — Pascal's call on which approach.
2. Once WriteFile-class I/O is proven, the remaining C4 surface (real games'
   broader syscall/DLL footprint — threads, sync objects, more of kernel32,
   COM if MXO needs it) is still unexplored; hello8/hello9 exercise only the
   startup path.

## C3 status — not started, but designed

`C3-SPEC.md` has a real, source-grounded recon: exact virtio-gpu/Venus
device and protocol shape, the KosmicKrisp/virglrenderer host toolchain
already identified and available (`homebrew-qemu-virgl-kosmickrisp` tap),
and the DXVK/Zink translation layers named. Nothing has been built — no
kernel edits, no boots, per the spec's own header. This is a large,
from-scratch guest-side Vulkan driver (GAIA-OS has no Linux/drm substrate to
borrow from) — likely comparable in size to the C4 effort so far, maybe
larger given it also needs a real windowing/compositing surface (`win32u`).

## C5 status — not started, spec only

`C5-M1-SPEC.md` covers the iBoot→m1n1→GAIA-OS chainload path and framebuffer
handoff contract, recon-only, zero kernel edits. Not on the critical path to
"games run" (QEMU is the target rig until C3/C4 are solid) — worth revisiting
once there's something worth running on real hardware.

## Straight answer: how far from "games running"

Closer than a week ago (full crash-free 32-bit process lifecycle is real,
hard-won ground), but still far from a game. Concretely still needed, in
rough order: (1) clear the WriteFile-class I/O blocker in C4, (2) prove
enough of kernel32/ntdll's broader surface to load a real game EXE's actual
import table (threads, sync, more file I/O — games use far more than hello9
does), (3) build C3's graphics path essentially from zero, (4) get an actual
game binary through both. No shortcuts identified; each leg is genuine
systems engineering, not a config toggle. C5 (bare metal) can wait.

Read `HANDOFF.md`'s top section for the live per-atom detail before starting
new work — it moves fast and is the source of truth for exact current state.
