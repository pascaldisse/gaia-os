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

## CHALICE C4 state refresh (2026-07-18, leg 4 -- HEAD `198e8a0`)

**C4-4 COMPLETE, verified live, both CPU backends:**
- **Two-root FS** (`ntfile.c` NtOpenFile, `b01d9ca`): dirname-segment
  redirect -- `sysnative\X` always -> flat basename (native/ARM64 escape);
  `system32\X` -> flat basename UNCHANGED unless a new
  `nt_wow64_process_active` flag is set, then -> `syswow64/X`; literal
  `syswow64\X` -> `syswow64/X` always. fs.c has no real subdirectories --
  the two roots are two on-disk NAME PREFIXES on the same flat namespace
  (existing bare names byte-identical/unchanged; new `syswow64/` prefix
  names the i386-windows tree, `build.sh` populate). Donor-grounded (wine
  mirror gitlab.winehq.org/wine/wine, fetched read-only this session):
  `dlls/wow64/file.c` `get_file_redirect`, `dlls/ntdll/unix/file.c:3130-3270`,
  `dlls/ntdll/loader.c:4369`.
- **Live proof** (`dbd732a`, `nt_wow64_fs_selftest`, real NtOpenFile calls +
  raw COFF Machine-field read via NtReadFile -- NtQuerySection can't parse
  32-bit PE yet, C4-5 territory): `system32\kernel32.dll` non-wow64 ->
  `0xAA64` (unchanged) / wow64-active -> `0x14C` (redirected!);
  `sysnative\ntdll.dll` wow64-active -> `0xAA64` (escape works);
  `syswow64\kernelbase.dll` -> `0x14C` (explicit ask). All 4 now
  required-green in `pe9_gate.py`, both backends.
- **BTCpuProcessInit/ThreadInit call** (`b01d9ca` code + `198e8a0` live
  result): donor init order grounded (`dlls/wow64/syscall.c` `process_init`
  :1039 calls `pBTCpuProcessInit()` once; `thread_init:1073` calls
  `pBTCpuThreadInit()` per thread). New opt-in shell arg `pe9 <backend>
  call` (NEVER the default `pe9`/`pe9 fex` the canonical gate drives --
  this is real ARM64 native code from an unresolved-import image,
  `do_imports=0`, and `sync_handler` has no recovery from a raw CPU
  exception). Live result (`scratch/pe9call_probe.py`, isolated harness,
  wowbox64 backend): `BTCpuProcessInit` executes several real calls deep
  (lr resolves inside `wowbox64.dll`, not a first-instruction failure)
  before a controlled, legible fault -- `wowbox64.dll+0x1638`, `far=0xA1`
  (small-integer dereference, the predicted signature of an unresolved
  import thunk). Satisfies the atom's own gate ("observably called --
  log line or a controlled fault") via both the log line AND the resolved
  fault. Kernel halts cleanly after (`sync_handler` dump + `wfe`); probe
  script SIGTERMs QEMU itself regardless, confirmed no leaked process.

**NOT attempted this leg (flagged, not started): C4-5** (map i386 ntdll +
`hello8_x86.exe`, enter `BTCpuSimulate` against a real 32-bit CONTEXT).
Budget spent on C4-4's two sub-atoms (two-root FS design+proof, BTCpu call
attempt+analysis) -- C4-5 needs actual 32-bit PE HEADER PARSING (`pemod.c`
currently hard-rejects any non-ARM64/non-PE32+ image, the exact gate that
made `NtQuerySection` fail on the i386 tree above) before a 32-bit CONTEXT
can even be built, unsized and likely the hardest atom of the whole C4
slice per C4-SPEC's own sizing note -- left for the next leg with a fresh
budget rather than start it uncommitted/rushed.

**No-regression:** `pe9_gate.py` (both backends, all required lines incl.
the 4 new two-root proof lines) + `pe8_gate.py` standalone both verified
PASS live at HEAD.

## CHALICE C4 state refresh (2026-07-18, leg 5 -- HEAD `52e4542`)

**C4-5 prerequisite COMPLETE, verified live, both CPU backends:**
- **PE32 (i386) image machinery** (`pemod.c`, `05f1eac`): `PeMapImage`/
  `pe_query_image_info` accept OptionalHeader magic `0x10B` (PE32) +
  Machine `0x14C` (i386) alongside PE32+/ARM64 -- 32-bit field widths
  (ImageBase@0x1C, StackReserve/Commit@0x48/0x4C, NumberOfRvaAndSizes/
  DataDirectory@0x5C/0x60, all DWORD not QWORD; SizeOfImage/SizeOfHeaders/
  Subsystem etc. land at IDENTICAL offsets on both magics, unchanged
  macros reused) -- donor-grounded (`wine/include/winnt.h`
  `IMAGE_OPTIONAL_HEADER32`). Base relocations: `IMAGE_REL_BASED_HIGHLOW`
  (type 3, 32-bit add) alongside existing DIR64 -- same `apply_relocs`
  semantics (scar #6 unchanged). New assert: PE32 image (alloc+
  SizeOfImage) must land below `0x100000000` -- legible `pe_err` if not
  (territory allocator is `0x45000000+`, well under 4GB, never expected to
  fire). `NtCreateSection`/`NtMapViewOfSection`/`NtQuerySection` (ntfile.c)
  needed ZERO changes -- they already delegate generically to
  `PeMapImage`/`pe_query_image_info`.
- **Section-path live proof** (`ntboot.c` `nt_pe32_section_selftest`,
  `dd99987`): real `NtOpenFile` -> `NtCreateSection(SEC_IMAGE)` ->
  `NtQuerySection(SectionImageInformation)` -> `NtMapViewOfSection` chain
  over TWO genuine i386 blobs -- `syswow64/ntdll.dll` (stock) and
  `hello8_x86.exe` (C4-1 recon artifact, now added to the disk populate
  set) -- both resolve `Machine=0x14C` via the FORMAT-CORRECT parser (not
  the earlier slice's raw-byte-read fstest workaround) and map
  successfully below 4GB (bases `0x4D7D0000`/`0x4EA40000`). New required-
  green lines in `pe9_gate.py` (`pe32sec syswow64-ntdll`/`pe32sec
  hello8-x86-exe`, `Machine=0x14C` substring).
- **Boot-path measure** (`shell.c`/`scratch/pe10_probe.py`, `52e4542`,
  opt-in `pe10` verb, isolated -- never the canonical gate): `nt_boot_process`
  UNCHANGED, main-image blob swapped to `hello8_x86.exe` (now mappable at
  all). Stock ntdll runs FAR into real process init (NLS maps, locale,
  registry pseudo-keys, heap/VM negotiation -- identical to pe6/7/8's
  proven native path) then FAULTS exactly where expected:
  `kernel32.dll+0x37008` (its own process-startup thunk) calls the
  resolved entry point DIRECTLY as native code -- `elr=hello8_x86.exe+0x1000`
  (the exe's real entry RVA), `ec=0` (undefined instruction -- raw x86
  opcode bytes decoded as garbage ARM64). Root cause, not a bug: our boot
  path gives ntdll/kernel32 NO WOW64 signal (no WOW64-shaped PEB/TEB
  fields wired into `nt_boot_process` itself -- that machinery
  (`nt_build_wow64_teb32`, TEB32/PEB32/CROSSPROCESSWORKLIST) exists but
  only in the SEPARATE `nt_boot_wow64_probe` function, C4-3/4 territory,
  never plugged into the real boot path), so kernel32 assumes a normal
  native launch and jumps straight into the (x86) entry point instead of
  routing through wow64cpu/`BTCpuSimulate`.

**No-regression:** `pe9_gate.py` (both backends, all required lines incl.
the 2 new pe32sec lines) + `pe8_gate.py` standalone both verified PASS
live at HEAD. qemu leak-checked (pgrep+kill) before/after every boot.

**Exact next step (C4-5 continuation):** wire `nt_boot_process`'s own
WOW64 detection/signal into the boot path itself -- either (a) grow
`nt_boot_process` with a wow64-mode branch that plugs `nt_build_wow64_teb32`'s
TEB32/PEB32/WOW64INFO/CROSSPROCESSWORKLIST structures into the SAME TEB
the real boot uses (today two disconnected code paths: the real boot
builds a plain 64-bit TEB via `nt_build_teb`, the probe builds a
throwaway one via a SEPARATE `nt_build_teb(0,0,0)` call) so stock
kernel32's own `BaseThreadInitThunk`-equivalent sees the WOW64 markers and
takes the `wow64cpu`/`BTCpuSimulate` branch instead of a direct call; or
(b) trace kernel32.dll+0x37008 in the donor (`llvm-objdump` against stock
`kernel32.dll`, ImageBase `0x180000000` file-RVA convention) to find
EXACTLY which WOW64-check it skips, ground the minimal signal needed
rather than wiring the whole TEB32/PEB32 apparatus blind. `pe10` (opt-in,
isolated) is the harness for iterating on this -- never wire it into
`pe9_gate.py`'s canonical path until it stops crash-faulting.

## CHALICE C4 state refresh (2026-07-18, leg 7 -- HEAD `<fill after commit>`)

**BTCpuProcessInit genuinely executing wowbox64.dll's own native init --
furthest point yet, real backend banners printed.** Continues from leg-6's
`nt_boot_process` WOW64-signal graft (8a470b3/166b099/1af6121). Two missing-
DLL faults burned down this leg, both via fetch-stock-dlls.sh/build.sh
populate deltas only (zero kernel source changes):

- **wow64win.dll** (`dlls/wow64/syscall.c:1035`): process_init loads it
  UNCONDITIONALLY right after CPU-backend export resolution, before
  `BTCpuProcessInit` ever runs -- missing it faulted `NtOpenFile
  win32u.dll`... no, faulted the whole process cleanly at
  `STATUS_DLL_NOT_FOUND` (`pe10 ret=0xC0000135`) with no BTCpu call
  attempted at all. Fix: `fetch-stock-dlls.sh` `WOW64_NATIVE_TARGETS`
  += `wow64win.dll` (same aarch64-windows source dir), `build.sh` mkdisk
  populate += `wow64win.dll=`.
- **win32u.dll**: NOT loaded by wow64's own process_init directly --
  ground truth via `llvm-objdump -p wow64win.dll`: wow64win.dll's OWN
  import table links against `wow64.dll` + **`win32u.dll`**
  (`NtGdiAbortDoc`/`NtGdiAbortPath`/... stubs) + `ntdll.dll`. The guest
  loader resolves a SEC_IMAGE-mapped DLL's imports right after its own
  `NtMapViewOfSection` (real ntdll loader machinery, not a syscall this
  floor implements) -- missing win32u.dll failed wow64win.dll's OWN load
  (`STATUS_DLL_NOT_FOUND`), aborting process_init one step earlier than
  visible without instrumentation (confirmed live: `NtOpenFile win32u.dll
  dir=system32` then clean abort). Fix: same two files, `win32u.dll` added
  alongside wow64win.dll.

**With both DLLs present, `pe10` (isolated probe) now runs dramatically
farther:** wow64.dll maps, WOW64 signal wires, `NtQuerySystemInformationEx`/
`NtQueryInformationProcess(ProcessWow64Information)` answer correctly
(leg-6), wowbox64.dll (CPU backend) + wow64win.dll + win32u.dll all open
and map/resolve successfully, `BTCpuProcessInit` is CALLED and genuinely
EXECUTES real backend code far enough to print **its own native startup
banners to the serial console**: `[BOX64] WowBox64 arm64 v0.4.2 (Hangover
11.9) with Dynarec built on May 17 2026 13:07:41` then `[BOX64] Dynarec for
ARM64, with extension: ASIMD`. This is wowbox64.dll's C init code running
for real (ARM64 native, not yet x86 simulation) -- proof `BTCpuProcessInit`
is not merely called but is deep inside real Box64 engine bring-up,
far past leg-4's `wowbox64.dll+0x1638`/`far=0xA1` fault.

**Furthest-point blocker: territory exhaustion, not a resolvable fault
line.** Box64's own init allocates heavily via repeated
`NtAllocateVirtualMemory` (a halving retry pattern first: `0xFD0000` ->
`0x7E8000` -> `0x3F4000`, each succeeding after backoff -- looks like a
real Wine/Box64 heap-grow-with-retry algorithm, not a bug) until the
0.75GB DLL territory (`NT_DLL_TERRITORY_BASE 0x45000000` ..
`NT_DLL_TERRITORY_LIMIT 0x7ff00000`, `ntfile.c`) is genuinely full, then
settles into an UNBOUNDED retry loop requesting the same `0x20000` (128KB)
chunk forever (`CHALICE DEBUG: territory EXHAUSTED ... status=0xC0000017`
repeating with zero backoff/giveup -- confirmed durable over a 30s drain
window, `scratch/pe10_probe.py` extended-timeout variant, never escapes,
never prints a further CHALICE line or a `pe10 ret=`). Root cause analysis
(not yet fixed): the 0.75GB territory is already sized to consume nearly
ALL of QEMU's `-m 1G` RAM (`kmain.c mmu_on` identity-maps exactly one 1GB
L1 block at `0x40000000-0x80000000`; `KUSER_SHARED_DATA` at the real-
Windows-fixed `0x7ffe0000` floors the territory ceiling, so nothing can
simply grow upward past that in the current map) -- Box64's dynarec code-
cache/heap needs is uncovering that ceiling for the first time now that
BTCpuProcessInit is genuinely running deep native code. NOT a single-fault
fix: needs either (a) a second physical RAM block + L1 identity-map entry
(`-m 2G`, add `l1_table[2]` for `0x80000000-0xC0000000`) with a NEW pool
dedicated to CPU-backend allocations (KUSER stays fixed, general DLL
territory ceiling stays put -- only the backend gets more room), or (b)
investigate whether Box64's request pattern is itself unbounded/wrong-sized
for this environment (donor-grounded first, per HANDOFF doctrine) before
assuming more RAM is the real fix. Left for the next leg with a fresh
budget -- this is the exact HANDOFF "blocked >2 cycles" stopping point,
analyzed and documented rather than patched uncommitted/rushed.

**No-regression:** `pe9_gate.py` (both backends, all required lines) +
`pe8_gate.py` standalone both verified PASS live at HEAD. qemu leak-checked
(pgrep+kill) before/after every boot this leg.

**Exact next step:** decide territory/RAM sizing approach above (Pascal-
level call: is 2GB QEMU RAM the right move, or does Box64 need a request-
size cap/pool config first) -- once BTCpuProcessInit completes past its
own init, `BTCpuSimulate` entry against the real x86 CONTEXT is the
remaining leg-goal atom, unattempted (blocked upstream of it this leg).

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
