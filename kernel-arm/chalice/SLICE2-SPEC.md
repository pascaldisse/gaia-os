# CHALICE SLICE 2 SPEC — NT service dispatch + TEB/PEB floor

Anchor: `kernel-arm/chalice/HANDOFF.md` — READ FULLY FIRST (contract, donor
file:line receipts, framing note). Register = OS-dev vocabulary ONLY (implement
NT service ABI · resolve IAT · map PE sections · service table dispatch).
Donors read-only quarry: `wine/`, `reactos/` at repo root.

Two serial atoms. A = this wave. B = next wave (do NOT start B in A).

---

## Atom A — dispatcher + TEB/PEB + console gate

### A1 `kernel-arm/chalice/ntids.h` (NEW, shared kernel↔PE-stub)
- Table-0 service ids, stable enum, order = HANDOFF boot list. Id 0 up.
- NTSTATUS consts: `STATUS_SUCCESS 0` · `STATUS_NOT_IMPLEMENTED 0xC0000002` ·
  `STATUS_INVALID_SYSTEM_SERVICE 0xC000001C`.
- TEB slot convention: `+0x370` service-table ptr · `+0x378` syscall_frame ptr ·
  `+0x380` dispatcher entry ptr (our slot — VERIFY stub shape vs
  `wine/dlls/ntdll/unix/signal_arm64.c:1630-1785`; if donor pins a slot, match
  donor + note deviation in HANDOFF).

### A2 `kernel-arm/zoneA/ntdisp.S` (NEW) — dispatcher, no trap, one addr space
- Entry via `blr` from PE stub. `x8` = id (low 12 bits) | table idx (bits 12:13).
- Build `syscall_frame` 0x330 B on kernel-side per-call stack, layout EXACT per
  HANDOFF (x0..x28@0, fp/lr/sp/pc@0xe8, cpsr@0x108, id@0x120, fpcr/fpsr@0x128,
  v0..v31@0x130). Store frame ptr → `[x18+0x378]`.
- id ≥ ServiceLimit → x0 = STATUS_INVALID_SYSTEM_SERVICE, return.
- Args: x0–x7 pass-through; stack args copied per ArgumentTable bytes (needed:
  NtWriteFile = 9 args). Verify marshal against donor receipt above.
- Save/restore x18 around C handler call (kernel C = aarch64-none-elf, x18
  temp; PE side = win ABI, x18 = TEB, must survive).
- Return via frame->pc (`ret x16` pattern).

### A3 `kernel-arm/zoneA/ntsvc.c` + `ntsvc.h` (NEW)
- `SYSTEM_SERVICE_TABLE` {ServiceTable, CounterTable, ServiceLimit,
  ArgumentTable}; table 0 real, 1–3 limit=0.
- Full boot-list table wired; unimplemented → stub ret STATUS_NOT_IMPLEMENTED.
- REAL in A: `NtWriteFile` (handle == params.hStdOutput → kernel puts;
  IO_STATUS_BLOCK.Information = len, 16 B struct) · `NtClose` (→0) ·
  `NtTerminateProcess` (unwind to PeRun w/ exit code — same mechanism PeRun
  already uses for entry return).

### A4 TEB/PEB floor (ntsvc.c or pemod.c ext)
- Alloc + populate: TEB, PEB, RTL_USER_PROCESS_PARAMETERS, PEB_LDR_DATA + one
  LDR_DATA_TABLE_ENTRY (the image). Offsets hand-mirrored from
  `reactos/sdk/include/ndk/peb_teb.h` (LP64) — receipt offsets in comments.
- 4 trapdoor ptrs (HANDOFF): TEB.NtTib.Self · TEB.ProcessEnvironmentBlock ·
  PEB.Ldr · PEB.ProcessParameters. PEB also: ImageBaseAddress, ProcessHeap
  (dummy), OSMajor=10/OSMinor=0/Build, NumberOfProcessors=1, BeingDebugged=0.
- params.hStdOutput = pseudo handle (pick const, e.g. 0x14; StdOutput @ +0x28 —
  verify vs donor header).
- TEB slots per A1. `pe_enter(entry, teb)` asm shim: set x18=TEB, call entry,
  restore x18 (kernel C may clobber x18 → shim + dispatcher own it).

### A5 test exe `kernel-arm/chalice/hello3.c` + PE-side stubs
- Stubs (zig cc asm, win ABI): `mov x8,#id; ldr x16,[x18,#slot]; blr x16; ret`
  — shape verified per A1.
- hello3 main: walk x18→TEB→PEB→ProcessParameters→hStdOutput (exercises
  trapdoors), NtWriteFile("CHALICE: the cup speaks\n"), NtClose,
  NtTerminateProcess(-1, 99).
- `mkpe.sh` builds hello3.exe · `build.sh` embeds pehello3 · `pe` verb runs pe3.

### A gate
- Gate A: clean build. Gate B serial log: prior four verbs green (`zc:
  GaiaMod=100` · say love=1 · aot · pe1 ret=42 / cup fills / pe2 ret=7) PLUS
  `CHALICE: the cup speaks` + `pe3 ret=99`. Ramfb regression:
  `scratch/ramfb_gate.py`.
- QEMU law: NEVER naked — `timeout`-wrap, `< /dev/null`, serial→log.
- Read-the-disk law: read every file before editing it.
- Green → commit `CHALICE C2a: NT service dispatch + TEB/PEB floor + console
  gate` + update HANDOFF State section. Do not push.

---

## Atom B (NEXT wave — blocked on A)
- `NtContinue`: full CONTEXT restore (ARM64 CONTEXT per reactos ndk), loader's
  leap off its own stack. hello3 probe: CONTEXT{pc=part2, sp, x0=cookie} →
  part2 prints "CHALICE: continue lands" → terminate. Slice-2 boss.
- `NtAllocateVirtualMemory`/`NtFreeVirtualMemory`: flat arena, page-align,
  honor *BaseAddress/*RegionSize in-out; COMMIT|RESERVE both = alloc.
- `NtQueryInformationProcess` (ProcessBasicInformation only) ·
  `NtQuerySystemInformation` (SystemBasicInformation only).
- KUSER_SHARED_DATA @ 0x7ffe0000: build.sh `-m 256M` → `-m 1G` (RAM then
  0x40000000–0x80000000, page lands 128K under ceiling); verify mem.c arena
  never reaches it; populate NtMajor/Minor/Build, NumberOfPhysicalPages.
- Gate: pe3 extends w/ continue probe; all prior lines still green.
