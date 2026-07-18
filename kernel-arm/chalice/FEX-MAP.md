# CHALICE FEX map — Hangover 11.9

## C4-27 — syscall-hinge stub design (BOP/Wow64Transition) + reconfirmed frontier

- **BTCpuGetBopCode** (`FEX/Source/Windows/WOW64/Module.cpp:689-691`): `return BridgeInstrs::Syscall;` — a process-global `void*`, set exactly once, in `BTCpuProcessInit` (`:557-561`): `SIZE_T Size = 4; void* Addr = nullptr; NtAllocateVirtualMemory(NtCurrentProcess(), &Addr, (1U<<31)-1, &Size, MEM_RESERVE|MEM_COMMIT, PAGE_EXECUTE_READWRITE); *(uint32_t*)Addr = 0x2ecd2ecd; BridgeInstrs::Syscall = Addr; BridgeInstrs::UnixCall = Addr+2;`. `0x2ecd2ecd` little-endian = two back-to-back `int 0x2e` (`CD 2E`) opcodes — classic pre-syscall-instruction x86 trap encoding, never actually decoded as real x86 (FEX intercepts by RIP identity, `Module.cpp:451/461`, before any instruction fetch).
- **wow64.dll publication route** (donor, real Windows/Wine path, NOT exercised on this floor's `pe10` — see C4-26): `dlls/wow64/syscall.c:878-880` `init_image_mapping` writes `BTCpuGetBopCode()`'s value into the 32-bit ntdll's own exported `Wow64Transition` variable (`dlls/ntdll/ntdll.spec:1215 @ extern -arch=win32 Wow64Transition`); `:1047` ALSO writes it directly into `*__wine_syscall_dispatcher` on the SAME (native) module handle that publishes `LdrSystemDllInitBlock`. This floor's `pe10` path never maps a real 32-bit ntdll (C4-26 receipt), so this publication mechanism is dead code here — **this floor derives the BOP value a different, still fully donor-grounded way**: live-observing the exact `NtAllocateVirtualMemory` call `BTCpuProcessInit` itself makes for this allocation (see `zoneA/ntsvc.c` C4-27 hook, `HANDOFF.md` § C4-27) — the SAME real syscall, watched at the boundary this floor already owns, rather than replicating wow64.dll's own internal `RtlFindExportedRoutineByName`+write step (which has no 32-bit ntdll to target on this path anyway).
- **calling convention, reverse-derived from FEX's own interceptor** (`Module.cpp:438-461` `HandleSyscallImpl`): entry via a `call` (not `jmp`) to the BOP address; `ReturnRIP = *(uint32_t*)[ESP]` (the `call`'s own pushed return address); `ReturnRSP = ESP+4` (post-pop); syscall id = guest `EAX` at call time; `args = ReturnRSP+4 = ESP+8` passed straight to `Wow64SystemServiceEx(EAX, args)` (donor `wine/dlls/wow64/syscall.c:1202-1211`: `id=num&0xfff; table=&syscall_tables[(num>>12)&3]; wow64_syscall(args,table->ServiceTable[id])`). Reconciled address-arithmetic proof this stub design relies on: a canonical real ntdll stub `mov eax,id; lea edx,[esp+4]; call [Wow64Transition]` computes `edx` = its OWN entry-ESP+4 (i.e. the address right after ITS OWN return address, where arg0 lives) BEFORE the `call` pushes a new return address; FEX's `args=ESP_trap+8` recovers that exact same address purely from stack arithmetic post-call, without ever reading EDX — confirms this floor's stub does not need to set EDX, only push the args in-order onto the stack before its own `call`.
- **table/id selection**: `num & 0xfff` selects the id within `syscall_tables[(num>>12)&3]`; `syscall_tables[0]` (`syscall.c:59-61`) is compiled directly into stock `wow64.dll` itself (`syscall_thunks[]`, generated from `ALL_SYSCALLS32`/`wow64_NtXxx` thunks) — real, unmodified stock code, needs no floor participation once wow64.dll is correctly loaded (already true — `pe10 fex` already reaches `BTCpuSimulate`). `NtTerminateProcess` = id `0x2C` in BOTH the win32 `ntdll.spec` (`-syscall=0x002c NtTerminateProcess`, line 439) and this repo's own `wine-arm64-syscalls.txt:36` (`44 0x02c NtTerminateProcess`) — one canonical Wine numbering, confirmed identical across archs, no translation needed.
- **arg order confirmed against the real thunk** (`wine/dlls/wow64/process.c:1144-1152` `wow64_NtTerminateProcess`): `HANDLE handle = get_handle(&args); LONG exit_code = get_ulong(&args);` — `get_handle`/`get_ulong` (`wow64_private.h:80-81`) each consume exactly one packed `UINT` (`args_alignment=4` for i386, `syscall.c:988`), in declaration order — args[0]=handle, args[1]=exit_code, confirming standard __stdcall right-to-left push order (`push exit_code; push handle; call [BOP]`) lands them correctly. `if (!handle && pBTCpuProcessTerm) pBTCpuProcessTerm(handle,FALSE,0);` — a NULL handle triggers an extra FEX-side cleanup call before the real `NtTerminateProcess`; this floor's stub pushes `-1` (`NtCurrentProcess()` pseudo-handle) specifically to skip that extra call (this floor's own `NtTerminateProcess`, `zoneA/ntsvc.c`, ignores `ProcessHandle` entirely regardless — `-1` is the minimal-fault-surface choice, not a functional requirement).
- **stub bytes built** (`zoneA/ntsvc.c`, `nt_wow64_stub_write_if_ready`): `50` (`push eax`) `6A FF` (`push -1`) `B8 2C 00 00 00` (`mov eax,0x2C`) `E8 <rel32>` (`call BOP`) — 13 bytes total, `rel32 = bop_addr - (stub_addr+13)`, computed live from two runtime-discovered addresses (stub buffer from `nt_territory_alloc`, BOP from the live `NtAllocateVirtualMemory` hook) — no constant ever hardcoded.
- **measured outcome, this pass** (`scratch/pe10_probe.py`, `pe10 fex`): stub built + seeded correctly every boot (`CHALICE: C4-27 stub buffer addr=0x47690000 seeded at esp=0x4768C000` → `CHALICE C4-27: BOP addr discovered=0x55E00000` → `CHALICE C4-27: guest stub written addr=0x47690000 bop=0x55E00000 bytes=0xD`) — but PeMain never executes far enough to reach its own `ret` this pass. `RAISE#1` fires first, from `ctx.pc=0x4B8034E4`, mapped fresh this run:
```text
0x34E4 => BTCpuSimulate (Source/Windows/WOW64/Module.cpp:757) confidence high [embedded-symbol; start=0x1800034d4]
```
  — `BTCpuSimulate+0x10`, the SAME identity C4-24 first named and C4-26 re-confirmed (RtlCaptureContext's own capture point inside `BTCpuSimulate`'s prologue, `TLS.EntryContext()=entry_context` at `BTCpuSimulateImpl` entry, `Module.cpp:759-761`, BEFORE `CTX->ExecuteThread` ever begins translating guest bytes at `Eip=exe_entry`). `pe10 ret=3221225501` (`=0xC000001D`), unchanged. This is the pre-existing FEX-dispatcher-`hlt#0`-vs-NT-SEH architectural gap (C4-18/21/24/26), confirmed once more, upstream of and orthogonal to the syscall-hinge stub — not a defect in this pass's own design/implementation, and not newly introduced by it (regression gates below confirm zero collateral change).
- **gate, no regression**: build clean; `pe8_gate.py` PASS (`ret=78`); `pe9_gate.py` PASS both backends; `ramfb_gate.py` PASS (7/7); `pe10` (wowbox64, informational, not gate-required) unchanged pre-existing parked fault (`elr=0 far=0`).

## Scope / reproducibility

- donor → `FEX` @ `86ff33bbe`; gitignored quarry; no donor edits
- DLL → `kernel-arm/chalice/stock/wow64-chain/libwow64fex.dll`; PE ARM64; ImageBase `0x180000000`
- mapper → `kernel-arm/chalice/map-fex-offsets.sh`
- default invocation → `kernel-arm/chalice/map-fex-offsets.sh`
- alternate → `map-fex-offsets.sh DLL OFFSET...`
- env inputs → `FEX_DLL`; `FEX_OFFSETS`; `LLVM_BIN`; `FEX_SOURCE`; `FEX_DISASM_BEFORE`; `FEX_DISASM_AFTER`
- ladder → COFF symbols/exports/debug directory (`llvm-nm`, `llvm-readobj`) → ARM64 `.pdata` unwind starts (`llvm-readobj --unwind`) → branch-label/source identifier xrefs + donor definition grep → offset-centred `llvm-objdump` window
- static-only → no QEMU; no build; no zoneA edits

## Results

```text
0x1545DC => FEXCore::Assert::ForcedAssert() (FEXCore/Source/Utils/ForcedAssert.cpp:5) confidence high
0x165D14 => FEX::Windows::CPUFeatures::FetchHostFeatures(bool) (Source/Windows/Common/CPUFeatures.cpp:59) confidence high
```

- rung-1 → exact retained COFF `T` symbols; symbols=7258; exports=22; CodeView debug-directory present
- rung-2 → `.pdata` enclosing start `0x1801545dc`; `0x180165ae8` respectively; agrees with symbols
- `+0x1545dc` window → `hlt #1`; immediate fault instruction; donor `FEXCore/Source/Utils/ForcedAssert.cpp:5-9`
- `+0x165d14` window → `+0x165d0c` logging helper call; `+0x165d10` `bl ForcedAssert`; `+0x165d14` `brk #1`; LR = deliberate trap continuation, not JIT code

## Fault chain

```text
wow64.dll → BTCpuProcessInit
  → FEX::Windows::CPUFeatures::FetchHostFeatures(IsWine)
  → OpenProcessorKey(0)
  → RegOpenKeyExA(HKLM, "Hardware\\Description\\System\\CentralProcessor\\0", KEY_READ)
  → NtOpenKeyEx; null HKEY → ERROR_AND_DIE_FMT("Couldn't detect CPU features")
  → LogMan::Msg::MFmt → FEXCore::Assert::ForcedAssert → HLT/BRK
```

- FEX entry/call → `FEX/Source/Windows/WOW64/Module.cpp:510-535`
- key construction/open → `FEX/Source/Windows/Common/CPUFeatures.cpp:15-21`; failed key branch → `:59-63`
- assert macro → log + `FEX_TRAP_EXECUTION` → `FEX/FEXCore/include/FEXCore/Utils/LogManager.h:147-151`; trap target → `FEX/FEXCore/Source/Utils/ForcedAssert.cpp:5-9`
- Win32→NT bridge: `RegOpenKeyExA` → `NtOpenKeyEx` → `FEX/Source/Windows/Common/WinAPI/Misc.cpp:126-145`; NT declaration → `FEX/Source/Windows/include/winternl.h:519`

## C4-16 registry floor — live

- source → `01dfce9` + `606c2de`; `zoneA/ntfile.c`, `ntsvc.c`, `ntsvc.h`, `chalice/ntids.h`
- open route → FEX `RegOpenKeyExA` → `NtOpenKeyEx` → HKLM `\\Registry\\Machine` root pseudo-handle → root-relative `Hardware\\Description\\System\\CentralProcessor\\0` → CentralProcessor\\0 pseudo-handle; exact/case-insensitive; CentralProcessor\\1 → `STATUS_OBJECT_NAME_NOT_FOUND` → finite donor enumeration
- value route → FEX `RegGetValueA` → `NtQueryValueKey(..., KeyValuePartialInformation=2, ..., 512, ...)`; floor validates class; writes `KEY_VALUE_PARTIAL_INFORMATION` `[TitleIndex=0, Type=REG_QWORD(11), DataLength=8, Data=raw-mrs]`; unknown CentralProcessor names → `STATUS_OBJECT_NAME_NOT_FOUND`
- CP packing derivation → Windows `CP xxxx` = `((op0-2)<<14)|(op1<<11)|(CRn<<7)|(CRm<<3)|op2` for AArch64 `S<op0>_<op1>_C<CRn>_C<CRm>_<op2>`; checks: `S3_0_C0_C6_0` → `0x4030` → `ID_AA64ISAR0_EL1`; `S3_3_C0_C0_1` → `0x5801` → `CTR_EL0`
- served → `4000` MIDR_EL1; `4020` ID_AA64PFR0_EL1; `4021` ID_AA64PFR1_EL1; `4024` ID_AA64ZFR0_EL1 (`S3_0_C0_C4_4`: raw spelling; freestanding assembler lacks `+sve` alias); `4030` ID_AA64ISAR0_EL1; `4031` ID_AA64ISAR1_EL1; `4032` ID_AA64ISAR2_EL1; `4038` ID_AA64MMFR0_EL1; `4039` ID_AA64MMFR1_EL1; `403A` ID_AA64MMFR2_EL1; `5801` CTR_EL0
- IRON → every value → direct EL1 `mrs`; no model constants
- live `pe10 fex` → banner; CentralProcessor\\0 success; all 11 reads; CentralProcessor\\1 NOT_FOUND; **past `FetchHostFeatures` / `ForcedAssert`**
- superseded → § C4-17 forensic receipt below

## C4-17 JIT frontier — live

- source → `09628c7` → `acfcf85` → `3c38eea`; `zoneA/boot.S`, `kmain.c`, `ntfile.c`, `ntfile.h`
- forensic contract → raw `ESR_EL1`; EC/ISS; ELR/FAR/SPSR; safe fault-instruction read; `nt_fault_region(label, addr)` → image / reserve-commit arena state / VM region / LOW territory / HIGH pool
- first JIT-adjacent fault → raw ESR `0x96000006`; EC `0x25` Data Abort current EL; ISS `0x6`; ELR `0x4B8048A0`; FAR `0x2909C0008`
- mapper → `map-fex-offsets.sh ... 0x148A0` → `FEXCore::LookupCache::FindBlock(FEXCore::Core::InternalThreadState*, unsigned long long)`; `Dispatcher.cpp:64`; confidence high; fault instruction → `ldr x8, [x22,#8]`
- ELR class → `image libwow64fex.dll+0x148A0`; FAR class → reserve-commit arena RESERVED; reservation base `0x2881C0000`; size `0x9800000`
- demand trace → seen; EC gate accepted; fired; arena 2MB window committed; `eret` retry. C4-15 handler correct; EC decoder correct.
- successor → raw ESR `0x02000000`; EC `0`; ISS `0`; SPSR `0x20000345`; ELR `0x2681B02F0`; instruction `0xD4400000` → `hlt #0`; FAR remains prior `0x2909C0008`, stale/no HLT data access
- ELR class → reserve-commit arena COMMITTED; dispatcher allocation base `0x2681B000`; size `0x4000`; offset `0x2F0`
- source identity → FEX `Dispatcher.cpp:379-385`: `SignalHandlerReturnAddress`; `hlt(0)` signal-return dance; Windows path → `BTCpuResetToConsistentState` (`WOW64/Module.cpp:871-955`) + `NtContinue`; GAIA missing synchronous Windows exception dispatch; distinct from demand commit
- furthest → FEX `LookupCache::FindBlock` reached; its sparse-arena first touch committed + resumed; halted at dispatcher SEH trampoline; no hello8 marker yet
- regression → pe8 `ret=78`; pe9 wowbox64 + FEX; ramfb → PASS

## C4-18 synchronous exception contract — measured / implementation

### Native ARM64 ntdll dispatcher

- tool → `/opt/homebrew/opt/llvm@20/bin/llvm-objdump`; stock `ntdll.dll`; ImageBase `0x180000000`; export RVA `0x66c40`
- measured entry:

```text
180066c40 <KiUserExceptionDispatcher>:
  adrp x16, ...; ldr x16, [x16,#0xf98]; cbz x16, +0x18
  add x0, sp, #0x3b0
  mov x1, sp
  blr x16
+0x18:
  add x0, sp, #0x3b0
  mov x1, sp
  bl dispatch_exception
  brk #1
```

- entry contract → `sp+0x000` ARM64 `CONTEXT` `0x390`; `sp+0x390` `CONTEXT_EX` `0x20`; `sp+0x3b0` `EXCEPTION_RECORD64` `0x98`; `x0=record`; `x1=context`; dispatcher → `dispatch_exception` → internal `RtlDispatchException`/continue-or-raise path
- Wine donor → `dlls/ntdll/unix/signal_arm64.c:140-150` `exc_stack_layout`; `:221-235` `context_init_empty_xstate`; `:753-779` trap setup; `:834-850` syscall setup; exact size `0x460`; stack = `(context->Sp & ~15) - 0x460`; redzone `[+0x450,+0x460)`
- status citations → `wine/include/winnt.h:630` `STATUS_BREAKPOINT=0x80000003`; `:634` `STATUS_ACCESS_VIOLATION=0xc0000005`; `:638` `STATUS_ILLEGAL_INSTRUCTION=0xc000001d`; record shape `:2416-2446`; ARM64 context offsets `:1735-1742`, `:1851-1918`

### FEX / WOW64 expected round-trip

- `wine/dlls/wow64/syscall.c:1497-1553` → `Wow64PrepareForException(rec,context)` → `pBTCpuResetToConsistentState(&{rec,context})`; then `Wow64PassExceptionToGuest`
- FEX `Source/Windows/WOW64/Module.cpp:871-949` → AV fast paths: CallRetStack / OvercommitTracker / JIT guard; otherwise reconstruct WOW64 context + guest record; host context replaced with `TLS.EntryContext`; `:952-958` invokes `NtContinue(context,FALSE)` when handled
- success signature → C4-17 `hlt #0` at JIT dispatcher becomes `CHALICE EXCEPTION code=C000001D`; stock ntdll enters WOW64/FEX; FEX either `NtContinue`s a handled host-side condition or progresses through `BTCpuSimulate` / 32→64 thunk

### Floor design / source

- `boot.S` → sync frame widened `0x100` GPR + `0x200` q0-q31; order: raw FEX advisory SVC → arena demand-commit → guest exception → forensic fatal; dispatch preserves guest `x18`, switches `sp`, `eret`s to measured ntdll entry
- `kmain.c` → guest-only classifier: kernel image range remains fatal; known PE image / VM / arena accepted; EC `0x24/0x25` → AV `{WnR, FAR}`; EC `0x3c` BRK → breakpoint; EC `0x00` HLT → illegal instruction; builds `CONTEXT`, `CONTEXT_EX`, `EXCEPTION_RECORD64`; logs `CHALICE EXCEPTION`
- `ntdisp.S!NtContinue` → `eret` restore: `x0-x30` incl. x16/x17; `sp`; `pc`; `spsr`; q0-q31; FPCR/FPSR; no former partial-context loss
- `NtRaiseException` → retains diagnostic line; redirects via same native dispatcher stack, no terminate path
- build → `GAIA_NO_RUN=1 bash build.sh` PASS

### Live outcome — `39a7d1f`, 2026-07-18

- required-green → `pe8_gate` PASS (`ret=78`); `pe9_gate` PASS (wowbox64 + FEX); `ramfb_gate` PASS
- `pe10 fex` → former frontier delivered: `CHALICE EXCEPTION: code=0xC000001D elr=0x2681B02F0 dispatch=0x47086C40`; no kernel forensic termination at first HLT
- FEX outcome → **not handled / not progressed**; 20,816 repeated deliveries of same arena `hlt #0`; no `hello8_x86` / WriteFile marker
- `NtContinue` first-12 handoff trace → zero lines during loop; stock dispatch did not invoke floor `NtContinue`; loop precedes the floor continuation restore → next frontier = WOW64/ntdll dispatcher-handler registration / return disposition, not a new FEX DLL-relative fault
- mapping law → no new DLL-relative ELR; `map-fex-offsets.sh` not applicable; existing HLT identity remains `FEXCore Dispatcher.cpp:379-385`

## C4-20 execution truth — Theory B; CONTEXT return frontier

- source → `8afc109`→`cef4b61`; rig → `GAIA_GDB_SOCKET` opt-in, default off; `chalice/pe10_trace.py`; raw RSP serial-derived bases; `RtlRestoreContext` export RVA=`0x67564` from `llvm-objdump -p ntdll.dll`
- live bases → `ntdll=0x47020000`; `wow64=0x4a6c0000`; `libwow64fex=0x4b7f0000`; targets → dispatcher=`0x47086c40`; wow64 hook=`0x4a6dda18`; FEX reset=`0x4b92423c`; HLT=`0x2681b02f0`; RtlRestoreContext=`0x47087564`
- RSP receipt → `qSupported=PacketSize=20020;qXfer:features:read+;vContSupported+;multiprocess+`; no `swbreak+`/`hwbreak+`; this QEMU build accepts initial stop/continue/read-register traffic but does not reply to `Z0`/`Z1`; harness retains standard packets, serial capture avoids UART back-pressure; breakpoint hit-order is therefore UNVERIFIED on this host
- register truth → repeated `CHALICE ERET TRUTH: elr_el1=0x47086c40`; boot.S rewrites the recorded ELR immediately before `eret`; Theory A falsified — hardware return target is the native dispatcher, not original HLT
- returned context → next delivery reads retained prior context before overwriting: `flags 0x220->0; pc 0x2681b02f0->0; sp 0x4116b6e0->0`; userspace chain consumes/zeros the delivery context, then HLT recurs; Theory B holds: native dispatcher/WOW64/FEX path executes without a floor service, but no valid continuation context survives for restore
- donor chain → Wine `dlls/wow64/syscall.c:1497-1553`; FEX `Source/Windows/WOW64/Module.cpp:871-958`; next fix → identify the context owner that zeros/replaces host ARM64 CONTEXT, then construct required `ContextFlags`/`CONTEXT_EX` against those paths; no hello8 WriteFile marker
- green → build PASS; pe8 ret=78; pe9 both backends PASS; ramfb PASS; pe10 fex remains repeated HLT

## C4-21 CONTEXT construction + dispatcher marker — live

- commits → `3044132` context/ex/bracket rig; `11a1244` dedicated sync C stack
- donor flags → Wine `dlls/ntdll/unix/signal_arm64.c:309-322` `save_context()` → `CONTEXT_FULL`; Wine `include/winnt.h:1735-1745` → ARM64 base=`0x400000`, FULL=`0x400007`; floor → `ARM64_CONTEXT_FULL=0x00400007`, no debug/X18 request bits
- donor fields → `signal_arm64.c:314-321` → Fp/Lr/Sp/Pc/Cpsr=PSTATE, X0..X28, FP/SIMD; floor → SPSR_EL1→Cpsr; frame→X0..X30; q0..q31; FPCR/FPSR. Live first delivery → `flags=0x400007 cpsr=0x20000345 pc=0x2681b02f0 sp=0x4116b6e0`
- CONTEXT_EX → real header, not padding: `signal_arm64.c:140-150`; initialized by `:221-235`, used by `:771-778`/`:841-849`. Floor layout → CONTEXT `0x390`; EX `+0x390`; record `+0x3b0`; `All={Offset=-0x390,Length=0x450}`; `Legacy={-0x390,0x390}`; `XState={+0xc0,0}`
- named construction bug → sync frame=`0x300` + `sync_try_guest_exception` C frame=`0x1d0`; both on interrupted guest stack; delivery at `orig_sp-0x460` overlapped C locals. Receipt before stack isolation → written flags corrupted to `0x170`, Cpsr=`0`; prior `0x220` likewise corruption, not ARM64 flags. Fix → `sync_exception_stack[0x4000]`; saved guest frame retained; C runs separately; delivery remains below original guest SP
- live dispatcher dump → `pe10_trace.py`; `qSupported:multiprocess+;swbreak+;hwbreak+`; reply lacks sw/hw; `Z0` still no reply. Raw `[0x2681b0000,+0x4000)` → `/tmp/gaia-fex-dispatcher.bin`; receipt `+0x2f0=000040d4` → `hlt #0`; next `+0x2f4` prologue
- `+0x2f0` identity → **SignalHandlerReturnAddress**; FEX emitter order `FEXCore/Source/Interface/Core/Dispatcher/Dispatcher.cpp:379-384`; RT marker next `:387-392`; guest SIGILL after `:395-402`. Intended marker action → signal-return fault dance, not x86 unimplemented instruction; Windows reset treats dispatcher PC as JIT (`Source/Windows/WOW64/Module.cpp:161-173`, `:921-949`): reconstruct WOW64 state, copy `TLS.EntryContext`, return false; `NtContinue` only true-return fast paths (`:952-958`)
- zeroer bracket #1 → HLT delivery → next fault `libwow64fex+0x11b28`: `RETURN flags 0x400007->0x400007; pc 0x2681b02f0->0x2681b02f0; sp 0x4116b6e0->0x4116b6e0`; delivery survived; execution passed `+0x2f0`
- new fault → `libwow64fex+0x11b28` = `FEXCore::Context::ContextImpl::ReconstructCompactedEFLAGS`, `FEXCore/Source/Interface/Core/Core.cpp:177`; FAR=`0x409`; `Thread->CurrentFrame` null at `Core.cpp:179` dereference; § named, no speculative floor change
- zeroer bracket #2 → second AV delivery valid; first subsequent `nt_dispatch`=`id 67`/NtContinue sees `flags=0x400007 pc=0 sp=0 x18=0`; zero/replacement lies between second native delivery and NtContinue; donor candidate `Module.cpp:946-949` `memcpy(Context,TLS.EntryContext(),...)`; owner exact still UNVERIFIED
- furthest → first SignalHandlerReturn round-trip survives; FEX reaches `+0x11b28`; no `hello8_x86`/WriteFile marker
- green → build PASS; pe8=`ret=78`; pe9=both backends PASS; ramfb PASS

## C4-19 exception-cycle autopsy — UNRESOLVED; source probes reverted

- measurement commit → `c855240`; live `pe10 fex` → 20,811 cycles / 30s; cycle #1→#2 service trace = **zero** `nt_dispatch` entries; `NtRaiseException=0`; `NtContinue=0` → C4-18 second-chance suspect falsified; no service re-dispatch
- ntdll hook → stock disasm `adrp x16, ntdll+0x152000; ldr x16,[x16,#0xf98]` → slot RVA `0x152f98`; live → `ntdll=0x47020000`, slot=`0x47172f98`, ptr=`0x4a6dda18` = `wow64.dll+0x1da18` (`Wow64PrepareForException`) → loader `init_wow64` + `RtlFindExportedRoutineByName` link live
- next link → `wow64.dll!Wow64PrepareForException`: `adrp x8,wow64+0x40000; ldr x8,[x8,#0x330]; blr x8`; live slot=`0x4a700330`, target=`0x4b7f423c` (libwow64fex mapped range) → `pBTCpuResetToConsistentState` link initialized; no silent init failure
- donor → Wine `loader.c:4327,4346-4357`; `signal_arm64.c:323-331`; `wow64/syscall.c:1535-1540`; FEX `Module.cpp:921-954`; FEX only calls `NtContinue` when reset impl returns true; normal JIT exception path reconstructs + returns false for ntdll guest dispatch
- failed diagnostic bridges → `4ef9109`/`e4b8bef`/`a288150`/`0c1f254`/`02f4828`; target bridge + direct wow64-hook probe never observed after floor stack setup; source reverted by `f15272e` chain → do not retain speculative control-transfer fix
- frontier → hardware HLT post-handler resume/control-transfer; floor builds stack + logs delivery but dynamic target never executes; establish minimal same-EL transfer model against QEMU ARM HLT semantics before touching FEX CONTEXT/flags; `NtRaiseException` chance semantics unchanged because its path did not occur

## C4-24 — STATUS_INVALID_DISPOSITION loop — RESOLVED

- symbols resolved via `map-fex-offsets.sh` against **both** donors (script is DLL/offset-parameterized; `FEX_SOURCE` env override points it at wine for ntdll, default FEX for libwow64fex):
  - `map-fex-offsets.sh stock/ntdll.dll 0x3868C` (`FEX_SOURCE=<repo>/wine`) → `raise_status+0x18` (wine `dlls/ntdll/exception.c:358-367`; ARM64 body `dlls/ntdll/signal_arm64.c`), confidence high, embedded-symbol
  - `map-fex-offsets.sh stock/wow64-chain/libwow64fex.dll 0x34E4` (default FEX_SOURCE) → `BTCpuSimulate+0x10` (`FEX Source/Windows/WOW64/Module.cpp:757-777`), confidence high, embedded-symbol
- poison-pc `0xFFFFFFFFFFFFFEFE` — grepped as a literal/pattern (`0xFEFE`, `~0x101`, wow64-transition markers) across both donor quarries: **no hit**. Never observed in the C4-24 instrumented capture either. Retired — was a mischaracterization of `raise_status+0x18`'s own resolved identity, not a distinct sentinel value.
- loop anatomy (instrumented `NtRaiseException`, `2db734c`, capped 24 calls, full record + `FirstChance` + `CONTEXT.pc/sp/lr`): `RAISE#1` = `dispatch_exception`'s (`exception.c:369`) own second-chance tail call — `firstchance=0`, `code=0xC000001D`, `ctx.pc=BTCpuSimulate+0x10`, `addr=0` (FEX's `Wow64PrepareForException`→`BTCpuResetToConsistentStateImpl` WOW64-boundary reconstruction, `Module.cpp:871-958`, anchors the record/context on `TLS.EntryContext()` and zeroes `ExceptionAddress`). `RAISE#2..#18` = wine's own `raise_status`(`exception.c:358-367`)/`RtlRaiseException`(`signal_arm64.c:745-771`) defensive retry loop, re-entered by THIS FLOOR on every cycle: `firstchance=0`, `code=0xC0000026`, `ctx.pc==ctx.lr==ntdll+0x3868C` every time (identical), `ctx.sp` monotonically decreasing ~`0x6c0`B/cycle — real unbounded guest C-stack recursion, one full `dispatch_exception`/`call_seh_handlers`/`raise_status` lap per floor syscall round-trip, not a "same NT exception record re-raised" nor a "new hardware-cycle first-chance exception" in the Windows sense.
- root cause — `dispatch_exception()` (`exception.c:239-370`) is the *only* donor call site ever passing `FirstChance=FALSE` (its own tail, reached when `call_seh_handlers` returns `STATUS_UNHANDLED_EXCEPTION`); on real Windows this is the terminal second-chance report (no debugger here, `PEB.BeingDebugged=0` per this floor's own TEB/PEB build) — it does not re-run `KiUserExceptionDispatcher`. This floor's `NtRaiseException` (`ntsvc.c`) ignored `FirstChance` and unconditionally called `nt_raise_guest_exception()`, re-dispatching the already-unhandled `BTCpuSimulate`-anchored context a second time and triggering wine's own `raise_status` retry loop.
- ruled out (evidence-based, not assumed): `.pdata`/exception-directory fidelity for any mapped module — not implicated, `call_seh_handlers` legitimately returning `STATUS_UNHANDLED_EXCEPTION` for a JIT-internal fault with no SEH frame is correct Windows behavior; our synthesized exception-frame/CONTEXT_EX linkage — not implicated, `RAISE#1`'s context is internally consistent (matches the independently-probed `EntryContext.pc/sp` exactly, C4-23); TEB `NtTib.StackBase`/`StackLimit` — not implicated, `call_seh_handlers`'s `.pdata`-based `LanguageHandler` search path (the one actually hit here) doesn't consult TEB stack bounds at all (only the TIB-frame-chain fallback branch does, unused for ARM64 PE code).
- fix (`ec206c8`) — `ntsvc.c` `NtRaiseException`: `FirstChance==0` → diagnostic print + clean terminate (`nt_setjmp`/`nt_longjmp`, same channel as `NtTerminateProcess`/`NtRaiseHardError`), no dispatcher re-entry. `FirstChance!=0` unchanged.
- live `pe10 fex` (post-fix) — loop gone: exactly one `RAISE#1` (`code=0xC000001D`), immediate clean terminate, `pe10 ret=3221225501` (`=0xC000001D`). Re-gate: `pe8_gate` `ret=78`, `pe9_gate` both backends, `ramfb_gate` — all PASS, no regression.
- furthest point / new frontier — no `hello8_x86`/WriteFile marker; `STATUS_ILLEGAL_INSTRUCTION` (`0xC000001D`) itself, raised at the `BTCpuSimulate`/`TLS.EntryContext` anchor, `addr=0` — plausibly FEX treating the arena-dispatcher `hlt #0` `SignalHandlerReturnAddress` marker (`FEXCore/Source/Interface/Core/Dispatcher/Dispatcher.cpp:379-384`) as a genuine guest fault instead of completing its internal signal-return dance. Out of this task's scope (loop-breaking only) — next worker's frontier, now a clean one-line diagnosis instead of a hang.

## C4-26 — LdrSystemDllInitBlock populate fix committed but UNEXERCISED this boot path; RAISE#1 guest content now proven REAL — new receipt, no new regression

- commits (prior worker, salvage-resumed) → `3f81099` (C4-25 instrumentation: live WOW64_CPURESERVED I386_CONTEXT dump + guest-memory hexdump at RAISE#1) + `4c1b0cc` (C4-26 fix: `nt_wow64_populate_init_block_if_ntdll32`, hooked at `NtMapViewOfSection`, ntfile.c) — both UNGATED by the prior worker (30-min wall hit right after commit); this pass gates + measures them, no further code changes.
- build → `GAIA_NO_RUN=1 bash build.sh` clean. Required-green re-gate, no regression: `pe8_gate` `ret=78` PASS; `pe9_gate` PASS both backends (`wowbox64` `ret=0`, `fex` `ret=0`); `ramfb_gate` PASS (7/7 pixel checks).
- **measured: the C4-26 hook never fires in `pe10 fex`'s actual boot path** — grepped the full transcript for its own diagnostic line (`"CHALICE C4-26: LdrSystemDllInitBlock populated..."`): **zero occurrences**. Cross-checked against every `CHALICE IMAGE: name=...` line in the run: `ntdll.dll` (native, Machine `0xAA64`, base `0x47020000`), `hello8_x86.exe`, `wow64.dll`, `libwow64fex.dll`, `wow64win.dll`, `win32u.dll` — **no `syswow64/ntdll.dll` (or any 32-bit-named `ntdll.dll`) is ever mapped** during this boot. Root cause: `nt_boot_process` (`ntboot.c:258-314`) maps exactly ONE ntdll — the literal `ntdll_name` argument `pe10`'s shell command passes (`shell.c:249`, hardcoded `"ntdll.dll"`, always the **native ARM64** blob) — via a direct `PeMapImage` C-call (Phase-0, bypasses `NtCreateSection`/`NtMapViewOfSection` entirely by design, per this file's own comment: *"no Ldr walk here to resolve them against anyway"*). A genuine 32-bit `syswow64/ntdll.dll` module is only ever mapped by the **self-test** probe (`nt_pe32_section_selftest_one`, `ntboot.c:757`, exercised by `pe9`, not `pe10`). `pe10`'s WOW64 bootstrap (`nt_wow64_set_initial_context` / `nt_wow64_cpureserved_set_context`, `ntboot.c:450,456`) seeds `Eip=exe_entry` **directly in C**, never running a real 32-bit ntdll init chain that would call through `LdrSystemDllInitBlock->pLdrInitializeThunk` — so C4-26's hook has no 32-bit-ntdll `NtMapViewOfSection` call to attach to in this flow. Not a bug in the fix itself (donor-grounded, correctly gated on Machine `0x14C`) — it is simply dead code on `pe10`'s current path; live only if/when a future worker adds a real dynamic 32-bit-ntdll load.
- **despite the hook never firing, RAISE#1's guest content is now REAL, not zero** — `CHALICE C4-25 GUEST CTX: Eip=0x47661000 (hello8_x86.exe+0x1000) Esp=0x4768C000 Ebp=0x4768C000 Cs=0x1B Ss=0x23 EFlags=0x202`, all other GPRs 0 (fresh-thread state, expected). `Eip` resolves cleanly to the mapped exe image + the C-seeded entry offset — matches `nt_wow64_cpureserved_set_context(cpureserved, exe_entry, x86_stack_esp)`'s own boot-time write (`ntboot.c:456`), read back intact.
- **guest-memory hexdump confirms real, valid x86 code at that Eip** (`CHALICE C4-25 GUEST BYTES @Eip-16..+32`, words at offsets 0/+4/+8 = `0xB8E58955 0x0000002A 0x0000C35D`) → little-endian bytes `55 89 E5 B8 2A 00 00 00 5D C3` = `push ebp; mov ebp,esp; mov eax,0x2A; pop ebp; ret` — a textbook "return 42" stub, i.e. `hello8_x86.exe`'s real compiled entry code, correctly mapped and byte-for-byte intact at the address the guest CPU state names as its own Eip.
- **separate, distinct zero-write observed mid-init, NOT the value RAISE#1 reads back**: `NtSetInformationThread ThreadWow64Context <- eip=0x0 esp=0x4768BD20` (a *different* Esp than the boot-seeded `0x4768C000` — `0x2E0` lower, consistent with a callee frame a few pushes deep) appears twice before the first ARENA fault, and its own `NtQueryInformationThread` readback also returns `eip=0x0`. This matches C4-26's own theorized mechanism (`LdrSystemDllInitBlock.pLdrInitializeThunk` still zero, since the hook never ran) for **that specific SetContext call site** — but it is a different code path than the one whose content RAISE#1 later reports (which reads real `0x47661000`). Whether this zero-Eip write is ever actually dispatched into (i.e., FEXCore genuinely executes at guest RIP 0 as a result) or is an inert/overwritten intermediate value is **UNVERIFIED this pass** — flagged, not chased (would need new instrumentation at the FEXCore `ExecuteThread` boundary itself, out of this salvage's "do not re-instrument beyond measuring the existing fix" scope).
- **outcome vs success ladder**: **(a) achieved** — Eip is real (not 0) and the guest code at it is valid, mapped, correct x86; FEX is not executing/faulting on garbage. **(b) NOT achieved** — no `hello8_x86` WriteFile marker; process still terminates via `NtRaiseException FirstChance=FALSE`, `pe10 ret=3221225501` (`=0xC000001D` unsigned), **identical exit code and `RAISE#1` identity** (`code=0xC000001D`, `ctx.pc=libwow64fex+0x34E4`=`BTCpuSimulate+0x10`, `addr=0`) to the state C4-24 already documented as its own frontier *before* C4-25/C4-26 ever started — this specific STATUS_ILLEGAL_INSTRUCTION/unhandled-terminate behavior is **not new**, and is not the same thing C4-26 targeted (that was guest-content-is-garbage; guest content is now proven good). The real remaining gap is architectural, already named in C4-18/C4-21's own docs: FEX's dispatcher `hlt #0` (`SignalHandlerReturnAddress`, `Dispatcher.cpp:379-384`) is a **host-signal-return marker**, meant to be absorbed by a Linux-side `SIGILL` handler internal to FEX and never reach NT SEH dispatch at all on real Windows/Wine; this floor instead routes it through a full synthetic `KiUserExceptionDispatcher` → `dispatch_exception` → `call_seh_handlers` cycle, which (correctly, per wine's own donor logic) finds no SEH frame for a JIT-internal marker and falls through to the unhandled/terminate tail. Fixing this needs a different mechanism (an FEX-internal-fault short-circuit rather than full guest SEH injection) — a genuinely new design question, not a small named field/service gap; **left named, not attempted**, per this task's "no speculative changes" law.
- **ladder classification: (c)** — new, more precise receipt; narrows the frontier (rules out bad Eip/garbage-guest-code as the cause) without changing the measured outcome. No new DLL-relative fault address appeared (`ctx.pc` is the same previously-mapped `BTCpuSimulate+0x10` identity) — `map-fex-offsets.sh` not re-run, nothing new to map.

## C4-22/C4-23 — TLS probe receipts + x18/CONTEXT-clobber root cause — RESOLVED

- probe commits → `d9e98b8` (TEB64.TlsSlots probe), `9239e3d` (RAM-bounded read fix); fix commit → `1184e5b`
- donor cite → TEB64.TlsSlots @ `0x1480` (wine `include/winternl.h:1238`); WOW64_TLS_MAX_NUMBER=19, THREAD_STATE=16/CONTROL_WORD=17/ENTRY_CONTEXT=18 (FEX `Source/Windows/include/winternl.h:28-29`, mirrored `Source/Windows/WOW64/Module.cpp:76-80,101-108`)
- receipt #1 (first HLT delivery, `+0x2f0`) → `x18=0x4116C120` (real TEB); `slot16(ThreadState)=0x270001000`; `slot18(EntryContext)=0x4116B7B0`; `ec.pc=0x4B7F34E4 ec.sp=0x4116B7A0 ec.x18=0x4116C120` — all sane, `ec.x18==x18`. **BTCpuThreadInit RAN**; TLS correctly populated. TEB_SIZE=0x1600 audited vs slots 16-18 offsets (0x1500-0x1518) — no truncation.
- receipt #2 (second fault, `libwow64fex+0x11b28` `ReconstructCompactedEFLAGS`, FAR=`0x409`) → `x18=0x0`, direct hardware-register capture (`SAVEALL_SYNC`, not a memory deref) — zero intervening floor syscalls between receipt #1 and #2 (verified via transcript: no `nt_dispatch`/other `CHALICE ARENA` lines in between).
- FEX source confirms load-bearing chain → `ReconstructCompactedEFLAGS(Thread,...)` (`FEXCore/Source/Interface/Core/Core.cpp:177-179`) derefs `Thread->CurrentFrame`; `Thread` is a **parameter**, supplied by `BTCpuResetToConsistentStateImpl`/`ReconstructWowContext` via `TLS.ThreadState()` = `TEB->TlsSlots[16]` (`Module.cpp:101-102`); `TEB` = `GetTLS().TEB` = `NtCurrentTeb()` = ARM64 platform register **x18** (Windows ABI convention, never touched by ABI-compliant native code). x18=0 at guest-dispatch entry ⇒ TEB=NULL ⇒ garbage/near-null `Thread` after the (non-faulting, since address ~0x1500 lands in identity-mapped low kernel memory) `TlsSlots[16]` read ⇒ eventual fault at the observed `FAR=0x409` once `Thread->CurrentFrame`-derived arithmetic dereferences.
- **root cause, address-arithmetic-grounded (kmain.c/boot.S/sync_frame.h):** `guest_exception_stack()` zero-fills `[orig_sp-0x460, orig_sp-0x18)` (`ARM64_EXCEPTION_STACK=0x460`). `SAVEALL_SYNC` (boot.S) captures x0-x30+q0-q31 at `x21=[orig_sp-0x300, orig_sp)` (`SYNC_FRAME_SIZE=0x300`). Both ranges end at `orig_sp`; `0x460 > 0x300` ⇒ the CONTEXT zero-fill **fully contains** the register-capture frame. `sync_try_guest_exception`'s GPR-copy loop + Q `move_bytes` (kmain.c) ran *after* `guest_exception_stack()`, reading zeroed memory into the delivered CONTEXT; `boot.S`'s pre-`eret` `ldr x18,[x21,#SYNC_FRAME_X18_OFF]` read the same clobbered bytes. Every guest exception, not just this one, delivered x18=0.
- **fix (`1184e5b`):** `sync_try_guest_exception` snapshots `regs[]` onto its own (already off-guest-stack, C4-21 `sync_exception_stack`) C frame *before* calling `guest_exception_stack()`; ctx built from the snapshot. `boot.S` reloads x18 from the delivered CONTEXT struct (`x0`, `ARM64_CONTEXT_X18_OFF=0x098`, new `sync_frame.h` shared constant + compile-time assert) instead of the dead `x21` memory.
- **re-gate** → build clean; `pe8_gate` `ret=78`; `pe9_gate` both backends; `ramfb_gate`; all PASS, no regression.
- **pe10 fex outcome** → `libwow64fex+0x11b28` null-deref **gone**. New exception correctly reconstructed from real state: `NtRaiseException id=207 pc=0x4B7F34E4 sp=0x4116B7A0` (matches the earlier-probed `EntryContext.pc/sp` exactly — proof the delivered CONTEXT/TLS chain is now internally consistent end-to-end). Then `code=0xC000001D addr=0`, settling into a stable (non-crashing, no new fault addresses) loop: `NtRaiseException code=0xC0000026 (STATUS_ASSERTION_FAILURE) addr=ntdll.dll+0x3868C`, poison `pc=0xFFFFFFFFFFFFFEFE`, ~9200 iterations/45s. No `hello8_x86`/WriteFile marker yet — this is a **new, deeper frontier**, not the previous crash.
- next → resolve `ntdll+0x3868C` symbol (unresolved via static `llvm-objdump` this pass — needs export/pdb-adjacent lookup or live symbol walk) and instrument `NtRaiseException id=207`'s caller / the `0xFFFFFFFFFFFFFEFE` poison-pc origin (new instrumentation, explicitly deferred — this salvage's "do not re-instrument" law).
