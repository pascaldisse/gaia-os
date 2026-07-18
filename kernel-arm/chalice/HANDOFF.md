# CHALICE HANDOFF — Windows-ABI compatibility layer for GAIA-OS

Purpose: let GAIA-OS run programs built for the Windows ABI, the same way
**ReactOS, Wine, and Proton** do — by implementing the published NT service
interface as a GAIA-OS "personality." Ordinary OS-compatibility engineering:
our kernel becomes the floor a portable PE userland stands on. No target
software is modified; we implement a documented ABI so existing binaries load
and run. Donors cloned as read-only quarry (gitignored): `wine/`, `reactos/`,
`proton/`.

---

## STATE — hello7 surface walk COMPLETE (2026-07-18), verified live. HEAD `dada57f`.

**pe8/hello7 green: widened KERNEL32.dll surface over stock ntdll+kernel32+
kernelbase.** `hello7.exe` exercises `GetCommandLineA`, `GetModuleHandleA`
(NULL + by-name), `GetProcAddress` (+ indirect call through the resolved
pointer), `GetProcessHeap`/`HeapAlloc`/`HeapFree` (write+readback a live
block), `GetCurrentProcessId`, `SetLastError`/`GetLastError` roundtrip — each
prints one proof line, all present, `CHALICE: hello7 surface walk complete`,
`pe8 ret=78`. `pe7`'s WriteFile-via-import-table milestone (`kernel32
speaks`, `ret=77`) unchanged, no regression.

**Fix this slice: TEB.StaticUnicodeString (offset `0x1258`, this ABI).**
`GetModuleHandleA("kernel32.dll")` always failed (`ERROR_MOD_NOT_FOUND`,
`0x7E`) while `GetModuleHandleA(NULL)` worked — different code paths.
Ground truth via disassembly: `kernelbase.dll!GetModuleHandleA` →
`file_name_AtoW` → `add x19,x18,#0x1258` (dest `UNICODE_STRING*` for
`RtlAnsiStringToUnicodeString`/`RtlOemStringToUnicodeString`,
`AllocateDestinationString=FALSE`). Our `nt_build_teb` never wrote this
slot — `MaximumLength=0` → `STATUS_BUFFER_OVERFLOW` on ANY non-empty ANSI
name → `GetModuleHandleA` maps that to `0x7E` regardless of real cause.
Fix (`ntsvc.c`): `TEB_SIZE` `0x400`→`0x1600`; write an empty, properly-
capacitied `UNICODE_STRING` at `teb+0x1258` (`MaximumLength=0x2C0`,
`Buffer=teb+0x1268`) in `nt_build_teb`, before the guest ever runs.

**Per-boot territory reset (prior slice, reconfirmed this slice):**
wholesale reset of the DLL-territory bump pointer + VM-arena tracking table
at the top/bottom of every `nt_boot_process` call (`nt_territory_mark`/
`nt_territory_release_since`, `nt_vm_regions_release_since`) — one guest
process resident at a time, full reclaim on exit. A prior free-list-reuse
design (`nt_territory_free`) was tried and **reverted** (over-engineering
for a single-process-at-a-time kernel with no concurrent guests to isolate);
wholesale reset is simpler and sufficient. Proven exhaustion-dead this
slice: `pe6`/`pe7`/`pe8` run **twice back-to-back in one QEMU session** (6
`nt_boot_process` calls total) — zero `STATUS_NO_MEMORY`/`0xC0000017`, all
completion markers present twice (`scratch/backtoback_gate.py`).

**Gate list, current (all required-green, no regression):** `pe1`–`pe5`
required-green lines (below) + `scratch/pe7_gate.py` (adds `pe7`) +
**`scratch/pe8_gate.py`** (adds `pe6`/`pe7`/`pe8`, now the canonical
full-surface gate — supersedes `pe7_gate.py` for day-to-day use) +
`scratch/ramfb_gate.py` + `scratch/backtoback_gate.py` (exhaustion-dead
proof, new this slice).

---

## Build & test — the only loop that counts

```
cd kernel-arm/zoneA && GAIA_NO_RUN=1 bash build.sh    # build ONLY
```
- **NEVER launch QEMU naked** — `build.sh`'s tail `exec`s QEMU on stdio. Build
  with `GAIA_NO_RUN=1`; run shell verbs ONLY through the unix-socket harnesses
  `scratch/pe7_gate.py` (pe1–pe6 required-green + pe7) and
  **`scratch/pe8_gate.py`** (same required-green + pe7 + pe8, canonical
  full-surface gate). `scratch/` is gitignored/session-local — a fresh
  clone/worker has none; recreate from the pattern in either file if missing.
  `pgrep qemu` + kill leaks before every boot.
- **Checkpoint-commit before every boot/test loop** — the 30-min worker cap /
  900s stall abort has eaten uncommitted work repeatedly.
- **Required-green gate (no regression):** `pe1 ret=42` · cup fills ·
  `pe2 ret=7` · cup speaks · continue lands · NT10 · `pe3 ret=99` · cup links ·
  `pe4 ret=60` · thunk enters · chain speaks · `pe5 ret=99` · stock ntdll
  mapped · stock thunk enters · `pe6 ret=99` · `pe7 ret=77`/kernel32 speaks ·
  **`pe8 ret=78`/hello7 surface walk complete (new this slice)**. Plus
  `scratch/ramfb_gate.py` (`-m 1G` baked into build.sh; 7 pixel checks) and
  `scratch/backtoback_gate.py` (exhaustion-dead: pe6/pe7/pe8 x2 in one boot).
- **Source-only commits** — `chalice/*.exe`/`.dll`/`.pdb` and
  `zoneA/pehello_bin.c` are build artifacts that churn every `build.sh`; leave
  them uncommitted.
- **Tooling:** `/opt/homebrew/opt/llvm@20/bin/llvm-objdump` disassembles the
  stock DLLs (ImageBase `0x180000000`, so file-RVA = vaddr − 0x180000000). Real
  Wine ARM64 syscall-number map frozen at `chalice/wine-arm64-syscalls.txt`
  (260 entries). Donor `wine/` is gitignored — a fresh clone lacks it; the main
  working copy has it. Method that worked all of slice 3: **measure, don't
  guess** — instrument-first per fault, resolve every address to
  `<module>+offset`, disassemble the donor to ground the fix.

---

## What's implemented (the NT service floor)

**Dispatcher (`zoneA/ntdisp.S`).** Wine's ARM64 path never traps: a PE stub
loads a fn-ptr from the TEB and `blr`s it. `nt_dispatch` is a table-dispatched
indirect call in one address space (no user/kernel split). Real Wine
convention: service number in `x8`, caller return address in `x9`; preserves
`x18` (TEB) + `x19–x28` across the boundary (a real syscall clobbers only
`x0`). Service table = 512 slots indexed by the **real Wine ARM64 syscall
numbers**; unregistered → `NtStub` → `STATUS_NOT_IMPLEMENTED`.

**Memory.** `NtAllocate/FreeVirtualMemory` back from the **0.75GB
identity-mapped territory** (`nt_territory_alloc`, `0x45000000`→`0x7ff00000`,
64KB-aligned per allocation), NOT the 64MB kernel heap. `NtProtectVirtualMemory`
accounted (flat model, no enforcement). `NtQueryVirtualMemory
(MemoryBasicInformation)` returns the true image AllocationBase from the image
registry — struct is **0x30 bytes**. KUSER_SHARED_DATA at `0x7ffe0000`.

**Image loading (`zoneA/pemod.c`, `zoneA/ntfile.c`).**
`NtCreateSection(SEC_IMAGE)` + `NtMapViewOfSection` map a DLL via `PeMapImage`
into territory. `NtQuerySection(SectionImageInformation)` reports the image's
Machine (0xAA64)/entry/stack sizes from the PE header. **`PeMapImage` has an
`apply_relocs` param:** `NtMapViewOfSection` passes `apply_relocs=0` (the
PE-side guest loader relocates SEC_IMAGE maps itself — donor `loader.c`
`build_module`→`perform_relocations`, unconditional); `PeRun` (standalone exes)
and `nt_boot_process`'s own ntdll/exe bootstrap pass `apply_relocs=1`.

**TEB/PEB + boot protocol (`zoneA/ntboot.c`).** `nt_boot_process` maps
ntdll+exe (Phase-0, mirrors Wine's unix side), builds TEB (4 trapdoor pointers)
+ PEB (`ApiSetMap`@0x68 via `apisetschema.dll`; Ldr module list with an ntdll
self-entry), wires the three unixlib pointers (`__wine_syscall_dispatcher`→
`nt_dispatch`, `__wine_unixlib_handle`, `__wine_unix_call_dispatcher`→
`nt_unix_call_shim`), pushes a synthetic CONTEXT, enters `LdrInitializeThunk`.

**Locale/NLS.** `NtInitializeNlsFiles` + `NtGetNlsSectionPtr` map
`locale.nls`/`sortdefault.nls`/`l_intl.nls` into territory;
`NtQueryDefaultLocale` → `0x0409`.

**Registry floor.** Minimal, in-memory: `NtCreateKey`→dummy pseudo-handle,
`NtOpenKey`/`NtQueryValueKey`→`STATUS_OBJECT_NAME_NOT_FOUND` (guest falls back
to NLS defaults), `NtSetValueKey`→no-op SUCCESS. No hive, no persistence.

**Exceptions / instrumentation (`zoneA/kmain.c`).** `NtRaiseException`/
`NtRaiseHardError` print a legible line (code + resolved `<module>+offset`) and
cleanly terminate the process instead of crash-looping. `sync_handler` dumps a
full register + fp-chain backtrace resolved through the image registry. An IRQ
PC-sampler (`profile_armed`) locates spin loops.

Real services live: NtWriteFile, NtClose, NtTerminateProcess,
NtAllocate/Free/ProtectVirtualMemory, NtQueryVirtualMemory,
NtQueryInformationProcess (ProcessBasicInformation), NtQuerySystemInformation
(SystemBasicInformation), NtContinue, NtCreateSection/MapViewOfSection/
QuerySection, NtOpenFile/ReadFile, NtInitializeNlsFiles/GetNlsSectionPtr, the
registry floor, NtRaiseException/HardError, NtQueryDefaultLocale.

---

## Scars / laws (each cost a debugging cycle — keep them)

1. **Register preservation:** `nt_dispatch` must preserve `x18` + `x19–x28`
   across the syscall boundary; a real syscall clobbers only `x0`. Leaking a
   kernel pointer or clobbering a callee-saved reg corrupts the guest's locals.
2. **Return convention:** stock Wine stubs pass the caller LR in `x9`
   (`mov x9,x30; blr; ret`) — return to it, not `x30`.
3. **Real syscall numbers:** the service table is indexed by Wine's real ARM64
   numbers (`wine-arm64-syscalls.txt`), not hand-assigned ids.
4. **LLP64:** PE-side pointer-target integers are 64-bit; match widths on
   in/out params.
5. **`build_ntdll_module` learns ntdll's own base** via
   `NtQueryVirtualMemory(MemoryBasicInformation).AllocationBase` on an in-ntdll
   address — that struct is **0x30 bytes**, and a too-strict length check made
   ntdll lose its own base (broke every self-import lookup).
6. **Guest self-relocation:** the PE-side loader relocates SEC_IMAGE maps
   itself → `NtMapViewOfSection` must NOT relocate (else `file + 2×delta`).
7. **Unixlib pointer triplet:** `*_dispatcher`/`*_handle` exports with adjacent
   RVAs are one wiring struct — check neighbors, not just the named export.
8. **Territory 64KB-aligned:** DLL image bases must be 64KB-aligned (Windows
   allocation granularity).
9. **Trapdoor pointers first:** miss one of TEB.NtTib.Self / PEB / PEB.Ldr /
   ProcessParameters and ntdll dies silently before its first service call.
10. **TEB.StaticUnicodeString (offset `0x1258`, this ABI) is load-bearing**
    for ANY ANSI-name resolution (`GetModuleHandleA` by name,
    `GetModuleFileNameA`, etc — the `AllocateDestinationString=FALSE` path
    through `RtlAnsiStringToUnicodeString`/`RtlOemStringToUnicodeString`).
    Left zeroed it silently fails every such call with `STATUS_BUFFER_
    OVERFLOW` → `ERROR_MOD_NOT_FOUND` (`0x7E`), NOT a crash — easy to miss
    since `GetModuleHandleA(NULL)` (a different, PEB-based path) keeps
    working. `nt_build_teb` must wire an empty, properly-capacitied
    `UNICODE_STRING` there before the guest ever runs.

---

## NEXT — the road to games (Pascal rules the forks)

`pe7` is the userland foundation, proven. Games (Teardown, MXO) need two more
layers — **both are Pascal's design decisions, not autonomous work:**

- **C3 — graphics.** Minimal `win32u` + a GPU/display path (shares a rung with
  the ramfb work). Teardown wants **OpenGL**; MXO wants **DirectX 9** (already
  proven under DXVK, config in-hand). Bring an options page before building.
- **C4 — x86 door.** Games are mostly x86 (MXO 32-bit primary, Teardown
  x86-64). Hangover ships the translator **as a PE DLL above this floor** (never
  a Linux substrate underneath). **Engine choice = Pascal's
  ruling: FEX vs Box64** (both ship in Hangover 11.9). 32-bit path is primary
  (≈99% of games).
- **C5 — bare metal.** QEMU is the debug rig; target is Apple-silicon metal
  (split lands at GPU + bootloader). The "replace macOS" throne.

Law: **NO DRM in the Temple** (doctrine + the only thing needing enforced memory
protection our flat model lacks). Both target games scanned clean.

Donor value: **ReactOS** = NT-kernel reference · **Wine** = userland ABI
reference · **Proton** = games-grade compatibility reference.

---

## Immediate next step for a fresh session

`hello7`/`pe8`'s widened KERNEL32.dll surface walk is green (this slice) —
GetCommandLineA/GetModuleHandleA/GetProcAddress/GetProcessHeap+HeapAlloc+
HeapFree/GetCurrentProcessId/GetLastError+SetLastError all proven. To push
further, either (a) extend `hello7.exe`/a new `hello8.exe` to call more Win32
surface (console setup, file I/O beyond NtOpenFile/ReadFile, more kernelbase
APIs) and burn down whatever it newly needs, or (b) start C3/C4 per Pascal's
ruling (see NEXT below — that's the real fork point now, not more surface-
walk atoms). Every gap from here shows itself as a legible `CHALICE FAULT` /
`NtRaiseException` line resolved to `<module>+offset` — instrument-first,
measure, then fix (TEB.StaticUnicodeString this slice: no fault line at all,
just a wrong return value — disassemble the DONOR when there's no crash to
chase, not just when there is).
