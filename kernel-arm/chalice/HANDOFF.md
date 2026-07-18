# CHALICE HANDOFF — Windows-ABI compatibility layer for GAIA-OS

Purpose: let GAIA-OS run programs built for the Windows ABI, the same way
**ReactOS, Wine, and Proton** do — by implementing the published NT service
interface as a GAIA-OS "personality." Ordinary OS-compatibility engineering:
our kernel becomes the floor a portable PE userland stands on. No target
software is modified; we implement a documented ABI so existing binaries load
and run. Donors cloned as read-only quarry (gitignored): `wine/`, `reactos/`,
`proton/`.

---

## STATE — CURRENT: C4-23 x18/CONTEXT-clobber fixed; new frontier `NtRaiseException(0xC0000026)` loop at `ntdll+0x3868C`. Runtime `1184e5b`; live 2026-07-18.

- probe receipts (`d9e98b8`/`9239e3d`, TEB64.TlsSlots@0x1480, WOW64_TLS_MAX_NUMBER=19 → THREAD_STATE=16/CONTROL_WORD=17/ENTRY_CONTEXT=18, FEX `Source/Windows/include/winternl.h:28-29`) → at first exception delivery (HLT `+0x2f0`), `x18=0x4116C120` (real TEB), `slot16(ThreadState)=0x270001000` (non-null), `slot18(EntryContext)=0x4116B7B0` with `ec.pc=0x4B7F34E4 ec.sp=0x4116B7A0 ec.x18=0x4116C120` — all sane, internally consistent (ec.x18 == live x18).
- **VERDICT: BTCpuThreadInit RAN** (not never-ran, not wrong-thread) — TLS slots 16/18 were populated correctly by wow64/FEX per-thread init before the crash. TEB_SIZE=0x1600 audited against every offset this chain touches (TlsSlots[16..18] @ 0x1480+0x80..0x90, well inside bounds); **no truncation** — neither named C4-22 candidate ("TLS never populated" / "TEB truncated") applied. Both explicitly ruled out by receipt, not assumed.
- second probe (at the `ReconstructCompactedEFLAGS` AV, `libwow64fex+0x11b28`, FAR=`0x409`) → `x18=0x0` (`teb-out-of-range`) — the guest's own x18 hardware register, captured directly by `SAVEALL_SYNC` (not a memory read that could spuriously read 0), was **zero** at this second fault, despite being valid (`0x4116C120`) moments earlier at the first fault, with **zero intervening floor syscalls** (verified: no `nt_dispatch`/`CHALICE ARENA` lines between the two probes other than exception #2's own entry).
- **root cause (C4-23, file:line-grounded, address arithmetic, not speculative):** `guest_exception_stack()` (`kmain.c`) zero-fills the full `ARM64_EXCEPTION_STACK=0x460`-byte CONTEXT/EX/RECORD region ending at `orig_sp`. `SYNC_FRAME_SIZE=0x300` (boot.S `SAVEALL_SYNC`) is **smaller** than `0x460` and **both regions end at the same `orig_sp`** — so the CONTEXT zero-fill (`[orig_sp-0x460, orig_sp-0x18)`) **fully swallows** the raw register-capture frame `x21=[orig_sp-0x300, orig_sp)` that `regs` points into. `sync_try_guest_exception`'s own GPR-copy loop (`ctx[X_OFF+i*8]=regs[i]`) and Q-register `move_bytes` ran **after** that zero-fill and read back zeroed memory; `boot.S`'s `ldr x18,[x21,#SYNC_FRAME_X18_OFF]` (the reload-before-`eret` into `KiUserExceptionDispatcher`) read the same clobbered memory. **Every guest exception delivery therefore handed the dispatcher `x18=0` (TEB=NULL)** — explaining the null `Thread->CurrentFrame` in FEX's `ReconstructCompactedEFLAGS` (`Core.cpp:177,179`): FEX fetches `Thread` via `TEB->TlsSlots[THREAD_STATE]` (`FEX Source/Windows/WOW64/Module.cpp:101-102`, `TLS.ThreadState()`), unreachable with `TEB=0`; the observed `FAR=0x409` matches a near-null-derived garbage `Thread` pointer chained through `CurrentFrame`.
- **fix (`1184e5b`):** snapshot `regs[]` onto `sync_try_guest_exception`'s own C frame (already off-guest-stack on `sync_exception_stack`, C4-21) *before* calling `guest_exception_stack()`; build the delivered CONTEXT from that snapshot; `boot.S` now reloads x18 from the delivered CONTEXT struct itself (`x0`, offset `ARM64_CONTEXT_X18_OFF=0x098`, new shared `sync_frame.h` constant with a compile-time assert tying it to `kmain.c`'s `ARM64_CONTEXT_X_OFF`) instead of the clobbered `x21` memory.
- **outcome:** re-gate all-green (`pe8_gate` `ret=78`, `pe9_gate` both backends, `ramfb_gate`, no regression). `pe10 fex` — the `libwow64fex+0x11b28` null-deref crash **is gone**; execution now reaches `NtRaiseException id=207` with `pc=0x4B7F34E4 sp=0x4116B7A0` (exactly matching the earlier-probed `EntryContext.pc/sp` — FEX correctly reconstructed and delivered a *new* guest exception from real, non-zero state), then `code=0xC000001D addr=0`, then settles into a **stable, non-crashing loop**: `NtRaiseException code=0xC0000026 (STATUS_ASSERTION_FAILURE) addr=ntdll.dll+0x3868C`, poison `pc=0xFFFFFFFFFFFFFEFE`, repeating (~9200x/45s, no new fault addresses, no forensic termination). **No `hello8_x86`/WriteFile marker yet** — new frontier is this assertion loop, not a crash; genuinely deeper than C4-21/C4-22's frontier.
- **next (new instrumentation, out of this salvage's "do not re-instrument" scope):** resolve `ntdll.dll+0x3868C` (`RtlAssert`-class internal assert, unresolved via static `llvm-objdump` symbol lookup this pass) and probe `NtRaiseException id=207`'s call site / the poison-pc `0xFFFFFFFFFFFFFEFE` origin to find what floor service or guest state the assertion is checking.

---

## STATE — C4-21 delivered-CONTEXT fixed; FEX `CurrentFrame` frontier (SUPERSEDED by C4-23 above). Runtime `11a1244`; live 2026-07-18.

- raw data abort → `ESR_EL1=0x96000006`; EC=`0x25`; ISS=`0x6`; ELR=`libwow64fex.dll+0x148A0` → `FEXCore::LookupCache::FindBlock`; FAR=`0x2909C0008` → reserve-commit arena, reservation `[0x2881C0000, +0x9800000)`, initially RESERVED.
- demand commit → seen; fired; 2MB backing + retry; proof → FAR class COMMITTED on later fault. C4-15 gate correct; no EC decoder defect.
- marker → dispatcher `[0x2681b0000,+0x4000)` live-dumped; `+0x2f0=hlt #0` → FEX **SignalHandlerReturnAddress**, `Dispatcher.cpp:379-384`; signal-return dance, not x86 decode gap. § `FEX-MAP.md` C4-21.
- CONTEXT → Wine `signal_arm64.c:309-322` requires ARM64 `CONTEXT_FULL=0x400007`; EX header real/initialized (`:140-150`, `:221-235`). Bug → floor C stack overlapped delivery at `orig_sp-0x460`; receipt corrupt flags/Cpsr. Fix → sync C stack separate; live delivery `flags=0x400007 cpsr=0x20000345 pc=0x2681b02f0 sp=0x4116b6e0`.
- HLT return → intact: `flags/pc/sp` unchanged at next delivery; execution passes `+0x2f0`; first new fault `libwow64fex+0x11b28` / `ReconstructCompactedEFLAGS` (`FEXCore Core.cpp:177`), FAR=`0x409`, null `Thread->CurrentFrame`.
- zeroer → second AV delivery valid; first subsequent `NtContinue` sees valid flags but `pc/sp/x18=0`; bracket → delivery→NtContinue. `Module.cpp:946-949` EntryContext memcpy candidate; exact owner UNVERIFIED; no speculative fix.
- commits → `3044132` ARM64 context/EX + bracket rig; `11a1244` sync-stack isolation.
- build/gates → `GAIA_NO_RUN=1 bash build.sh` PASS; `pe8_gate` PASS (`ret=78`); `pe9_gate` both backends PASS; `ramfb_gate` PASS.
- rig → `chalice/pe10_trace.py`; qSupported offer includes `swbreak+;hwbreak+`; host reply lacks both; Z0 no reply; breakpoint hit-order UNVERIFIED. Live dispatcher raw dump `/tmp/gaia-fex-dispatcher.bin`.
- pe10 fex → no hello8/WriteFile marker; furthest `libwow64fex+0x11b28`.
- wowbox64 → parked fallback; prior `elr=0/far=0` JIT-cache fault.

Next → donor-derive `Thread->CurrentFrame` lifecycle at `BTCpuSimulate`/SignalHandlerReturn; trace why reset path sees null frame; preserve second delivery context through `NtContinue`; then hello8 WriteFile.

---

## STATE — hello7/pe8 floor (leg-8 baseline, SUPERSEDED by CURRENT above; HEAD moved to `2c24a58`). Still the pe8 required-green floor:

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
Ground truth via `llvm-objdump`: `kernelbase.dll!GetModuleHandleA` →
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
- **Tooling:** `/opt/homebrew/opt/llvm@20/bin/llvm-objdump` reads symbols in the
  stock DLLs (ImageBase `0x180000000`, so file-RVA = vaddr − 0x180000000). Real
  Wine ARM64 syscall-number map frozen at `chalice/wine-arm64-syscalls.txt`
  (260 entries). Donor `wine/` is gitignored — a fresh clone lacks it; the main
  working copy has it. Method that worked all of slice 3: **measure, don't
  guess** — instrument-first per fault, resolve every address to
  `<module>+offset`, consult the donor source to ground the fix.

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
11. **Guest-exception CONTEXT stack (`0x460` bytes) can swallow the raw
    register-capture frame (`0x300` bytes) if both end at the same `orig_sp`.**
    `guest_exception_stack()`'s zero-fill silently destroyed `SAVEALL_SYNC`'s
    x0-x30/q0-q31 snapshot before it was read for CONTEXT population *and*
    before boot.S's x18-reload-before-`eret` — delivering `x18=0` (TEB=NULL)
    to every guest exception dispatch, C4-23. Any future stack-region layout
    change here must re-check for overlap by address arithmetic, not just by
    inspection; this one hid behind two prior "fixed" C4-20/C4-21 stack-
    isolation passes that addressed a *different* overlap (the C locals) and
    didn't catch this one.

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

## C4 — x86 userland floor (current status)

The floor that lets a 32-bit (i386) PE load and hand its instruction stream to
a translator DLL is built and gated. All of it is donor-grounded against the
open Wine / Hangover / box64 / FEX-Emu sources (gitignored quarry).

**Implemented + gated (`pe9_gate.py`, both backends):**
- **Two-root FS** (`ntfile.c`): `system32/` = native aarch64 tree, `syswow64/`
  = i386 guest tree, `sysnative/` = escape back to native. Redirection keyed on
  the process WOW64 flag, mirroring Windows exactly.
- **WOW64 structures** (`ntsvc.c`/`ntboot.c`): TEB32/PEB32 pair at
  `teb64+0x2000`, WOW64INFO, CROSS_PROCESS_WORK_LIST (0x4000), WOW64_CPURESERVED
  with the embedded I386_CONTEXT seeded (Eip/Esp) — all offsets donor-exact.
- **PE32 (i386) image machinery** (`pemod.c`): OptionalHeader magic `0x10B` +
  Machine `0x14C` accepted, 32-bit field widths, HIGHLOW relocations, below-4GB
  assert. `NtCreateSection`/`MapViewOfSection`/`QuerySection` needed no change.
- **Translator trio maps + inits**: `wow64.dll` + a selectable backend
  (`wowbox64.dll` default, `libwow64fex.dll` via the `fex` token) + `wow64win.dll`
  + `win32u.dll`, loaded by stock ntdll's own loader (imports resolved by the
  loader, not by hand). Backend is a runtime parameter, never hardcoded.
- **CPU truth** (`ntsvc.c`): real ARM64 ID-register values feed
  KUSER_SHARED_DATA.ProcessorFeatures + a `CentralProcessor\0` registry key, so
  the backend reads honest silicon features.
- **Memory**: reserve/commit arena (VA-only reservations in a high arena,
  2MB-block commit on touch), HIGH pool above 4GB for unconstrained allocations,
  `NtAllocateVirtualMemoryEx`, parameterized QEMU RAM (default 4G) — real Windows
  memory semantics, which the translators depend on.

**Shell verbs** (isolated probes, never the canonical gate): `pe9`/`pe9 fex`
(map the trio + backend), `pe10`/`pe10 fex` (boot `hello8_x86.exe` as an i386
main image and let the loader drive the backend). `scratch/pe10_probe.py` runs
them under a socket harness and SIGTERMs QEMU regardless of outcome.

**Two backends — furthest point reached:**
- **FEX (`pe10 fex`) = the live lead.** Stock ntdll loads the trio, `wow64.dll`
  calls the backend's init, FEX's own init banner prints
  (`starting FEX (Hangover 11.9)`), two `NtAllocateVirtualMemoryEx` calls serve
  it, then a fault deep inside `libwow64fex.dll` (`elr=+0x1545DC`,
  `lr=+0x165D14`) — a genuine FEX-internal call chain, its own JIT init gap.
  Not yet at `BTCpuSimulate` running the exe's own bytes.
- **wowbox64 (`pe10`) = parked fallback.** Its init runs (banner prints,
  dynarec initializes), then faults in its own JIT code-cache region
  (`elr=0/far=0`), unchanged since leg-11.

**Next step (superseded by C4-23 above — kept for prior-leg archaeology only):**
the `libwow64fex.dll+0x1545DC`/`elr+0x11b28` faults described here and in
C4-19/20/21 are now understood and fixed (C4-23: x18/CONTEXT clobber). Current
true frontier is the `NtRaiseException(0xC0000026)` assertion loop at
`ntdll+0x3868C` — see the C4-23 STATE block at the top of this file. Once that
resolves, `hello8_x86`'s WriteFile printing through the 32→64 thunk = C4-5
complete. Then the graphics rung (C3) gets `zoneA` back for MXO's window.
