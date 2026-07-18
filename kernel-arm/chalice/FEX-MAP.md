# CHALICE FEX map — Hangover 11.9

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
