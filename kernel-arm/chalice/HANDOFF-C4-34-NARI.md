# CHALICE C4-34 — NARI HANDOFF (forward-only, self-contained)

Floor = GAIA-OS ARM64 kernel implementing the NT host boundary; stock userland
(FEX + Hangover PE) rides on top unmodified. One measurement stands between here
and `pe10 ret=42` (the 32→64 transition proof). Everything downstream is
pre-staged: C4-27 BOP stub built+seeded+intact, `hello9_x86` WriteFile guest
authored (`e977ef0`). Init chain is healthy end-to-end.

## The one open question (C4-33, on disk, gated green — `00d19d9`)

Live floor dump of FEX's `XIntervals` interval list at the self-fault says the
guest entry IS enclosed:

```
XIntervals count=8  [47660000,47665000)=hello8_x86.exe  exe_entry(0x47661000) enclosed=1
SynchronousFaultData=0x00000015020E0B01 = NoExecOp   Eip=exe_entry  pe10 ret=0xC0000005
```

The exec-range gate (`InvalidationTracker::QueryExecutableRange` →
`Decoder::CheckRangeExecutable`, Frontend.cpp:90-108) therefore MUST return true
for `exe_entry` → the frontend cannot emit `NoExecOp` for `exe_entry`. Yet the
fault byte-matches `NoExecOp` (the only PROT|USER|INSTR BreakDefinition,
OpcodeDispatcher.cpp:5018-5024). **⇒ the failing check queried a DIFFERENT
address than `exe_entry`.** That address is the whole ballgame.

Rivals (a) ctor-sweep-aborts / (b) inserted-then-removed / (c) section-walk-differs:
all three REFUTED by the C4-33 receipt. Do not re-chase them.

## Your atom: one receipt that names the queried address → R1 vs R2

Get decode-time visibility of the ACTUAL address `CheckRangeExecutable` tests at
the failing call, plus the `HitNonExecutableRange` flag. Two live hypotheses,
and the receipt splits them cleanly:

- **R1** — the check queries the host `InstStream+offset` pointer (`PeekByte`:
  `ByteAddress = InstStream + InstructionSize + Offset`). If this block's
  `InstStream` ≠ the guest linear addr, it lands OUTSIDE `XIntervals` (which
  stores guest addrs) → false → `NoExecOp`, while the reconstructed guest `Eip`
  still reads `exe_entry`. Tell: the queried addr is a host-region pointer, not
  `0x47661000`.
- **R2** — a pre-insert warm-up decode: the check ran once BEFORE the interval
  was live. Tell: queried addr == `exe_entry` but at an earlier timestamp than
  the interval insert.

Measure first. Do not spec the fix until the receipt names R1 or R2.

## Two roads — run the cheap one first

### Road A (cheapest, and pays forward): turn FEX's own narration on
`FEX::Windows::Logging::Init` (Logging.cpp:37-47) emits nothing on our floor even
though the hardcoded banner `__wine_dbg_output("starting FEX…")` prints fine
(transcript line 559). With narration live, FEX's own `DFmt/IFmt` lines report
decode + interval state directly — no offset-hunting, and every future leg gets
eyes for free. Two candidates, both floor-side and checkable:
- (i) `SilentLog()` returns true — `FEX_SILENTLOG=0` not honored (env-var
  conversion / `_environ` wiring on our floor). Check the env path first.
- (ii) `WineDbgOut = GetProcAddress(GetModuleHandleA("ntdll.dll"),
  "__wine_dbg_output")` came back null. Argued unlikely — the same
  `LdrGetProcedureAddress` path is proven working later (Module.cpp:527/550,
  C4-27/29) — but confirm by reading the resolved pointer at Init time.

### Road B (fallback / parallel): read the per-thread Decoder state directly
The proven C4-33 technique reaches any FEX-internal object without a donor edit:
1. `objdump -t libwow64fex.dll` — NOT stripped, full COFF symtab (methods
   `_ZN…InvalidationTracker…`, `WowSyscallHandler::…`, `BTCpuProcessInit`, …).
2. For an anon-namespace/static object, disassemble a consumer method and read
   its `adrp/add` pair for the address (exactly how C4-33 recovered
   `&InvalidationTracker = ImageBase 0x180000000 + RVA 0x34c430` from
   `QueryGuestExecutableRange`).
3. The per-thread `Decoder` hangs off the Thread reachable via `TlsSlots[16]`
   (proven live C4-22/29). Walk to its `Decoder` member, then read
   `InstStream` / `HitNonExecutableRange` / `ExecutableRangeBase` /
   `ExecutableRangeEnd` (Frontend.h:123-124) at the self-fault.
4. Read floor-side in `kmain.c` `sync_try_guest_exception` via
   `nt_image_base_by_name(...)` + arena-aware `nt_fault_read32` (heap is
   ≥4GB reserve-commit). Gate the dump once (`static int … _dumped`).

## After the receipt names R1/R2 → THEN spec the fix
- If **R1**: the exec-check is testing a host pointer against a guest-addr
  interval list. Spec the smallest floor-side correction that makes the block's
  `InstStream` and the guest linear addr agree at query time (our address spaces
  are 1:1 — establish WHY they diverge for this block before touching anything).
- If **R2**: an ordering gap — the interval must be live before the first decode
  of that range. Spec the floor sequencing that guarantees insert-before-decode.

Either way: measure → name → smallest change → gate. No speculative edits.

## Laws (non-negotiable — enforced every leg)
- **Single lane owns kernel edits + QEMU boots** (one checkout, single-socket
  harnesses). You are that lane. Recon/spec/other work stays new-files or
  read-only so nothing collides.
- **Source-only commits.** `.exe/.dll/.pdb/pehello_bin.c` build-artifact churn
  stays UNTRACKED — never stage it. (`hello9_x86.exe/.pdb` currently untracked =
  correct.)
- **Checkpoint-commit before every boot loop.** A wall/drop mid-run then loses
  nothing; on resume, check disk before re-running.
- **Gate every change, no regression:** `pe8_gate.py` ret=78 · `pe9_gate.py`
  both backends (wowbox64/fex) ret=0 · `ramfb_gate.py` 7/7. Instrumentation-only
  passes must leave `pe10 ret` identical before/after.
- **Receipts on disk, not memory.** Quote the transcript/commit, not recall.
- **The committed `map-fex-offsets.sh` runs on every new fault offset** — each
  step self-diagnoses to a named function. Keep the loop automated.

## Done-line
name R1/R2 (this atom) → land the smallest fix → `NoExecOp` gone → PeMain's
bytes execute → its `ret` lands in the waiting C4-27 BOP stub → **`pe10 ret=42`
= the 32→64 transition proven** → swap in `hello9_x86` → WriteFile marker prints
= the milestone → C3 graphics opens (C3-SPEC.md, Venus road) for the target apps.

Current STATE-CURRENT + full receipts: `HANDOFF.md` §C4-33, `FEX-MAP.md` §C4-33.
