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

## Floor requirement / candidate

- historical top candidate → `NtOpenKeyEx(KeyHandle, DesiredAccess, ObjectAttributes, OpenOptions)`; must accept HKLM root then root-relative `Hardware\Description\System\CentralProcessor\0`; failure is the sole direct branch condition at donor `CPUFeatures.cpp:59-63`
- current-tree audit → exact key route present: `kernel-arm/zoneA/ntfile.c:1564-1577`; `NtOpenKeyEx` registered/pass-through: `:1590-1616`; dispatch table: `kernel-arm/zoneA/ntsvc.c:1300-1302`
- consequence → mapped fault diagnoses an older/no-`NtOpenKeyEx` floor state; static analysis cannot claim current live clearance
- next required init state after open → registry values consumed unconditionally:
  - `CP 4030`, `4020`, `4021`, `4031`, `4038`, `403A`, `4024`, `4039`, `4032` → donor `FEX/Source/Windows/Common/CPUFeatures.cpp:42-53`
  - `CP 5801` / `CP 4000` → donor `:67-68`; per-core `CP 4000` enumeration/close → `:72-75`
  - value bridge → `RegGetValueA` / `NtQueryValueKey` → donor `FEX/Source/Windows/Common/WinAPI/Misc.cpp:147-184`; NT declaration → `FEX/Source/Windows/include/winternl.h:525`
- current floor supplies only `CP 4030`, `CP 4039`, `CP 403A` → `kernel-arm/zoneA/ntfile.c:1647-1682`; missing FEX-consumed values above → top post-open candidate; notably CTR/MIDR + all six omitted feature-register values
- priority → prove `NtOpenKeyEx` live first; then extend `NtQueryValueKey` CentralProcessor values from real ID/CTR register reads; no claim of a JIT-init fault from these two offsets
