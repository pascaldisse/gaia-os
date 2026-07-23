# CHALICE HANDOFF — Windows-ABI compatibility layer for GAIA-OS

## C4-33 — LEAD 2 EXECUTED; the C4-31/32 central thesis OVERTURNED by live receipt: `XIntervals` **DOES enclose** `exe_entry(0x47661000)` at the exact `NoExecOp` self-fault. All three rival theories REFUTED. New paradox named. Runtime `<see commits>`; live 2026-07-23.

- **LEAD 2 done — live `XIntervals` dumper wired floor-side** (`kmain.c` `sync_try_guest_exception`, new `CHALICE C4-33` receipt, gated once). FEX's global `std::optional<InvalidationTracker> InvalidationTracker` (Module.cpp:130, anon ns — NOT in symtab) address DERIVED, not guessed: libwow64fex.dll is **NOT stripped** (COFF symtab present) → disasm of `WowSyscallHandler::QueryGuestExecutableRange` (RVA 0x53f8): `adrp x0,0x18034c000 ; add x0,x0,#0x430` → `&InvalidationTracker = ImageBase(0x180000000)+0x34c430`. std::optional stores value inline; `XIntervals` is member 0 (InvalidationTracker.h:55) → the libc++ `std::vector<Interval>` {begin@+0,end@+8,cap@+16}, `Interval={u64 Offset,u64 End}` (IntervalList.h:16-23) sits AT the object. Live base via `nt_image_base_by_name("libwow64fex.dll",…)`; heap pointers read via arena-aware `nt_fault_read32`.
- **RECEIPT (pe10 fex, same self-fault event as C4-30/31, one transcript)**: `ivt=0x4DD8C430 begin=0x230040180 end=0x230040200 count=8` → intervals `[47020000,47660000)`(native ntdll) **`[47660000,47665000)`(hello8_x86.exe — ENCLOSES exe_entry)** `[48690000,488BD000)`(ntdll32) `[4C910000,4CA30000)`(wow64) `[4DA40000,4DDA2000)`(libwow64fex) `[4EDC0000,4EEF0000)`(wow64win) `[4FF00000,50030000)`(win32u) `[58040000,58041000)`(BOP) → **`exe_entry(0x47661000) enclosed=1`**. Correlated same run: `C4-30 ORIGINAL FAULT elr=0x26D0C02B0`, `C4-31 SynchronousFaultData raw=0x00000015020E0B01`=`NoExecOp` (Signal=SIGSEGV TrapNo=PF err_code=PROT|USER|INSTR) byte-identical, `Eip=exe_entry Eax=0`, `pe10 ret=0xC0000005` unchanged.
- **RIVAL VERDICTS (all REFUTED by this receipt)**: (a) ctor `VirtualQuery` sweep aborts early → NO (8 complete module intervals). (b) interval inserted-then-REMOVED by a later notify/flush → NO (present, full 0x5000 span, at fault time). (c) `HandleImageMap` section-walk reads our PE map differently → NO (interval present, correct base+span). **The C4-31/C4-32 sharpened gap ("`InvalidationTracker` never registers `exe_entry` / `XIntervals` doesn't enclose by decode time") is OVERTURNED** — it does enclose.
- **decode-gate re-read confirms the contradiction**: `InvalidationTracker::QueryExecutableRange` (InvalidationTracker.cpp:177-190) — `XIntervals.Query(exe_entry).Enclosed==true` → returns non-zero `Size` (RWX path can only shrink, not zero it) → `Decoder::CheckRangeExecutable` (Frontend.cpp:90-108) `if(Size==0)return false` NOT hit → returns **true** → `ReadByte`/`PeekByte` never set `HitNonExecutableRange` → the frontend **cannot emit `NoExecOp` for `exe_entry`**. Yet the self-fault byte-matches `NoExecOp` (OpcodeDispatcher.cpp:5018-5024, the ONLY BreakDefinition with err_code PROT|USER|INSTR). **NEW PARADOX, receipted.**
- **new frontier (named, not yet chased)**: NoExecOp fired for an address that is NOT XIntervals-enclosed, yet `WowContext.Eip`/guest CTX report `exe_entry` (which IS enclosed). Sharpest live hypotheses: (R1) the failing `CheckRangeExecutable` queried the decoder's **host `InstStream` pointer** (`PeekByte`: `ByteAddress=InstStream+InstructionSize+Offset`), which — if FEX's WOW64 block was set up with `InstStream` ≠ the guest linear addr (not 1:1 for this block) — lands outside XIntervals (which stores GUEST addrs) → false → NoExecOp, while the reconstructed guest `Eip` still reads exe_entry; (R2) a pre-insert warm-up decode. Resolving needs decode-time visibility (LEAD 1 narration, or reading the per-thread `Decoder` state `HitNonExecutableRange`/`ExecutableRangeBase`/`InstStream` — reachable next pass now that libwow64fex symbols are known).
- **LEAD 1 (voice) — still dead, narrowed, NOT fixed**: banner `__wine_dbg_output("starting FEX…")` works (transcript line 559, direct import via unixlib `nt_unix_dbg_write`) but ZERO `LogMan` `D/I/E`-prefixed lines anywhere → `FEX::Windows::Logging::Init` (Logging.cpp:37-47) emits nothing. Two candidates, could not isolate without FEX-internal visibility: (i) `SilentLog()` returns true (FEX_SILENTLOG=0 not honored — env conversion/`_environ`); (ii) `WineDbgOut=GetProcAddress(GetModuleHandleA("ntdll.dll"),"__wine_dbg_output")` null. Argued (ii) unlikely: the SAME `LdrGetDllHandle`/`LdrGetProcedureAddress` path is proven working later (Module.cpp:527/550 — `LdrSystemDllInitBlock`/`__wine_unix_call_dispatcher` both resolved, C4-27/29). Left for next pass; not a speculative floor edit this pass.
- **ladder**: (a) intervals dumped ✓ **and proven to enclose exe_entry** ✓ (exceeds the brief's (a)); (b) NoExecOp NOT gone — now a receipted paradox, not a missing-interval fix; (c)/(d) unreached. `pe10 ret=0xC0000005` unchanged.
- **re-gate, all green, no regression**: `pe8_gate.py` ret=78 · `pe9_gate.py` both backends (wowbox64/fex, both ret=0) · `ramfb_gate.py` 7/7. Instrumentation-only (one new receipt block); `pe10 ret` identical before/after.
- **commit this pass**: `kernel-arm/zoneA/kmain.c` (new `CHALICE C4-33` XIntervals dumper). Full derivation + receipts → FEX-MAP.md § **C4-33**.

## C4-32 — the "tell FEX the exe is executable" HYPOTHESIS TESTED AND REFUTED: this floor already tells it, twice, via donor-real self-registration `BTCpuProcessInit` performs on its own (PEB.ImageBaseAddress-based `HandleImageMap` + `InvalidationTracker`'s own constructor `VirtualQuery` sweep) — NOT the `BTCpuNotifyMapViewOfSection` export the brief's hypothesis pointed at (that export is real, but is for *subsequently-mapped* DLLs, not the main exe, which uses a different, PEB-based donor mechanism this floor already satisfies correctly). Live receipt (this floor's own `NtQueryVirtualMemory` log): the exe's full image range answers `branch=image state=MEM_COMMIT` with (unconditional) `PAGE_EXECUTE_READWRITE`, well before the fault — structurally sufficient for FEX's own `InvalidationTracker` to register `exe_entry` as executable through its EXISTING, already-firing code paths. No fix wired — wiring the notify export anyway would be redundant/speculative given this evidence, exactly what this project's law forbids. The real gap (why `XIntervals` doesn't enclose `exe_entry` by decode time despite two receipted insert calls) is sharper but still unresolved, FEX-internal; a promising, cheap next lead surfaced (FEX's own `LogMan` narration is silently going nowhere despite C4-15's `FEX_SILENTLOG=0` fix — `WineDbgOut` resolution appears to be failing specifically inside `Logging::Init`, unlike the identical-looking hardcoded banner call). Zero source changes this pass (doc/measurement only); `pe10 ret=3221225477` unchanged. Full donor citations, byte-level receipts → FEX-MAP.md § **C4-32**. Live 2026-07-19.

## C4-31 — the consume-check DONOR-TRACED end-to-end; its own input state PROVEN correct+present at the C4-30 self-fault (byte-exact match to FEX's `NoExecOp` `BreakDefinition` -- Signal=SIGSEGV, TrapNo=PageFault, err_code=PROT|USER|INSTR) -- REFUTES both suspects this task posed (A: PC/field-match failure; B: missing thread-state) -- new verdict: the consume-check WORKS AS DESIGNED (correctly translates this self-fault into a real x86 `STATUS_ACCESS_VIOLATION(EXECUTE_FAULT)` at `RIP=exe_entry`), which then correctly, by Windows' own design, goes UNHANDLED at guest SEH (hello8_x86.exe has zero SEH handlers) -- NOT an exception-delivery/consume bug. The REAL gap, newly named: FEX's `InvalidationTracker` never registers `exe_entry` as executable, despite every checkable input (PE headers, section `IMAGE_SCN_MEM_EXECUTE` flag, `PEB.ImageBaseAddress`, this floor's own `NtQueryVirtualMemory` reporting EXECUTE, image-registration ordering) looking correct -- named, not chased further this pass (donor-internal state, unreachable without a donor edit). No fix attempted (new frontier, not a small named-field gap; time-boxed). Runtime `<see commit below>`; live 2026-07-19.

- **MEASURE 1, the consume-check itself, donor-traced end-to-end**: `BTCpuResetToConsistentStateImpl` (`FEX/Source/Windows/WOW64/Module.cpp:871-951`), invoked by wow64.dll's `Wow64PrepareForException` -- confirmed the ONLY such mechanism for the WOW64 build (no `RtlAddVectoredExceptionHandler` registered anywhere in `Source/Windows/WOW64/`; `FEX::DummyHandlers::DummySignalDelegator` installs no host signal handler at all, WOW64-only vs ARM64EC). Gate: `if (!Thread || !IsAddressInJit(Context->Pc)) return false;` -- `IsAddressInJit` checks `Config.DispatcherBegin/End` (= `Dispatcher::Start/End`, set to span the WHOLE `EmitDispatcher()` emission, which structurally includes `GuestSignal_SIGSEGV`'s own address by donor emission order, `Dispatcher.cpp:61/417/597-598`) OR the JIT code-buffer. Past the gate (both AV-specific fast-paths declined and Thread/Pc both valid): `Thread->CurrentFrame->SynchronousFaultData` is read and handed to `FEX::Windows::HandleGuestException` (`Source/Windows/Common/Exception.h:12-79`) -- **this IS the actual field-translation step this task's brief called "the consume-check"** -- it builds a REAL x86 `EXCEPTION_RECORD` from `Fault.Signal`/`TrapNo`, then `memcpy(Context, TLS.EntryContext(), ...)` (`:947`) replaces the host ARM64 context with the `BTCpuSimulate`-entry anchor -- **the donor-proven ORIGIN of every prior leg's `BTCpuSimulate+0x10` RAISE#1 identity** (C4-18/21/24/26/27/29), a normal side effect of this path SUCCEEDING, not evidence of an unclaimed fault.
- **MEASURE 2b, the consume-check's own input state, LIVE-READ from our floor** (new `kmain.c` receipt, `CHALICE C4-31`, hand-derived offsets -- `InternalThreadState::CurrentFrame` is member 0; `CpuStateFrame::SynchronousFaultData` is offset `0x5D8` = `sizeof(CPUState)=0x5C0` + 3 fields, rounded to 8): live capture at the SAME C4-30 self-fault event -- `CurrentFrame=0x270001140 raw=0x00000015020E0B01 FaultToTopAndGeneratedException=1 Signal=0xB(=11=FAULT_SIGSEGV) TrapNo=0xE(=14=X86_TRAPNO_PF) si_code=2 err_code=0x15(=PROT|USER|INSTR)` -- an EXACT, unambiguous, byte-for-byte match to `OpDispatchBuilder::NoExecOp`'s own `BreakDefinition` (`OpcodeDispatcher.cpp:5018-5024`; the only other three `FAULT_SIGSEGV` sites all use `TrapNo=GP/OF`, so this match cannot be a coincidence) -- **first-time identification of the exact x86 frontend site**, resolving C4-30's own flagged "which exact site fired is UNVERIFIED" question: it's `NoExecOp` ("instruction fetch from non-executable guest memory"), firing at `WowContext.Eip=0x47661000=hello8_x86.exe+0x1000=exe_entry` (matches C4-25/C4-30's own already-captured `GUEST CTX Eip` receipt exactly).
- **verdict vs this task's own A/B**: neither, as framed. B (missing state) directly REFUTED -- state is present, correctly written, correctly readable, and names exactly which donor code path fired. A (PC/field-match) not found either -- Pc is in-range by construction, Thread is proven non-null by the very fact the SynchronousFaultData read succeeded through the same pointer chain. New, evidence-driven verdict: the consume-check translates this fault CORRECTLY per FEX's own `X86_TRAPNO_PF` handling (`Exception.h:63-68`, leaves `ExceptionCode` as delivered -- matches this pass's own observed `pe10 ret=3221225477`=`0xC0000005` exactly, and C4-29's identical observation) -- the resulting real x86 AV is then genuinely unhandled at the guest SEH level because `hello8_x86.exe` (a bare 10-byte function, zero SEH frames) has no handler -- this is correct Windows behavior for that exception, not a floor bug.
- **the real, newly-named gap**: WHY `FEXCore::Frontend::Decoder::CheckRangeExecutable`/`InvalidationTracker::QueryExecutableRange` (`InvalidationTracker.cpp:177-190`, backed by an `XIntervals` interval tree populated at `HandleImageMap`'s PE-section walk + the tracker's own constructor-time whole-address-space `VirtualQuery` walk) believes `exe_entry` is not executable, despite EVERY checkable input this pass verified looking correct: `hello8_x86.exe`'s `.text` genuinely has `IMAGE_SCN_MEM_EXECUTE` (`llvm-readobj -S`, confirmed), `PeMapImage` copies PE headers+section table byte-verbatim (`pemod.c:119-171`), `PEB.ImageBaseAddress` is correctly wired (C4-29), this floor's own `NtQueryVirtualMemory` reports `PAGE_EXECUTE_READWRITE` for the whole registered image (`ntfile.c:1273-1339`, `if (img)` branch), and `nt_image_register` for the exe runs during Phase-0, provably before FEX/wow64.dll ever loads. All remaining candidates live inside FEX's own `InvalidationTracker`/`IntervalList` internals -- unreachable from this floor's own memory without a donor edit (project law). **Named, not chased further** -- next worker's starting point; the entire consume-check chain is now fully donor-traced so no re-derivation is needed.
- **re-gate, all green, no regression**: `pe8_gate.py` ret=78 PASS; `pe9_gate.py` PASS both backends (wowbox64/fex, both ret=0); `ramfb_gate.py` PASS (7/7 pixel checks). Instrumentation-only change (one new receipt block in `sync_try_guest_exception`, reusing the existing `nt_fault_read32` safe-probe reader for arena-resident addresses instead of a raw bounds check) -- `pe10 ret=3221225477` (`=0xC0000005`) unchanged before/after.
- **commit this pass**: source-only, `kernel-arm/zoneA/kmain.c` (new `CHALICE C4-31 SynchronousFaultData` receipt in `sync_try_guest_exception`, reading `Thread->CurrentFrame->SynchronousFaultData` via the already-live `TlsSlots[16]` chain). Full donor citations, byte-level field decode, verdict -> FEX-MAP.md § **C4-31**.

---

## C4-30 — the ORIGINAL fault, fully explained end-to-end for the first time: it is FEX's OWN deliberate GuestSignal_SIGSEGV self-fault mechanism ("force a SIGSEGV by loading zero", Dispatcher.cpp -- donor-cited, byte-matched exactly), reached via IR::Break(Signal=FAULT_SIGSEGV) inside FEX's own fixed dispatcher-buffer code (size 0x4000, exactly MAX_DISPATCHER_CODE_SIZE) -- NOT a triage regression, NOT the C4-27 stub/seeding, NOT a missing wow64/ntdll-init service. Same pre-existing architectural gap 6 prior legs named (FEX-internal signal marker misrouted through this floor's full NT SEH cycle instead of an internal short-circuit) -- but for the first time the ORIGINAL pre-reset fault is captured and precisely identified, not just the post-reset BTCpuSimulate+0x10 anchor RAISE#1 already showed. No fix attempted (same out-of-scope design ruling C4-18/21/24/26/27/29 all deferred to Pascal). Runtime `ec45bd6`; live 2026-07-19.

- **MEASURE step 1, answered live**: extended `kmain.c`'s `sync_try_guest_exception` (our own floor's HW-fault-to-guest-NT-exception delivery point, fires BEFORE any guest/FEX code ever sees the fault) with a `CHALICE C4-30 ORIGINAL FAULT` receipt -- raw esr/ec/status/elr/far/wnr (`(esr>>6)&1`, ARM ARM D13.2.37) -- plus `nt_fault_region` classification of BOTH elr and far (reused existing classifier, ntfile.c). Live capture (`pe10 fex`): `esr=0x96000006 ec=0x25 status=0xC0000005 elr=0x26D0C02B0 far=0x0 wnr=0` (a READ, not a write) -- elr-orig classifies `reserve-commit-arena committed base=0x26D0C0000 size=0x4000`; far-orig classifies `outside-known-allocators` (a genuine, unambiguous NULL pointer -- not a demand-commit-arena address misclassified, not a small offset-from-null sentinel like C4-28's far=0x42).
- **the arena base+size IS FEX's own Dispatcher buffer, proven by exact size match**: FEX `Dispatcher::Dispatcher` allocates its one fixed JIT-dispatcher-loop/trampoline buffer via `FEXCore::Allocator::VirtualAlloc(MAX_DISPATCHER_CODE_SIZE, true)` (`Dispatcher.cpp:39`, `constexpr size_t MAX_DISPATCHER_CODE_SIZE = FEXCore::Utils::FEX_PAGE_SIZE * 4` = `0x1000*4 = 0x4000`) -- our arena region's size (0x4000) is an EXACT, non-coincidental match; this is the SAME class of dispatcher-trampoline arena C4-17/18/21/27/28 already independently named (always 0x4000 bytes, always classified reserve-commit-arena, base varies boot-to-boot per this floor's bump allocator).
- **the exact instruction, disassembled + donor-matched**: raw ARM64 word window [elr-32,elr+32) dumped (`nt_fault_read32`, new `CHALICE C4-30 ELR WINDOW` receipt) and hand-decoded (field-by-field bit extraction, cross-checked against ARM ARM encodings): `elr-4 = 0x52800001` = `MOVZ W1, #0` (zeroes X1, standard AArch64 32-bit-write zero-extends to X1); `elr+0 = 0xF9400021` = `LDR X1, [X1, #0]` (size=11 opc=01 imm12=0 Rn=x1 Rt=x1) -- **loads through the register JUST set to zero**. This is a byte-for-byte, instruction-for-instruction match for FEX's own donor source, `FEXCore/Source/Interface/Core/Dispatcher/Dispatcher.cpp` (GuestSignal_SIGSEGV handler, comment verbatim in the donor: "hlt/udf = SIGILL, brk = SIGTRAP, ??? = SIGSEGV, Force a SIGSEGV by loading zero") -- `else { LoadConstant(Size::i64Bit, Reg::r1, 0); ldr(XReg::x1, Reg::r1); }` (the `if (CTX->ExitOnHLTEnabled())` branch, taken when that config is OFF -- the default, confirmed live since we hit the else). Confidence: high, exact instruction match, not a heuristic offset guess.
- **why FEX reaches this trampoline, donor-grounded**: GuestSignal_SIGSEGV is branched to ONLY from IR::Break (`FEXCore/Source/Interface/Core/JIT/MiscOps.cpp:89`, DEF_OP(Break) -- `case Core::FAULT_SIGSEGV: ldr(TMP1, ..Pointers.GuestSignal_SIGSEGV); br(TMP1);`), FEX's mechanism for synthesizing a guest x86 exception from JIT-generated code. IR::Break(FAULT_SIGSEGV) is itself emitted by the x86 frontend (OpcodeDispatcher.cpp) for a short, closed set of reasons: INT n when the literal isn't the syscall gate (0xCD, #GP), INTO (0xCE), HLT (0xF4, deliberately mapped to #GP/SIGSEGV not SIGILL), PermissionRestrictedOp (#GP), or NoExecOp (a non-executable-page instruction FETCH, #PF/SEGV_ACCERR). **Which exact site fired is UNVERIFIED this pass** -- PeMain's own decoded bytes at the boot-seeded Eip (hello8_x86.exe+0x1000, re-confirmed intact this pass, see below) are a benign `push ebp; mov ebp,esp; mov eax,0x2A; pop ebp; ret` (no INT/HLT/UD2 opcodes anywhere in it) -- so if this Break fired while translating THAT exact block it did not come from decoding those bytes; more likely this fires from a DIFFERENT FEX-internal path (an init-time probe/warm-up compile, or a permission/exec-bit check against an address that isn't exe_entry) reached before BTCpuSimulate ever gets to execute PeMain's own bytes. **Next lead, not chased this pass (new instrumentation, time-boxed out)**: `CpuStateFrame::SynchronousFaultData` (`FEXCore/include/FEXCore/Core/CoreState.h:414-421`, `{FaultToTopAndGeneratedException, Signal, TrapNo, si_code, err_code}`, 8 bytes) is written by IR::Break (MiscOps.cpp:64-75) immediately before branching to the signal trampoline -- reading it from OUR floor (via the live TlsSlots[16]/ThreadState pointer already probed, C4-22/23/29) would name the EXACT TrapNo/si_code without touching donor code (no donor edits, per this project's own law) -- a clean next step, not attempted here to respect the wall.
- **MEASURE step 2, I386_CONTEXT + stack/stub receipts**: extended `ntsvc.c`'s existing RAISE#1 guest-content dump (previously gated on `code==0xC000001DUL` only -- now fires on `nt_raise_trace_count==1` regardless of status code, since C4-29's fix changed the terminal code to 0xC0000005). Live: `Eip=0x47661000 (hello8_x86.exe+0x1000) Esp=0x488CC000 Eax=0x0 Ebp=0x488CC000` -- Eax=0 (NOT 0x2A -- PeMain did NOT complete/return; its own `mov eax,0x2A` never executed) and Esp==Ebp== the exact boot-seeded stack-top value (`nt_wow64_cpureserved_set_context`'s own write, unchanged) -- **PeMain never started executing**, consistent with the fault happening upstream of BTCpuSimulate ever dispatching to exe_entry's block. Guest-byte hexdump at Eip re-confirms the same intact, benign 10-byte function C4-26 already found (`55 89 E5 B8 2A 00 00 00 5D C3`).
- **MEASURE step 2, stack + stub-page hexdumps (new this pass)**: [Esp-8,Esp+16) -- word at Esp+0 reads back `0x488D0000`, exactly `nt_wow64_stub_addr` (the C4-27 stub buffer) -- **the stub return-address IS still correctly seeded on the stack**, undisturbed. Stub-page hexdump at that address (written=1, 13/0xD bytes as designed): decoded bytes `50 6A FF B8 2C 00 00 00 E8 F3 FF 76 0F` = `push eax; push -1; mov eax,0x2C; call rel32` -- **byte-for-byte intact**, and the rel32 (0x0F76FFF3) correctly resolves forward to this boot's own live-discovered BOP address (stub_addr+13+rel32 = 0x58040000, cross-checked by hand hex addition) -- **the C4-27 stub is correctly built, seeded, and undisturbed; it is simply UNREACHED**, same verdict C4-27 already reached, now doubly confirmed via a completely different fault identity.
- **MEASURE step 3, demand-commit triage check**: `CHALICE ARENA: ... elr=0x26D0C02B0 far=0x0` -> `nt_arena_contains(far=0)` correctly returns false (0 is not a reserve-commit-arena address, never was) -> `CHALICE ARENA: refused not-arena` -- the demand-commit handler SAW this fault and correctly declined it; **this is NOT a triage regression** (branch (a) from this task's own brief does not apply -- far is a genuine NULL, not a mis-triaged reserved-but-uncommitted arena address).
- **BRANCH verdict vs this task's own (a)/(b)/(c)**: **(a) does not apply** (far is not in the demand arena, correctly refused, not a regression). **(b) does not apply** (the C4-27 stub/Esp-seeding is proven intact and unreached, not the cause -- this fault happens before PeMain's own code ever runs). **(c) partially applies, sharpened**: this is not "an AV inside a wow64/ntdll init path" needing a *named missing service* -- it is AV inside FEX's OWN internal dispatcher-generated code, a DELIBERATE self-fault mechanism (not a floor gap at all on FEX's side) that this floor's general-purpose "forward every unclassified data-abort/hlt/brk to guest NT SEH" design (C4-18) cannot distinguish from a genuine guest AV -- this is the exact SAME already-repeatedly-named architectural gap (C4-18/21/24/26/27/29: FEX's own internal signal-return/exception-synthesis markers, meant to be absorbed by an FEX-internal host-signal short-circuit, are instead routed through this floor's full synthetic NT SEH cycle) -- but for the FIRST time, precisely identified as the GuestSignal_SIGSEGV marker specifically (not the previously-seen SignalHandlerReturnAddress/hlt#0 marker, nor the now-fixed TlsSlots[16]=2 sentinel), fully explaining the previously-opaque post-reset RAISE#1/BTCpuSimulate+0x10 identity end-to-end: our floor delivers this self-fault as a genuine STATUS_ACCESS_VIOLATION -> stock ntdll's KiUserExceptionDispatcher -> dispatch_exception -> wow64's Wow64PrepareForException -> FEX's BTCpuResetToConsistentStateImpl (which memcpy's a NEW reconstructed context anchored at BTCpuSimulate+0x10 over the real one, Module.cpp:947, by FEX's own design) -> guest ntdll calls NtRaiseException AGAIN with that reconstructed identity -> our floor's RAISE#1 only ever saw the anchor, never the original. **This full chain is now proven, not inferred**, for the first time across 6 prior legs.
- **SUCCESS LADDER verdict**: **(a) AV explained** -- ACHIEVED, fully, end-to-end, first time (exact instruction, exact donor mechanism, exact reconstruction chain to the post-reset anchor). **(a) "+ fixed"** -- NOT attempted: the fix is the identical already-named architectural decision (an FEX-internal-fault short-circuit vs full guest-SEH injection) 5 prior legs explicitly ruled needs Pascal's own design ruling, not a small named-service/triage fix -- attempting it here would be exactly the "speculative changes" this project's own law forbids. **(b) PeMain completes** -- NOT reached, and now explained WHY not: the fault fires before BTCpuSimulate ever gets to PeMain's block. **(c) new frontier mapped + recorded honestly** -- yes, more precisely than any prior leg: this is the first pass to capture the TRUE original fault (not just its post-reset anchor) and identify the exact FEX-internal mechanism by name, byte, and donor line.
- **re-gate, all green, no regression**: `pe8_gate.py` ret=78 PASS; `pe9_gate.py` PASS both backends (wowbox64/fex, both ret=0); `ramfb_gate.py` PASS (7/7 pixel checks). Instrumentation-only change (new receipt prints + one condition relaxation on an existing debug dump) -- no control-flow/behavior change, confirmed by identical `pe10 ret=3221225477` (=0xC0000005) before and after.
- **commit this pass**: `ec45bd6` (source-only: kmain.c new CHALICE C4-30 ORIGINAL FAULT/ELR WINDOW receipts in sync_try_guest_exception; ntsvc.c RAISE#1 dump condition widened + new CHALICE C4-30 GUEST STACK/STUB PAGE hexdumps). Full byte-level detail, donor citations, disassembly -> FEX-MAP.md § **C4-30**.

## C4-29 — the null image base RESOLVED: FEX's own `BTCpuProcessInit` read `LdrSystemDllInitBlock->ntdll_handle` unconditionally and got NULL (this floor never mapped a 32-bit ntdll a second time, unix-loader-side, the way real Wine does). Fixed by mapping `syswow64/ntdll.dll` Phase-0 (C-direct, mirrors the existing native ntdll/exe maps) and populating the block directly -- C4-28's entire fresh fault generation (`RtlImageNtHeader(NULL)` -> `TlsSlots[16]=2` loop) is GONE, confirmed not recurring, no regression. New frontier: back at the SAME pre-existing `BTCpuSimulate+0x10` architectural gap C4-18/21/24/26/27 already independently named (FEX's own `hlt #0` dispatcher marker misrouted through full NT SEH) -- but reached this time via a genuinely healthy path: real `TlsSlots[16]` heap pointer (`BTCpuThreadInit` ran), real JIT/LookupCache activity in the reserve-commit arena, a clean single-pass NT SEH round-trip (no loop). Runtime `c594741`; live 2026-07-19.

- **MEASURE step 1 (who calls `RtlImageNtHeader(NULL)`), answered live**: extended the existing guest-exception-delivery instrumentation (`kmain.c` `sync_try_guest_exception`) with a caller-LR resolve (`resolve_line("lr", regs_safe[30])`) -- caught the caller as `libwow64fex.dll+0x169B40` (`x0=0`, the NULL arg), statically mapped to the return address right after `bl RtlImageNtHeader` inside `FEX::Windows::InvalidationTracker::HandleImageMap` (`Source/Windows/Common/InvalidationTracker.cpp:63`). Traced one level up (`FEX/Source/Windows/WOW64/Module.cpp:547-551`, `BTCpuProcessInit`): two `HandleImageMap` calls exist -- one for the main image (reads the native 64-bit `PEB.ImageBaseAddress`, already correctly wired, NOT the fault), one for the 32-bit ntdll (reads `LdrSystemDllInitBlock->ntdll_handle`, which was NULL -- the actual fault).
- **donor-grounded root cause**: `wine/dlls/ntdll/unix/loader.c:1595-1633` `load_ntdll_wow64_functions(HMODULE module)` is the ONLY donor call site that ever writes `ntdll_handle` (`pLdrSystemDllInitBlock->ntdll_handle = (ULONG_PTR)module`) -- a pure Wine **unix-side** (host-loader) step: map the 32-bit ntdll a SECOND time (after the native one) and walk its export table, zero guest execution. The PE-side `init_wow64()`/`SET_INIT_BLOCK` macro (`dlls/ntdll/loader.c:4404-4416`) sets 6 other function-pointer fields but has `ntdll_handle` explicitly commented OUT -- confirms this is floor/unix-side responsibility, exactly matching how this kernel already maps native ntdll+exe directly (Phase-0, C-side, no NT syscall). This floor never had the 32-bit-ntdll half of that Phase-0 step; C4-26 wrote the *populate* logic (`nt_wow64_populate_init_block_if_ntdll32`) but only wired it behind an `NtMapViewOfSection` hook that (C4-26's own receipt, and this task's fresh re-confirmation) never fires on `pe10`'s boot path.
- **fix (`c594741`)**: `ntboot.c`, inside the existing `if (wow64_mode)` block -- map `syswow64/ntdll.dll` directly via `boot_blob_lookup`+`PeMapImage` (same convention as the native ntdll/exe maps a few lines above), register it, then call `nt_wow64_populate_init_block_if_ntdll32("ntdll.dll", ntdll32_base, ntdll32_size)` **directly, C-side** -- the Wine-equivalent unix-side step, at this floor's existing Phase-0 point. Function exposed non-`static` (`ntfile.c`/`ntfile.h`) for the new direct call site; its old `NtMapViewOfSection` hook is left in place, harmless.
- **PEB.ImageBaseAddress receipt (MEASURE step 2)**: native 64-bit PEB was ALREADY correctly wired (`nt_build_teb(exe_base,...)`, pre-existing, no bug) -- new boot-time receipt line confirms it live (`CHALICE C4-29: PEB.ImageBaseAddress=0x47660000 (exe_base=0x47660000)`). **PEB32.ImageBaseAddress, however, was a genuine self-flagged gap** (`nt_build_wow64_teb32`'s own C4-3-era comment: "left 0 this atom -- no 32-bit ntdll/exe image mapped yet") -- fixed alongside (donor-cited `wine include/winternl.h:847`, offset `0x08`), confirmed via receipt (`CHALICE C4-29: PEB32.ImageBaseAddress=0x47660000`), though NOT the reader in this particular fault chain (FEX's `BTCpuProcessInit` never reads `TEB32->Peb` at this stage) -- closed because it was directly implicated by the brief and already self-documented as owed.
- **TlsSlots[16] resolution (MEASURE step 3)**: fixed, not separately -- `slot16(ThreadState)` now reads a real heap pointer (`0x270001000`), not the `2` sentinel C4-28 found. Confirms the earlier "abnormal arrival" hypothesis was correct and downstream of the SAME root cause: `BTCpuProcessInit` never survived past its own `HandleImageMap(NULL)` crash far enough to reach `BTCpuThreadInit` at all. No separate TlsSlots-writer bug ever existed.
- **fresh pe10-fex ladder vs the brief's own (a)-(d)**: **(a) ACHIEVED** (`BTCpuThreadInit` ran, real TLS pointer). **(b) likely achieved, indirect evidence** (post-thread-init: a genuine JIT/`LookupCache` reserve-commit-arena fault, demand-committed+retried, followed by live `NtSetInformationThread`/`NtQueryInformationThread ThreadWow64Context` round-trips -- consistent with PeMain's JIT'd bytes executing; no literal "first guest instruction" marker printed this pass, would need new instrumentation). **(c) NOT reached** -- terminates via a single clean `NtRaiseException id=207` (`code=0xC0000005`, `firstchance=0`, no loop -- C4-24's fix still holds), `ctx.pc=libwow64fex.dll+0x34E4`, freshly re-mapped this pass and confirmed IDENTICAL to the pre-C4-28 `BTCpuSimulate+0x10` identity (`Module.cpp:757`) -- the SAME architectural gap C4-18/21/24/26/27 all independently named and declined to fix speculatively (FEX's internal `hlt #0` signal-return marker misrouted through this floor's full synthetic NT SEH cycle). `pe10 ret=3221225477` (`=0xC0000005`). **(d) done** -- `map-fex-offsets.sh` re-run, no new DLL-relative address this pass (same identity, freshly confirmed); C4-28's entire fault generation confirmed gone, not recurring anywhere in the transcript.
- **verdict**: this task's own bug (the null image base) is CLOSED end-to-end -- caller identity, donor root cause, fix, TlsSlots resolution, re-gate, fresh ladder, all receipted. The remaining blocker to `hello8_x86`'s WriteFile marker is the SAME pre-existing, already-named, already-out-of-scope architectural gap five prior legs independently reached -- this pass reaches it via a measurably healthier path (real TLS, real JIT activity, clean single-pass SEH walk, not a null-pointer detour), real forward progress even though the terminal exit code is still an unhandled-exception terminate.
- **re-gate, all green, no regression**: `pe8_gate.py` `ret=78` · `pe9_gate.py` both backends (`wowbox64`/`fex`, both `ret=0`) · `ramfb_gate.py` 7/7 · `scratch/backtoback_gate.py` PASS (first attempt under an external 150s wrapper cut short mid-transcript -- not a real failure; rerun without the wrapper, full ~110s run, passed clean, matching this file's own "~2min wall time needed" note).
- **commits this pass**: `51d759e` (instrumentation: caller-LR resolve at guest-exception delivery; PEB32.ImageBaseAddress wire), `c594741` (the fix: syswow64/ntdll.dll Phase-0 map + direct populate call; `nt_wow64_populate_init_block_if_ntdll32` exposed non-static). Full byte-level detail, donor citations, disassembly → `FEX-MAP.md` § **C4-29**.

---

## C4-28 — null-page guard GATED (3 live-caught regressions fixed); the guard CHANGED the whole pe10-fex fault cascade exactly as hypothesized -- new, EARLIER, genuinely different frontier (ntdll `RtlImageNtHeader(NULL)`), NOT the old dispatcher-marker identity. Verdict on rivals A/B/C: new evidence, inconclusive -- named, not chased further this pass (time-boxed). Runtime `6af950a`; live 2026-07-19.

- **salvage-resumed task.** Disk state at start: `d84c091` (C4-28 guard commit, self-flagged "gates not yet re-verified") + an uncommitted `kmain.c` diff of unknown intent.
- **kmain.c diff adjudication: DROPPED.** Three `puts("DBG a/b/c\n")` debug prints inside `mmu_on()`, no functional change, no explanatory commit message. `git checkout`'d away; recorded here per the "no mystery diffs survive" law.
- **GATE, attempt 1: FAILED — guard broke boot before "MMU: on" ever printed.** Direct live-serial boot (bypassing the gate harness's own socket-drain timing to get a raw trace) showed **zero bytes of UART output**, even the pre-MMU banner. Root cause (live ESR/ELR/FAR receipt, `qemu-system-aarch64 -serial stdio` one-shot): `ESR_EL1` EC=`0x25` DFSC=`0x21` (**Alignment fault**) writing `l2_dev_table[1]` at `l2_dev_table+8`. With the MMU still off at this exact point in `mmu_on()`, **every access defaults to Device-nGnRnE** (`SCTLR_EL1.M=0` forces the whole address space Device per the ARM ARM) — and Device memory requires **natural alignment even for a plain `stp`**. The guard's own loop (`for i=1..511`, skipping index 0) left the compiler free to pair adjacent element stores into one `stp` starting at an 8-byte-only-aligned offset, straddling a 16B boundary → fault, every boot, before the kernel ever printed a single character.
- **iteration 1 (reorder loop, index-0 special-cased after)**: moved the crash forward one step (now `l1_table[0]`/`l1_table[1]` pairing, same class of bug) — proved source-order reordering alone is insufficient; the compiler has no notion of "Device memory" and is free to fuse stores wherever the C abstract machine permits it, regardless of statement order.
- **iteration 2 (compiler barriers, `asm volatile("":::"memory")` around each transition)**: reduced but did NOT fully suppress it — the SAME fusion re-appeared **inside** the small `NT_RAM_BLOCKS` loop's own unrolling (no barrier can live *inside* a single loop body without defeating the loop itself).
- **iteration 3 (`-mstrict-align` compiler flag, tried and REVERTED)**: stops the store-fusion class of bug architecturally, but breaks this freestanding/`-nostdlib` build: clang lowers `fs.c`'s `FsWrite` struct-copy code to a libcall `memcpy` this kernel doesn't provide (link fails: `undefined reference to 'memcpy'`). Reverted; noted in `build.sh` so nobody retries it blind.
- **iteration 4 (fix, KEPT)**: `l1_table`/`l2_dev_table` declared `volatile`. This is the actual C-semantics guarantee needed (no store may be elided, reordered across, or **fused** with another volatile access) — stronger than a memory-clobber barrier, and scoped to only the two page-table arrays (no global codegen/perf/ABI impact elsewhere). Direct live-serial boot confirmed clean start-to-shell after this fix.
- **GATE, attempt 2: FAILED — two more genuine, PRE-EXISTING null-pointer bugs, now surfaced for real.** With the alignment crash fixed, `pe8_gate.py` failed: `pe6`/`pe7`/`pe8` all terminated `NtRaiseException code=0xC0000005 (STATUS_ACCESS_VIOLATION)`. Both were **real dereferences of address 0/near-0 by genuine stock-ntdll code**, previously masked because the old (pre-guard) mapping treated address 0 as valid device memory (silent garbage reads instead of faults) — **exactly the failure mode the guard exists to catch** (per `d84c091`'s own commit message), now caught for real:
  1. **`RTL_USER_PROCESS_PARAMETERS.CurrentDirectory.DosPath.Buffer` was always NULL** (field named in a comment, never wired). `map-fex-offsets.sh` resolved the fault site to ntdll's internal `get_full_path_helper` (embedded-symbol, confidence high): `ldr x22,[x24,#0x8]` (loads `DosPath.Buffer`) then unconditionally `ldrh w8,[x22,#0x2]` (checks `Buffer[1]` for a drive-letter `':'` pattern, no NULL check) — fires for pe6/7/8 because their `ImagePathName` is deliberately a bare relative exe name (not the `wow64_mode`-only `"C:\\windows\\..."` absolute path this file's own C4-6 comment already predicted would need `CurrentDirectory` if ever hit). **Fix (`ntboot.c`)**: wire `CurrentDirectory.DosPath = "C:\\"` unconditionally (all processes, not just `wow64_mode`).
  2. **`TEB.ActivationContextStackPointer` (offset `0x2c8`, well-known public Win64 TEB layout) was always NULL.** Crash moved forward to ntdll's `RtlFindActivationContextSectionString` (embedded-symbol, confidence high): `ldr x8,[x18,#0x2c8]; ldr x8,[x8]` — dereferences the TEB field directly with no null-check on the pointer itself (only checks the *result* of the dereference). **Fix (`ntsvc.c`, `nt_build_teb`)**: allocate + wire a small zeroed `ACTIVATION_CONTEXT_STACK` (only the leading `ActiveFrame` pointer, offset 0, is read on this path — zeroed satisfies the fast-empty-stack `cbz` branch immediately after).
- **GATE, attempt 3: PASS, no regression.** `pe8_gate.py` (`ret=78`) · `pe9_gate.py` both backends (wowbox64 + fex, `ret=0`) · `ramfb_gate.py` (7/7 pixel checks) · `scratch/backtoback_gate.py` (pe6/7/8 x2 in one boot, exhaustion-dead, needs ~2min not the gate's own default timeout). Direct live-serial boot also re-verified clean start-to-shell independent of the harness. `pgrep qemu`+kill before/after every boot, no leaks.
- **Committed**: `6af950a` (source-only: `build.sh`, `kmain.c`, `ntboot.c`, `ntsvc.c`; build artifacts reverted per source-only-commits law).
- **THEN: pe10-fex verdict, fresh, with the guard live** (`scratch/pe10_probe.py fex`). The brief's own hypothesis — "the null-page fault may CHANGE the whole cascade (a clean AV at 0 early instead of garbage translation)" — **is confirmed true, and more dramatically than expected**: the fault chain is **not** the old `BTCpuSimulate+0x10`/dispatcher-marker identity (C4-18/21/24/26/27) at all this time. New chain, live-captured in full:
  1. **First fault** (after `starting FEX (Hangover 11.9)` banner + full `BTCpuProcessInit`/CPU-feature-registry walk complete): `ESR` EC=`0x25`, `elr=ntdll.dll+0x43DB8`, **`far=0x0`** — a genuine, clean AV at address 0, exactly the brief's predicted shape. `map-fex-offsets.sh` (embedded-symbol, confidence medium) → `RtlImageNtHeader`: `ldrh w8,[x8]` reading the MZ signature of a module base pointer passed in `x0` — i.e. something called `RtlImageNtHeader(NULL)`.
  2. Every subsequent fault in the transcript (repeats, non-hanging, `NtRaiseException`-cycled) is a **different, later** address: `elr=libwow64fex.dll+0x3B1C far=0x42`. Mapped (embedded-symbol, confidence high) → `BTCpuResetToConsistentStateImpl(_EXCEPTION_POINTERS*)` (`Source/Windows/WOW64/Module.cpp:871`), specifically its own **third instruction** (`ldr x23,[x21,#0x40]`, offset `+0x2c` into the function) — `x21` is loaded from `TEB->TlsSlots[16]` (`WOW64_TLS_THREAD_STATE`, FEX's own private slot per `Source/Windows/include/winternl.h:28-29`/`Module.cpp:101-102`), the live `CHALICE TLS PROBE` line confirms `slot16(ThreadState)=0x0000000000000002` — a tiny, non-pointer sentinel value, not a real `FEXCore::Core::InternalThreadState*` heap pointer. `far=0x42` = `0x2+0x40`, confirming the arithmetic exactly.
  3. **Verdict on rivals A/B/C, new evidence, still inconclusive** — grepped the transcript for FEX's own `BTCpuThreadInit` narration (the function that legitimately writes a REAL heap pointer into this exact slot, `Module.cpp:626 TLS.ThreadState() = Thread;`, donor-read this pass): **no trace of it having run** (only `BTCpuProcessInit`'s "starting FEX" banner appears; nothing between it and the crash resembles thread-init). This floor never itself writes to `TlsSlots[16]` (verified: `grep TlsSlots` across `ntboot.c`/`ntsvc.c` shows only slots 1/`WOW64_TLS_CPURESERVED` and 10/`WOW64_TLS_WOW64INFO` are ever floor-written) — so the `2` is either (i) a genuinely pre-thread-init zeroed/leftover TEB byte pattern that happens to read as `2` at this offset (rung-C-adjacent: "abnormal arrival", something reads this slot before any real init wrote it), or (ii) FEX's own lazy/placeholder tagging scheme for an as-yet-uninitialized thread (not confirmed in the donor source read this pass — `BTCpuThreadInit`'s only write is the real-pointer one at `:626`, no smaller sentinel constant found nearby). **Not resolved this pass** — flagged, not guessed at further, per the "measure don't guess" doctrine and the time-box below.
- **Ladder classification: (c) — new, more precise, GENUINELY DIFFERENT mapped frontier, honestly recorded.** Not (a) marker-consumed/JIT-enters, not (b) syscall-hinge-proven. The old `BTCpuSimulate+0x10` dispatcher-marker identity (C4-18/21/24/26/27's whole prior anatomy) **did not recur once** in this pass's full transcript — the guard genuinely changed which code path FEX's own WOW64 exception-reconstruction machinery takes, moving the frontier EARLIER (into `RtlImageNtHeader(NULL)` / `BTCpuResetToConsistentStateImpl`'s own thread-state read) rather than deeper. This is real forward information, not a repeat of prior legs' findings.
- **NOT chased further this pass** — instrumenting `RtlImageNtHeader`'s caller (who passes it NULL) and `BTCpuThreadInit`'s actual call site/timing is genuinely new instrumentation work (per the brief's own "evidence tools" list, appropriately scoped for the NEXT pass), and the 30-minute wall was closing. Per LAW ("partial receipts beat lost work"): guard is gated, fixed, committed, gate-green; this fresh pe10-fex anatomy is fully receipted above for the next worker to resume from, without re-doing any of this pass's measurement work.
- **commits this pass**: `6af950a` (guard fixes, source-only). No further code changes attempted after the fresh pe10-fex measurement (scope discipline: FIX only what fresh receipts NAME, and this pass's receipts name new investigation targets, not a small fix).

---

## C4-27 — the syscall hinge stub BUILT, SEEDED, and its BOP address LIVE-DERIVED (zero hardcode) -- correctly wired but UNREACHABLE this pass: the pre-existing C4-18/21/24/26 architectural gap (FEX's own `hlt #0` SignalHandlerReturnAddress marker misrouted through full NT SEH dispatch) fires BEFORE PeMain's first instruction ever executes, same identity, terminal outcome unchanged. Runtime (this pass, uncommitted at write time -- see commits list below); live 2026-07-18.

- **design, implemented exactly per the conductor's fixed ruling** -- a 13-byte i386 stub (`push eax; push -1; mov eax,0x2C; call rel32->BOP`) built in a dedicated guest-reachable (<4GB, `nt_territory_alloc`) buffer; `ntboot.c` seeds `*(u32*)x86_stack_esp = stub_addr` (PeMain's own synthetic return-address slot, the exact word its `ret` pops) at the SAME call site as the existing `nt_wow64_cpureserved_set_context` seeding, C4-11's own convention. `ntsvc.c`'s `NtAllocateVirtualMemory` gained a new hook: FEX's `BTCpuProcessInit` allocates its BOP ("bridge op"/`BridgeInstrs::Syscall`) trampoline via a REAL `NtAllocateVirtualMemory(ZeroBits=0x7fffffff, Size=4, MEM_RESERVE|MEM_COMMIT, PAGE_EXECUTE_READWRITE)` call this floor already services (`FEX Source/Windows/WOW64/Module.cpp:557-561`) -- the hook recognizes this exact shape (first match only) and captures the **live returned address** as the BOP value, no capture/hardcode, satisfying the IRON law explicitly (measured every boot, from the real syscall's own return value). The moment BOP is known, the stub's bytes (including the `call`'s `rel32`, computed from the two now-known addresses) are written into the pre-reserved buffer.
- **measured live** (`scratch/pe10_probe.py`, `pe10 fex`, `nt_dispatch_log_enabled` already on for `pe10`): `CHALICE: C4-27 stub buffer addr=0x47690000 seeded at esp=0x4768C000` (early, during the same C-seed phase as `nt_wow64_cpureserved_set_context`) → ... → `CHALICE C4-27: BOP addr discovered=0x55E00000` → `CHALICE C4-27: guest stub written addr=0x47690000 bop=0x55E00000 bytes=0xD` (mid-boot, inside `BTCpuProcessInit`, exactly where the donor source places this allocation) — **both halves of the design fire correctly, every boot, with a freshly different BOP address each run** (0x55E00000 this pass vs whatever ASLR/bump-state picks next time), proving the derivation is genuinely live, not a fluke of one fixed layout.
- **BUT the stub is never reached this pass** — `pe10 fex`'s terminal outcome is **byte-for-byte identical** to C4-24/C4-26's own pre-existing frontier: `RAISE#1 code=0xC000001D firstchance=0 ctx.pc=0x4B8034E4 ctx.sp=0x4116B7A0 addr=0`, then `NtRaiseException FirstChance=FALSE -- terminating`, `pe10 ret=3221225501` (`=0xC000001D`). Mapped fresh this pass (`map-fex-offsets.sh stock/wow64-chain/libwow64fex.dll 0x34E4` against this run's own base) → `BTCpuSimulate+0x10` (`Source/Windows/WOW64/Module.cpp:757`), confidence high, embedded-symbol — **the exact same identity C4-24 first named and C4-26 re-confirmed**, not a new fault, not shifted by this pass's changes.
- **root cause of unreachability, donor-grounded**: this ctx.pc is `RtlCaptureContext`'s own capture point inside `BTCpuSimulate`'s asm prologue (`TLS.EntryContext() = entry_context` in `BTCpuSimulateImpl`, Module.cpp:759-761) — i.e. FEX's JIT entry stub captures ITS OWN native-ARM64 return-address/frame as `EntryContext` *before* `CTX->ExecuteThread(TLS.ThreadState())` ever begins interpreting/JIT-compiling a single byte of the guest's x86 stream at `Eip=exe_entry`. The `RAISE#1` exception is FEX's `Dispatcher.cpp:379-384` `hlt #0` `SignalHandlerReturnAddress` marker — a normal, expected part of FEX's OWN internal JIT-entry/exception-delivery machinery (meant to be silently absorbed FEX-internally via a host `SIGILL` handler on real Linux/Wine, never meant to reach NT SEH dispatch at all) — being misrouted, on this floor, through a full synthetic `KiUserExceptionDispatcher`→`dispatch_exception`→`call_seh_handlers` cycle that (correctly, per wine's own donor logic) finds no SEH frame for a JIT-internal marker and falls through to unhandled/terminate. This is **upstream of and orthogonal to** whether PeMain's guest code ever executes — the guest program (and therefore this pass's stub) never gets a first instruction fetched, so the stub, though correctly built/seeded/BOP-wired, cannot be exercised until this separate, pre-existing gap closes.
- **not a regression, not new**: C4-26 already named this exact identity/outcome as "the remaining gap is architectural... a genuinely new design question, not a small named field/service gap... left named, not attempted, per this task's no-speculative-changes law." This pass's own measurement (fresh `map-fex-offsets.sh` run, fresh BOP address, fresh transcript) independently reconfirms the identical conclusion — ruling out any possibility that C4-25/26's own fix (LdrSystemDllInitBlock populate, still dead code on this path per C4-26) or this pass's stub/BOP work shifted the frontier at all.
- **gate, no regression**: build clean (`GAIA_NO_RUN=1 bash build.sh`); `pe8_gate.py` PASS (`ret=78`); `pe9_gate.py` PASS both backends (wowbox64 + fex); `ramfb_gate.py` PASS (7/7). `pe10` (wowbox64, no `fex` token) re-checked too (not gate-required, informational): identical pre-existing parked fault (`elr=0 far=0`, unchanged) — the new stub-seed code runs unconditionally for `wow64_mode` regardless of backend and does not disturb wowbox64's own independent frontier.
- **scope discipline**: per this task's own "MEASURE FIRST" step 3 and "no speculative changes" law, the FEX-dispatcher-marker-vs-NT-SEH routing question (the real remaining blocker) was NOT attempted this pass — it is a genuinely different design decision (an FEX-internal-fault short-circuit before NT SEH injection, vs today's always-inject-into-full-SEH-cycle floor design) requiring its own ruling, left named for the next worker, same as C4-26 left it.
- source-only commits this pass → `ntsvc.c`/`ntsvc.h` (BOP-discovery hook + stub writer) + `ntboot.c` (stub buffer alloc + return-address seed) — see FEX-MAP.md § C4-27 for the byte-level stub design + calling-convention citations.
- corrected per this task's own instruction: the C4-5 "done" line below (hello5/pe7's WriteFile marker) refers to a DIFFERENT, EARLIER milestone (`hello3`/`hello5`'s import-table WriteFile call, unrelated to the x86/WOW64 door) — the x86-door WriteFile marker this HANDOFF's "NEXT" section describes is **not yet reached** and belongs to a FUTURE `hello9_x86.c` (C4-28: a version of hello8 that actually calls WriteFile once the SEH-routing gap above is closed) — `hello8_x86.c`'s own header comment already says exactly this ("completion probe ret=42", "NO WriteFile exists in it").

---

## C4-26 — LdrSystemDllInitBlock fix gated + measured; UNEXERCISED on pe10's path; RAISE#1 guest content proven REAL (Eip/code valid), same terminal outcome as C4-24. Runtime `4c1b0cc`; live 2026-07-18.

- salvage-resumed task: prior worker committed `3f81099` (C4-25 instrumentation) + `4c1b0cc` (C4-26 fix) and hit the 30-min wall immediately after, with zero build/gate/pe10 verification and zero doc updates. This pass: built clean, re-gated (no regression), ran `pe10 fex`, measured the fix's real effect, documented. No further code changes made — see below for why.
- **build** → `GAIA_NO_RUN=1 bash build.sh` clean. **Gates, no regression**: `pe8_gate` `ret=78` PASS · `pe9_gate` PASS both backends (wowbox64 + fex) · `ramfb_gate` PASS (7/7).
- **pe10 fex measured outcome** → `RAISE#1`: `firstchance=0 code=0xC000001D ctx.pc=libwow64fex+0x34E4(BTCpuSimulate+0x10) addr=0` (same identity C4-24 already named); `pe10 ret=3221225501` (`=0xC000001D`), same exit code as C4-24's own pre-existing frontier. **New this pass**: the live `WOW64_CPURESERVED` I386_CONTEXT dump (C4-25's instrumentation) shows `Eip=0x47661000` (`hello8_x86.exe+0x1000`, the real seeded exe entry) with a valid guest-memory hexdump at that Eip decoding to `push ebp; mov ebp,esp; mov eax,0x2A; pop ebp; ret` — real, correctly-mapped, intact x86 code, not garbage/zero. Ladder rung **(a) confirmed**: Eip is real and guest content is good. Rung (b) (hello8_x86 WriteFile marker) **not reached** — outcome unchanged from C4-24.
- **C4-26's populate hook measured to never fire on this path** — grepped the full transcript for its own diagnostic print: zero hits. Every `CHALICE IMAGE:` line in the run confirms only the **native** ARM64 `ntdll.dll` is ever mapped (`nt_boot_process`'s Phase-0 `PeMapImage` C-call, bypassing `NtCreateSection`/`NtMapViewOfSection` by design); no `syswow64/ntdll.dll` (32-bit) is ever mapped during `pe10`'s boot (that only happens in the unrelated `pe9` self-test). So the fix's `NtMapViewOfSection`-hooked trigger condition (a 32-bit-Machine `ntdll.dll` getting mapped) never occurs here — not a bug in the fix's own logic, just dead code on this boot path. Genuinely explains why the fix, though correctly written and donor-grounded, produced no observable behavior change in `pe10 fex` (the observed Eip=real outcome above comes from this floor's own direct C-seeded `nt_wow64_cpureserved_set_context`, not from the fix).
- **remaining gap is architectural, not a small field/service fix** — the terminal `STATUS_ILLEGAL_INSTRUCTION`/unhandled-terminate is FEX's `hlt #0` `SignalHandlerReturnAddress` dispatcher marker (`Dispatcher.cpp:379-384`) being routed through a full synthetic NT SEH cycle (`KiUserExceptionDispatcher`→`dispatch_exception`→`call_seh_handlers`, correctly finding no handler for a JIT-internal marker, correctly falling to unhandled/terminate) instead of being absorbed by an FEX-internal signal-return short-circuit the way real Wine/Linux does at the host-signal level. Fixing this is a new design question (a different NT-exception-vs-FEX-internal-fault routing decision), not a one-field/one-service gap — **left named for the next worker**, per this task's "no speculative changes" law.
- full receipt, byte-level detail, donor citations → `FEX-MAP.md` § **C4-26**.
- commits this pass → none (docs-only; code was already committed by the salvaged worker as `3f81099`/`4c1b0cc`).

---

## C4-24 — STATUS_INVALID_DISPOSITION loop BROKEN; new frontier CLEANLY DIAGNOSED `STATUS_ILLEGAL_INSTRUCTION` termination. Runtime `ec206c8`; live 2026-07-18.

- **`ntdll+0x3868C` identity** — `raise_status+0x18` (wine `dlls/ntdll/exception.c:358-367`; ARM64 body `dlls/ntdll/signal_arm64.c` region resolved via `map-fex-offsets.sh stock/ntdll.dll 0x3868C` against the **wine** donor, `confidence high [embedded-symbol]`): `b 0x180038684` — the return address right after `bl RtlRaiseException` inside `raise_status`'s `for(;;) RtlRaiseException(&ExceptionRec)` defensive retry loop (`DECLSPEC_NORETURN`; the loop exists ONLY as a belt-and-suspenders guard for "this must never return on real hardware").
- **poison-pc origin, ruled out as a real bug** — `0xFFFFFFFFFFFFFEFE` is NOT a sentinel constant anywhere in wine/FEX donor source (grepped both quarries: no `0xFEFE`/`~0x101`/wow64-transition-marker hit). It never appeared in the C4-24 instrumented capture either — the prior worker's "poison pc" label was a mischaracterization of `ntdll+0x3868C`'s own resolved identity (`raise_status+0x18`), not a distinct garbage value. Retired as a lead.
- **`0xC0000026` identity correction** — this is `STATUS_INVALID_DISPOSITION` (`wine/include/ntstatus.h:316`, `winnt.h:640`), **not** `STATUS_ASSERTION_FAILURE` (`0xC0000420`, `ntstatus.h:1044`) as C4-23's note guessed. Matches the task title; HANDOFF's C4-23 STATE block above is stale on this one point (kept unedited for archaeology, corrected here).
- **loop anatomy — measured, not guessed** (new `NtRaiseException` instrumentation, `2db734c`; full capture `/tmp/gaia-pe10-probe.log` from the pre-fix run): `RAISE#1` — `firstchance=0`, `code=0xC000001D` (`STATUS_ILLEGAL_INSTRUCTION`), `ctx.pc=0x4B7F34E4` (`libwow64fex.dll+0x34E4` = **`BTCpuSimulate+0x10`**, `FEX Source/Windows/WOW64/Module.cpp:757-777` — `RtlCaptureContext` captures FEX's own thread-entry-stub frame into `TLS.EntryContext()`, the WOW64-boundary anchor FEX substitutes into the exception record/context via `Wow64PrepareForException`→`BTCpuResetToConsistentStateImpl` when reconstructing a guest exception for native SEH dispatch), `addr=0` (FEX's reconstruction zeroes `ExceptionAddress`, by design). `RAISE#2`..`#18` (capped at 24) — **all** `firstchance=0`, `code=0xC0000026`, `ctx.pc==ctx.lr(x30)==ntdll+0x3868C` (every single time — literally `raise_status`'s own return address, unchanged), `flags=0x9` (`EXCEPTION_NONCONTINUABLE|EXCEPTION_...`), **`ctx.sp` monotonically decreasing** by `~0x6c0` bytes per raise (`0x4116B0E0 → 0x4116AA20 → 0x4116A360 → …`). This is **genuine unbounded guest C-stack recursion**, not a Windows-level "same exception record re-raised" nor a "new first-chance exception per hardware cycle" — it is wine's own `RtlRaiseException`→`dispatch_exception`→`call_seh_handlers`(`STATUS_INVALID_DISPOSITION`)→`RtlRaiseStatus`→`raise_status`→`RtlRaiseException`→… chain, spinning because **this floor's `NtRaiseException` re-entered `KiUserExceptionDispatcher` on every call regardless of the `FirstChance` flag**, including the `FirstChance=FALSE` second-chance call that `dispatch_exception`'s own tail issues once first-chance dispatch is exhausted.
- **root cause, donor-grounded** — `wine/dlls/ntdll/exception.c:239-370` `dispatch_exception()` is the **only** call site in the entire donor (grepped: `signal_arm{,64,64ec,i386}.c`, `x86_64`, `exception.c`) that ever passes `FirstChance=FALSE`: its tail, `return NtRaiseException(rec, context, FALSE);`, reached exactly when `call_vectored_handlers` found nothing and `call_seh_handlers` (`signal_arm64.c:217-303`, ARM64 `.pdata`/`virtual_unwind` frame search) returned `STATUS_UNHANDLED_EXCEPTION` (the `status != STATUS_UNHANDLED_EXCEPTION` guard just above it, which would otherwise call `RtlRaiseStatus`, is specifically skipped in this case). On real Windows this `FirstChance=FALSE` call is the **second-chance report to the kernel** — with no debugger attached (this floor's PEB `BeingDebugged` is unconditionally `0`, `ntsvc.c` TEB/PEB build), the kernel's second-chance handling terminates the process; it does **not** re-invoke `KiUserExceptionDispatcher` a second time. Every genuine first-chance call (`RtlRaiseException`'s own asm, `signal_arm64.c:745-771`, unconditionally uses `FirstChance=TRUE` at its label `1:`) is a different, correctly-handled case. This floor's `NtRaiseException` (`ntsvc.c`) ignored the `FirstChance` argument entirely and called `nt_raise_guest_exception()` (which re-enters `KiUserExceptionDispatcher` via `nt_exception_enter`) unconditionally — turning the second-chance terminal call into a second real dispatch attempt on the *same already-unhandled* `BTCpuSimulate`-anchored context, which recursed into wine's own `raise_status` retry loop.
- Not one of the three explicitly-speculative candidates the task offered (a: `.pdata` exception-directory fidelity gap; b: our synthesized exception frame missing a machine-frame/unwind linkage field; c: TEB `StackBase`/`StackLimit` audit) — the measured evidence named a cleaner, more precise gap: **the `FirstChance` dispatch contract itself was unimplemented**, one flag-check away, with zero speculative surface touched. `.pdata`/TEB-bounds audits were not needed to explain or fix the loop; `call_seh_handlers` itself legitimately returning `STATUS_UNHANDLED_EXCEPTION` for a JIT-internal fault with no matching SEH frame is expected, correct Windows behavior, not a floor gap.
- **fix (`ec206c8`)** — `ntsvc.c` `NtRaiseException`: `FirstChance==0` now prints a diagnostic line and cleanly terminates via the same `nt_setjmp`/`nt_longjmp` channel `NtTerminateProcess`/`NtRaiseHardError` already use (`nt_exit_code = code`), instead of calling `nt_raise_guest_exception()`. `FirstChance!=0` is unchanged (still the C4-18 `KiUserExceptionDispatcher` re-entry route).
- **re-gate (post-fix)** — build clean; `pe8_gate` `ret=78` PASS; `pe9_gate` PASS both backends (wowbox64 + FEX); `ramfb_gate` PASS. No regression.
- **`pe10 fex` outcome (post-fix)** — the `0xC0000026` loop is **gone**: exactly one `NtRaiseException` call (`RAISE#1`, `code=0xC000001D`, `firstchance=0`), immediately followed by `CHALICE: NtRaiseException FirstChance=FALSE (second-chance, unhandled) -- terminating`, then a clean process exit, `pe10 ret=3221225501` (`=0xC000001D` as unsigned, exactly `nt_exit_code`). No hang, no crash-loop, no forensic kernel fault — a **legible, one-line-diagnosed termination**, matching this floor's established "diagnose then cleanly exit" philosophy. `wowbox64` backend (`pe10`, no `fex` token) unaffected/unchanged — still its pre-existing parked own-JIT-cache fault, never touched this code path.
- **furthest point** — no `hello8_x86`/`WriteFile` marker yet; C4-5 (the x86 door) is **not yet complete**. The loop this task targeted is broken; the *new*, now cleanly-diagnosed frontier is `STATUS_ILLEGAL_INSTRUCTION` (`0xC000001D`) itself, raised by FEX at the `BTCpuSimulate`/`TLS.EntryContext` WOW64-boundary anchor with `addr=0` — i.e., FEX's own simulate-entry path considers the guest's first real dispatch attempt an illegal instruction (or is reporting the arena-dispatcher `hlt #0` `SignalHandlerReturnAddress` marker, `FEXCore/Source/Interface/Core/Dispatcher/Dispatcher.cpp:379-384`, as a genuine guest fault rather than completing its internal signal-return dance). Deliberately NOT chased further this pass — out of this task's scope (STATUS_INVALID_DISPOSITION loop only); next worker's frontier, not a hang.
- commits — `2db734c` (instrumentation), `ec206c8` (fix).

---


Purpose: let GAIA-OS run programs built for the Windows ABI, the same way
**ReactOS, Wine, and Proton** do — by implementing the published NT service
interface as a GAIA-OS "personality." Ordinary OS-compatibility engineering:
our kernel becomes the floor a portable PE userland stands on. No target
software is modified; we implement a documented ABI so existing binaries load
and run. Donors cloned as read-only quarry (gitignored): `wine/`, `reactos/`,
`proton/`.

---

## STATE — CURRENT: C4-31 — the consume-check (`BTCpuResetToConsistentStateImpl`->`HandleGuestException`) is donor-traced end-to-end and its own input state (`SynchronousFaultData`) is PROVEN present+correct at the C4-30 self-fault -- byte-exact match to FEX's `NoExecOp` ("instruction fetch from non-executable guest memory") `BreakDefinition`, first-time identification of the exact frontend site. This REFUTES this task's own A (PC/field-match failure) and B (missing thread-state) suspects: the check works as designed, correctly translating the fault into a real x86 `STATUS_ACCESS_VIOLATION(EXECUTE_FAULT)` at `RIP=exe_entry`, which then correctly goes unhandled at guest SEH (hello8_x86.exe has no SEH handlers) -- not an exception-delivery bug. The REAL, newly-named gap: FEX's `InvalidationTracker` never registers `exe_entry` as executable despite every checkable input (PE headers, section EXECUTE flag, PEB.ImageBaseAddress, this floor's own NtQueryVirtualMemory, image-registration ordering) looking correct -- internal to FEX, unreachable without a donor edit, named not chased (time-box). No fix attempted. See C4-31 block above. Runtime TBD (source-only, kmain.c); live 2026-07-19.

(prior state, still accurate for the original pre-reset fault identity itself, kept below)

## STATE — C4-30 — the ORIGINAL (pre-reset) fault, fully explained end-to-end for the first time: it is FEX's OWN deliberate `GuestSignal_SIGSEGV` self-fault ("force a SIGSEGV by loading zero", Dispatcher.cpp, byte-matched exactly) inside FEX's own fixed dispatcher buffer (size 0x4000 == `MAX_DISPATCHER_CODE_SIZE`, exact match), reached via `IR::Break(FAULT_SIGSEGV)` before `BTCpuSimulate` ever dispatches to PeMain's own block (Eax=0/Esp==Ebp==boot-seed proves PeMain never ran). Demand-commit triage correctly refused (far=0 genuinely outside all allocators, not a regression). The C4-27 stub is proven intact/correctly-seeded/unreached (stack + stub-page hexdumps). No fix attempted -- same out-of-scope architectural-design gap (FEX-internal signal marker vs full NT SEH injection) C4-18/21/24/26/27/29 all deferred to Pascal, now precisely identified for the first time as this specific marker. See C4-30 block above. Runtime `ec45bd6`; live 2026-07-19.

(prior state, still accurate for the null-image-base fix itself, kept below)

## STATE — C4-29 — the null image base RESOLVED: FEX's `BTCpuProcessInit` read `LdrSystemDllInitBlock->ntdll_handle` (never populated on this floor) and passed NULL into `HandleImageMap`->`RtlImageNtHeader`. Fixed by mapping `syswow64/ntdll.dll` Phase-0 (C-direct) + populating the block directly -- C4-28's whole fresh fault chain (`RtlImageNtHeader(NULL)`/`TlsSlots[16]=2` loop) is GONE, no regression (pe8/pe9-both-backends/ramfb/backtoback all green). New frontier: back at the SAME pre-existing `BTCpuSimulate+0x10` architectural gap C4-18/21/24/26/27 already named, reached this time via a genuinely healthy path (real TLS pointer, real JIT activity, clean single-pass SEH round-trip, no loop). See C4-29 block above. Runtime `c594741`; live 2026-07-19.

(prior state, still accurate for the null-page guard mechanism itself, kept below)

## STATE — C4-28 — null-page guard GATED (3 pre-existing null-deref regressions fixed for real: kmain.c stp-alignment, ntboot.c CurrentDirectory, ntsvc.c ActivationContextStackPointer); the guard genuinely CHANGES the pe10-fex fault cascade as hypothesized -- new, EARLIER frontier (`RtlImageNtHeader(NULL)` -> `BTCpuResetToConsistentStateImpl`'s own `TlsSlots[16]=2` sentinel read), NOT the old `BTCpuSimulate+0x10` dispatcher-marker identity C4-18/21/24/26/27 all shared (SUPERSEDED by C4-29 above -- this exact chain is now fixed). Verdict A/B/C: new evidence, inconclusive, named not chased (time-box). See C4-28 block above. Runtime `6af950a`; live 2026-07-19.

(prior state, still accurate for the syscall-hinge-stub mechanism itself, kept below)

## STATE — C4-27 — the syscall-hinge stub (PeMain ret -> floor stub -> NtTerminateProcess via FEX's live-derived BOP) is BUILT, SEEDED, and its BOP address IS discovered live every boot (zero hardcode) -- but UNREACHABLE (this state predates C4-28's guard, which changed the cascade -- see C4-28 above for the current anatomy): RAISE#1/`BTCpuSimulate+0x10` (the pre-existing C4-18/21/24/26 FEX-dispatcher-marker-vs-NT-SEH gap) fires before PeMain's first instruction executes, terminal outcome byte-for-byte unchanged from C4-24/26. See C4-27 block above. Runtime `66f8820`; live 2026-07-18.

(prior state, still accurate for the termination mechanism itself, kept below)

## STATE — C4-26 measured — Eip/guest-content at the `STATUS_ILLEGAL_INSTRUCTION` termination is now PROVEN REAL (not garbage); C4-26's own populate-fix is dead code on `pe10`'s boot path (never mapped a 32-bit ntdll to hook); terminal outcome UNCHANGED from C4-24 (see C4-26 block above). Runtime `4c1b0cc`; live 2026-07-18.

(prior state, still accurate for the termination mechanism itself, kept below)

## STATE — C4-24 `STATUS_INVALID_DISPOSITION` loop BROKEN (see C4-24 block above); new frontier = cleanly-diagnosed `STATUS_ILLEGAL_INSTRUCTION` (`0xC000001D`) termination at the FEX `BTCpuSimulate`/`TLS.EntryContext` WOW64-boundary anchor, no hang. Runtime `ec206c8`; live 2026-07-18.

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
