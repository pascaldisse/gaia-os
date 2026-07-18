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

## CHALICE C4 state refresh (2026-07-18, leg 8 -- HEAD `2c2a25d`)

**Memory wall landed per monad ruling; loop-breaker PROVEN (clean
termination, no hang); pe10 re-run reveals the REAL root cause is
RESERVE-vs-COMMIT accounting, not raw RAM size -- analyzed, not patched,
same HANDOFF stopping doctrine leg-7 used.**

**1. Mem/pool design landed** (params + donor citations, all in-source):
- `build.sh`: `GAIA_QEMU_MEM` (default `4G`) replaces the baked `-m 1G`.
  No gate script hardcodes `-m` itself (`ramfb_gate.py`/`pe*_gate.py` only
  set `GAIA_SERIAL`/`GAIA_DISPLAY` env, inheriting build.sh's default) --
  "fix the gate's invocation" needed zero gate-script edits.
- `kmain.c` `mmu_on`: identity map widened from 1 to `NT_RAM_BLOCKS`(4) x
  1GB L1 blocks (`NT_RAM_BASE`..`NT_RAM_TOP`, tracks `GAIA_QEMU_MEM`).
  **Also required TTBR0 `T0SZ` 32->25** (T0SZ=32 gave only a 4GB VA range,
  `0x0`-`0xFFFFFFFF` -- the entire HIGH pool at `>=0x100000000` was outside
  the address space the MMU could translate AT ALL, not a missing block
  descriptor; live-confirmed: first HIGH-pool touch faulted immediately,
  `far=0x100000000`, before this fix). ARMv8 4KB-granule rule (ARM ARM
  D8.2.1): T0SZ 25-33 (IA 31-39 bits) all start at level 1, same
  single-level scheme this file already used -- T0SZ=25 (IA=39, 512GB VA)
  makes the full existing 512-entry `l1_table` addressable, zero new
  table levels.
- `ntfile.c`: new HIGH pool (`NT_HIGH_TERRITORY_BASE 0x100000000`..
  `NT_HIGH_TERRITORY_LIMIT 0x140000000`, `nt_high_territory_alloc`/
  `_mark`/`_release_since`) alongside the unchanged LOW pool. Selection
  (`ntsvc.c` `NtAllocateVirtualMemory`): `ZeroBits==0` -> HIGH first, LOW
  fallback; `ZeroBits!=0` -> LOW only. Donor-grounded, not guessed:
  wow64's own thunks ALWAYS pass a nonzero zero_bits (explicit or
  wow64's own `default_zero_bits=0x7fffffff`, `wine/dlls/wow64/
  syscall.c:41`) through `get_zero_bits()` (`wine/dlls/wow64/
  wow64_private.h:94-97`) into the real 64-bit `NtAllocateVirtualMemory`;
  ntdll's own `get_zero_bits_limit()` (`wine/dlls/ntdll/unix/
  unix_private.h:569-586`) turns a nonzero zero_bits into a concrete
  ceiling, while `zero_bits==0` hits that function's own `limit=0` (no
  ceiling) path (`wine/dlls/ntdll/unix/virtual.c:5290-5297`) -- i.e.
  genuinely unconstrained native-ARM64 callers (wowbox64.dll's own
  dynarec heap/code-cache growth) are exactly the `ZeroBits==0` case, a
  32-bit guest's own VirtualAlloc never is (always arrives via the wow64
  thunk with a nonzero zero_bits). Per-boot wholesale reset covers BOTH
  pools (`ntboot.c`, both `nt_boot_process` and `nt_boot_wow64_probe`).
- Loop-breaker (`ntsvc.c` `NtAllocateVirtualMemory`): N=256
  (`NT_AVM_LOOPBREAK_N`) consecutive failed requests for the SAME
  page-rounded size -> one `CHALICE FAULT` line (size + caller resolved to
  `<module>+offset` via the real x9 caller LR at the syscall frame's
  `stub_lr` slot, `NT_FRAME_STUB_LR_OFF`) + clean termination via the
  existing `nt_setjmp`/`nt_longjmp` unwind channel (same one
  `NtTerminateProcess`/`NtRaiseException` already use).

**2. Fault-by-fault log after the wall fell (`scratch/pe10_probe.py`,
extended drain):** wow64.dll/wowbox64.dll/wow64win.dll/win32u.dll all map,
`BTCpuProcessInit` runs, `[BOX64] WowBox64 arm64 v0.4.2 ... Dynarec ...
ASIMD` banners print (same furthest point as leg-7) -- then 835 total
`NtAllocateVirtualMemory` calls before the loop-breaker fires: 114 x
`0xFD0000` (13MB, Wine's default heap RESERVE) + 40 x `0x3F4000` + 3 x
`0x7E8000` + assorted one-off growth sizes, EACH followed by a tiny
`0x10000` (64KB) `MEM_COMMIT`-flagged call actually touching a sliver of
the just-reserved range -- then 552 identical failed `0x20000` (128KB,
`MEM_RESERVE|MEM_COMMIT`) retries once both pools are full, broken
cleanly by the new loop-breaker at N=256 (`CHALICE FAULT:
NtAllocateVirtualMemory loop-broken N=0x100 size=0x20000
caller=0x4B7FF1AC (wowbox64.dll+0x2F1AC) -- terminating process cleanly`,
`pe10 ret=0xC0000017`/3221225495, shell regains the `T:/>` prompt --
CONFIRMED no hang, no naked-QEMU leak).

**3. Furthest point: exact fault + analysis (root cause quantified, not a
single resolvable line).** The 114 `0xFD0000` RESERVE-only calls alone
demand ~1.48GB of the 1.75GB combined LOW+HIGH budget -- but each backs
only a 64KB `MEM_COMMIT` touch (120 such calls total, ~7.5MB real use).
**Root cause: this territory allocator treats `MEM_RESERVE`-only requests
(`AllocationType` lacking `MEM_COMMIT`) identically to a real commit --
bumping the pointer and zero-filling the FULL requested size** -- a real
Windows `VirtualAlloc(MEM_RESERVE)` consumes zero physical memory (just
carves an address range; only a later `MEM_COMMIT`-at-base actually backs
pages). Wine/kernelbase's heap-growth algorithm reserves generously by
design (13MB per heap) and commits sparingly -- entirely normal Windows
behavior our flat/bump model can't distinguish, so it burns ~20x more
real territory than the guest ever touches. **This is a Pascal-level
design call, not a bug to autonomously fix**: a true reserve/commit split
needs per-reservation range tracking (so a later commit-at-base can back
just that sub-range) -- more structure than the flat bump arena HANDOFF
has twice ruled to keep simple (the free-list-reuse design was tried and
reverted for exactly this kind of complexity risk). Left analyzed, not
patched uncommitted/rushed -- same stopping point leg-7 hit, now
quantified precisely instead of estimated.

**No-regression:** `pe9_gate.py` (both backends, all required lines) +
`pe8_gate.py` standalone + `scratch/ramfb_gate.py` (post-`GAIA_QEMU_MEM`-
param) all verified PASS live at HEAD `2c2a25d`. qemu leak-checked
(pgrep+kill) before/after every boot this leg.



## CHALICE C4 state refresh (2026-07-18, leg 11 -- HEAD `<fill after final commit>`)

**FURTHEST POINT OF THE WHOLE C4 SLICE: genuine x86-side EIP progression
through the dynarec, live-proven.** Continuation leg, single focus (pe10
fault-by-fault past leg-9/10's RESERVE/COMMIT arena, which is confirmed
holding -- zero territory-exhaustion lines this entire leg, unlike leg-7/8).
Three burn-down cycles, each moved the fault line:

1. **Predecessor deltas disposed** (`8ee155d`): uncommitted `ntboot.c`
   (+6 lines, ntdll/exe base+size+entry debug prints) kept -- useful,
   harmless. `pe10_probe.py` drain 10->45s kept -- needed for the longer
   traces below. Stray `.bak` deleted.
2. **NtSetInformationThread (svc id=13, wine-arm64-syscalls.txt:210)
   wired** (`b63a48f` instrument stub -> `d5ebba5` real handler): measured
   live, class=0x1D(29)=ThreadWow64Context, len=0x2CC -- the documented
   real-NT SET counterpart of the GET this floor already answers
   (leg-9). Was a silent `NtStub`->`STATUS_NOT_IMPLEMENTED`; implemented
   for real (stash Eip/Esp from the incoming I386_CONTEXT, same fields
   the GET side already answers with). Did NOT move the fault (bit-
   identical crash before/after -- correctly diagnosed as *not yet on
   the path that consumes it* rather than declared a fix prematurely).
3. **WOW64_CPURESERVED's embedded I386_CONTEXT seeded** (`a973b0a` --
   THE fix that moved the fault): ground truth via `llvm-objdump`
   (`wowbox64.dll`, real `ImageBase=0x4c0000`, NOT the `0x180000000`
   convention that applies to the OTHER stock DLLs -- HANDOFF's own
   `ImageBase 0x180000000` line is per-DLL, always check, don't assume)
   on the exact faulting address (`wowbox64.dll+0xF88BC` -> file addr
   `0x5B88BC`, function `native_pass0`, instruction `ldr w8,[x12]`
   guarded by a `getProtection` call -- Box64's dynarec fetching the
   FIRST x86 opcode byte at the code pointer in x12). x12=x20=`0x2`
   exactly == `far=0x2` from the Data Abort -- root cause: `native_pass0`
   reads the thread's Eip **directly from TLS-slot memory**
   (`TlsSlots[WOW64_TLS_CPURESERVED]`'s embedded `I386_CONTEXT`, offset
   +4), NOT via a syscall round-trip -- `nt_build_wow64_teb32` allocates+
   zeroes that block but only ever wrote its `Machine` header field,
   leaving Eip/Esp permanently 0 (`0+2` prefix-skip convention == exactly
   `far=0x2`). Fix: `nt_wow64_cpureserved_set_context` (new, ntsvc.c/h)
   writes the SAME real Eip/Esp/Ebp/CS/EFlags/SS fields the
   `NtQueryInformationThread(ThreadWow64Context)` GET path already
   computes, called once from `nt_boot_process` right after
   `nt_wow64_set_initial_context` (same call site convention).

**Live result after the fix (`scratch/pe10_probe.py`, isolated,
GAIA_QEMU_MEM default 4G, both LOW+HIGH territory pools intact from
leg-9/10, zero exhaustion lines observed): `native_pass0` now decodes
REAL x86 bytes from `hello8_x86.exe`'s real entry.** Fault-dump registers
prove it directly: `x19=x20=0x47661000` (== the exact `exe_entry` value
our own `CHALICE: x86 guest initial context eip=0x47661000` line prints),
`x25=0x47661001` (entry+1 -- one byte, `0x55`/`push ebp`, the exe's real
first instruction per `llvm-objdump --triple=i386 hello8_x86.exe`,
consumed and advanced), backtrace `[0]=hello8_x86.exe+0x2000` (inside the
mapped image). **This is the leg's own success bar, met: "x86-side EIP
progression in fault lines counts, dynarec trace counts."**

**New furthest-point fault (analyzed, not a resolvable single line this
leg): `elr=0x0 far=0x0 lr=0x0`, `esr=0x02000000`/`ec=0x0` (ARM64 "Unknown
reason" == Undefined Instruction, NOT a Data/Instruction Abort).** Root
cause quantified: `kmain.c`'s `l1_table[0]` identity-maps physical address
`0x0` as **device nGnRnE** (QEMU virt's unimplemented-device stub, which
silently returns `0x00000000` on read rather than bus-faulting) -- so a
`br`/`blr` through a NULL function pointer lands on a real, mapped,
zero-filled word, which ARM64 decodes as an Undefined Instruction (EC=0)
rather than an Instruction Abort (which is what an actually-unmapped
address would produce). This is a genuine **null entry in wowbox64.dll's
own internal opcode-dispatch table** (the decoder correctly consumed
`0x55`/`push ebp` -- x25 proves it -- then presumably reached the next
opcode byte, `0x89`/`mov r/m32,r32`, 2-byte ModRM form, and its handler
slot in Box64's internal jump table is null in THIS build), not a kernel
bug -- reverse-engineering which exact table/offset needs the entry is
binary-level work inside a large stripped DLL (no Box64/Hangover source
available, unlike the vanilla-Wine files this floor has been grounding
against) -- genuinely the HANDOFF "blocked >2 cycles" stopping point, now
on a THIRD distinct fault category this leg (syscall-answer gap ->
TLS-memory-seed gap -> binary opcode-table gap), each one real forward
motion, analyzed and documented rather than guessed past.

**No-regression:** `pe9_gate.py` (both backends, all required lines) +
`pe8_gate.py` standalone + `scratch/ramfb_gate.py` all verified PASS live
at HEAD post-fix. qemu leak-checked (pgrep+kill) before/after every boot
this leg (network fetch of vanilla `wine/dlls/wow64/syscall.c`+`include/
winternl.h`+friends done read-only via `curl` this leg for THREADINFOCLASS
grounding -- no local `wine/` donor clone present this session, HANDOFF's
"main working copy has it" was not true this leg; fetched the specific
files needed instead, same grounding standard, no guessing).

**Exact next step:** with real x86 execution now proven reachable, the
remaining path to a full `BTCpuSimulate` entry / `hello8_x86` print needs
BOX64 OPCODE-TABLE reverse engineering (not more kernel-side syscall/TEB
work) -- disassemble `wowbox64.dll`'s dispatch structure around
`native_pass0`/`dynarec64_000`'s handling of `0x89` (and likely several
following opcodes) to find whether the null slot is a genuinely-missing
opcode (unlikely for something as common as `mov r/m32,r32` in a real
Box64 build -- more likely a **table/base-pointer setup step this floor
still hasn't wired**, e.g. a per-process JIT code-cache or opcode-table
init call BTCpuProcessInit makes via ANOTHER unimplemented syscall not
yet hit because this is the first opcode past the entry) -- instrument
further (add a table dump/trace around x8=`wowbox64.dll+0x805D38`, the
global/table base register live at the fault) before assuming a specific
fix. `pe10` (isolated, opt-in) remains the harness -- never wire into
`pe9_gate.py` until it stops crash-faulting.

## CHALICE C4 state refresh (2026-07-18, leg 12 -- HEAD `<fill after final commit>`)

**Null opcode-table-region fault (leg-11's furthest point) survived all 4
ordered hypotheses -- cheap ones DEAD/RECORDED, source grounding narrowed
the fault to Box64's own JIT code-cache region (not a static dispatch
table), binary RE confirmed the region's nature. No fix landed this leg --
two new REUSABLE seams did (backend A/B override, GAIA_QEMU_CPU), both
verified zero-regression. Genuinely blocked past hypotheses 1-4 per
HANDOFF's own stopping doctrine.**

1. **QEMU_CPU (DEAD, proven, not guessed):** `build.sh` `-cpu cortex-a72`
   parameterized to `GAIA_QEMU_CPU` (default unchanged). `pe10` run with
   `GAIA_QEMU_CPU=max`: banner (`[BOX64] Dynarec for ARM64, with extension:
   ASIMD`) and the FULL fault register dump (`elr=0x0 far=0x0`, every
   general register) came back **byte-identical** to the `cortex-a72`
   baseline -- `-cpu max` changes nothing Box64 observes. Root cause found
   in hypothesis 3 (below): Box64-for-WOW64 never reads ID registers or
   `getauxval` directly at all -- `-cpu`'s effect on those is moot for this
   codepath.
2. **BACKEND A/B (RECORDED, not chased):** new reusable seam --
   `nt_wow64_set_backend_override`/`nt_wow64_is_active`-style per-boot flag
   (`ntfile.c`/`ntfile.h`) redirects the real boot path's own literal
   `NtOpenFile("wowbox64.dll")` (wow64.dll's hardcoded default backend
   name -- the REAL boot path, unlike `nt_boot_wow64_probe`, never took a
   backend parameter before this) to any other on-disk name. Wired to
   `pe10 fex` (2nd shell token) -> `libwow64fex.dll`; default `pe10` (no
   token) unchanged/verified identical. **libwow64fex.dll produces a
   COMPLETELY DIFFERENT fault**, not the wowbox64 null-region pattern: two
   `CHALICE: svc id=107 UNIMPLEMENTED` lines (`NtAllocateVirtualMemoryEx`,
   `wine-arm64-syscalls.txt:103`, this floor has never wired the `Ex`
   variant -- only plain `NtAllocateVirtualMemory`) immediately followed by
   a genuine Data Abort (`esr=0x96000021`/`ec=0x25`, NOT ec=0) **inside
   ntdll.dll itself** (`elr=ntdll.dll+0x2818C`, `far=0x7`, backtrace through
   `wow64.dll+0x40000`/`libwow64fex.dll+0x31831A`) -- FEX's own heap-growth
   path calls the `Ex` allocator variant where Box64 called the plain one,
   and crashes inside ntdll's OWN heap code on the small-int deref, not
   inside the backend DLL. Real, useful floor-vs-backend intelligence
   (confirms the two backends exercise genuinely different NT surface) --
   per this leg's own instruction, NOT chased into a fix (would mean
   implementing `NtAllocateVirtualMemoryEx` + auditing ntdll's alternate
   heap-growth path for a backend this leg isn't targeting).
3. **SOURCE GROUNDING (real progress, no clean single-line fix found):**
   cloned `github.com/ptitSeb/box64` as a new gitignored donor quarry
   (`box64/`, `.gitignore` += `box64/`, same convention as `wine/`/
   `reactos/`/`proton/`) -- **checked out the EXACT matching tag `v0.4.2`**
   (our binary's own banner: `WowBox64 arm64 v0.4.2`; repo `git ls-remote
   --tags` confirmed `v0.4.2` exists, fetched depth-1 by tag, not guessed
   from HEAD/main which is already `v0.4.3`).
   - **`DetectHostCpuFeatures` ground truth** (`src/os/hostext_wine.c:8`,
     the WOW64/Windows-build variant, distinct from the Linux one): sets
     `cpuext.asimd=1` unconditionally, then ONLY gates
     aes/sha/pmull/crc32/atomics via `IsProcessorFeaturePresent` (a real
     Windows API that reads `KUSER_SHARED_DATA.ProcessorFeatures[]`, a
     fixed-offset array this floor's `nt_kuser_init` never populates --
     stays zero, all false) and flagm/frintts/rndr/afp/uscat via
     `RegQueryValueExA(HKLM\HARDWARE\DESCRIPTION\System\CentralProcessor\0,
     "CP 4030"/"CP 4039"/"CP 403A")` -- literal raw
     ID_AA64ISAR0_EL1/MMFR1_EL1/MMFR2_EL1 dumps Windows-on-ARM64 exposes as
     registry values, which this floor's registry stub
     (`NtOpenKey`->`STATUS_OBJECT_NAME_NOT_FOUND`) can never answer. **This
     is WHY `-cpu max` is definitively a dead end**: nothing in this path
     reads CNTFRQ/ID-registers/getauxval directly -- it's 100% mediated
     through two NT-level answers (KUSER_SHARED_DATA + registry) this
     floor doesn't supply, independent of the underlying QEMU vCPU's real
     capabilities. (Not fixed this leg -- would need KUSER_SHARED_DATA.
     ProcessorFeatures population + real registry CP-xxxx value floor,
     both nontrivial, deferred, not required for the goal below anyway
     since the fault isn't feature-gated, see next bullet.)
   - **Opcode dispatch is NOT table-indexed** (ruled out the leading
     theory): `dynarec64_00`'s `case 0x89` (`src/dynarec/arm64/
     dynarec_arm64_00.c:1577`, `MOV Ed,Gd`, exactly the 2nd instruction of
     `hello8_x86.exe` -- `llvm-objdump --triple=i386 hello8_x86.exe`:
     `401000: 55 pushl %ebp` / `401001: 89 e5 movl %esp,%ebp`, MODREG path,
     no feature-gated memory/SIMD codegen at all) is reached via a plain
     direct function CALL from `native_pass` (`src/dynarec/
     dynarec_native_pass.c:268`, `dynarec64_00(dyn, addr, ip, ninst, rex,
     &ok, &need_epilog)`), selected by prefix flags (`rex.isf0`/`is66`),
     never through an indirect/table branch. `native_pass0`
     (`STEP==0`, `dynarec_arm64_pass0.h`) additionally reduces `EMIT`/etc
     to pure bookkeeping (`dyn->native_size+=4`) -- pass0 doesn't even emit
     or branch through generated code, it's a size-counting dry run.
   - **The one real indirect-dispatch table found, `box64_jmptbl4/3/48/
     default2/default1/default0`** (`src/custommem.c:3046`
     `init_custommem_helper`, called from `BTCpuProcessInit`
     (`wine/wow64/wowbox64.c:274`) which we've already proven executes for
     real): a multi-level jump table used for INDIRECT branch target
     resolution INSIDE already-JIT-compiled code (`native_next` trampoline
     default), gated `if(BOX64ENV(dynarec))`. Traced the flag: `box64env.
     dynarec` is set once by `LoadEnvVariables`/`ApplyEnvFileEntry` early
     in `BTCpuProcessInit`, never modified before `init_custommem_helper`
     OR before `BTCpuSimulate`'s own `if(box64env.dynarec) DynaRun(emu)`
     check -- same flag, same value, no intervening writes -- so if
     `DynaRun`/`native_pass0` genuinely ran (proven, leg-11), this table
     WAS populated too. Ruled out as this fault's cause. (Also confirmed
     the `[BOX64] Dynarec for ARM64...` banner does NOT itself prove
     `box64env.dynarec==1` -- `PrintHostCpuFeatures`, `src/os/
     hostext_common.c:7`, prints unconditionally whenever
     `DetectHostCpuFeatures()` succeeds, independent of the dynarec-enable
     flag; the REAL proof `dynarec==1` is `native_pass0` executing at all,
     independently already established leg-11.)
4. **BINARY RE (last resort, as instructed):** real `ImageBase=0x4c0000`
   (`llvm-objdump -p`), RVA `0x805D38` computed and looked up against
   `llvm-objdump -h`'s section table (`.text` RVA `0x1000`-`0x43b4a4`,
   `.rdata` `0x43c000`-`0x45f234`, `.buildid`/`.data` to `0x461600`,
   `.pdata` starting `0x150c000`, `SizeOfImage=0x1510000`). **RVA 0x805D38
   falls in a ~17.6MB gap between `.data`'s end (`0x461600`) and `.pdata`'s
   start (`0x150c000`) -- reserved by `SizeOfImage` but backed by NO PE
   section** (`llvm-objdump -d` on that address range returns empty, not
   disassemblable code). This is NOT a static function-pointer table in
   `.rdata`/`.data` -- **it's Box64's own JIT CODE-CACHE pool**, carved out
   of the DLL's own declared (but file-unbacked) image span specifically
   so PC-relative/short ARM64 branches from `.text` can reach compiled
   dynablocks without going out of range (a standard box64/JIT technique).
   **Revised fault characterization vs leg-11's own framing**: not a
   "null table slot", but Box64 branching into its OWN not-yet-JIT-compiled
   code-cache slot -- i.e. either the dynablock compile (pass1-3 emission)
   for this address never actually ran before the jump, or the allocator
   picked/returned the wrong slot address. Register evidence supports a
   `ret`-shaped fault, not a `br`-through-null-pointer one: `elr=0x0 AND
   lr(x30)=0x0` simultaneously -- a `br` to a null pointer loaded from
   memory would leave `lr` at whatever it held before (unrelated, usually
   nonzero); both being zero together is the signature of `ret` (PC :=
   x30) executing with x30 itself never set/corrupted to 0, OR of the CPU
   landing on the very first zero-filled word of the unbacked region via
   a computed/table branch whose own source register was itself read as 0.
   Genuinely ambiguous without a labeled/symbolized disassembly of the
   dynablock allocator's own compiled code (no debug info in the shipped
   DLL) -- would need either (a) building box64 v0.4.2's WOW64 target
   OURSELVES from the now-available donor source to get a symbol-mapped
   binary to diff against, or (b) much deeper blind ARM64 RE of the
   allocator/dispatcher code paths in `.text` that reference this region.
   Both are real, bounded next steps but neither is a same-leg-budget
   binary-RE-only task per HANDOFF's own doctrine ("document findings,
   stop").

**Two reusable seams landed this leg (source-only, both default-unchanged,
zero regression):**
- `build.sh`: `GAIA_QEMU_CPU` (default `cortex-a72`).
- `ntfile.c`/`ntfile.h`: `nt_wow64_set_backend_override(name)` -- real
  boot-path CPU-backend A/B seam, wired to `pe10 fex`.
- `box64/` donor quarry cloned+gitignored (checked out `v0.4.2` to match
  the shipped binary exactly) -- available for the next leg's grounding
  without a re-clone.

**No-regression:** `pe9_gate.py` (both backends, all required lines) +
`pe8_gate.py` standalone + `scratch/ramfb_gate.py` all verified PASS live
at HEAD. qemu leak-checked (pgrep+kill) before/after every boot this leg,
never run naked, GAIA_NO_RUN=1 for every build-only check.

**Exact next step:** the null-region fault needs either (a) a
self-built, symbol-mapped box64 v0.4.2 WOW64 target (now trivial to attempt
-- donor source + exact tag are in-tree at `box64/`, CMake option `WOW64
AND ARM_DYNAREC`, `CMakeLists.txt:1610`) to get a labeled disassembly of
the allocator/dispatcher code around the code-cache region for a byte-exact
diff against the shipped `wowbox64.dll`, or (b) continued blind ARM64 RE of
`.text` code paths that compute addresses into the `0x461600`-`0x150c000`
gap (search for ADRP/ADD sequences targeting that RVA range, find the
CALLER of the fault site, not just the fault site itself -- leg-11/12 never
located the caller). Both bounded, neither attempted this leg (source
grounding + binary RE-of-fault-site only, per doctrine's stated order) --
genuinely blocked past all 4 hypotheses this leg's own instructions gave,
stopping per HANDOFF's "blocked >2 cycles" rule with the analysis
documented above rather than guessing a kernel-side patch.

## Immediate next step for a fresh session

Same fork point as leg-8 (superseded by leg-9-11's WOW64/dynarec work,
kept for history): `hello7`/`pe8`'s widened KERNEL32.dll surface walk is
green -- GetCommandLineA/GetModuleHandleA/GetProcAddress/GetProcessHeap+
HeapAlloc+HeapFree/GetCurrentProcessId/GetLastError+SetLastError all
proven. Every gap from here shows itself as a legible `CHALICE FAULT` /
`NtRaiseException` line resolved to `<module>+offset` — instrument-first,
measure, then fix. **Current real next step is C4-11's own "Exact next
step" just above -- Box64 opcode-table gap, not a syscall gap.**
