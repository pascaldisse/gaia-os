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
