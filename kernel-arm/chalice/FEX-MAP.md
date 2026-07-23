# CHALICE FEX map — Hangover 11.9

## C4-34 — the NoExecOp-vs-enclosed paradox NAMED (measurement atom, no fix): the failing `CheckRangeExecutable` call queried **ByteAddress=0x0**, not `exe_entry`, not a valid-but-different host address — **verdict R1** (InstStream/GuestCode is structurally a HOST-pointer parameter, decoupled from the guest linear addr XIntervals stores; here the host-side translation itself came back null). Live 2026-07-23.

### Floor reader (kmain.c `sync_try_guest_exception`, `CHALICE C4-34`, chained directly off the live C4-33 `ThreadState`)
Offsets derived (not guessed) from disassembling `CheckRangeExecutable`/`ReadByte`/`PeekByte` in the non-stripped `libwow64fex.dll` (RVAs 0x1ce90/0x1cf34/0x1d000), matched field-for-field against `FEXCore/Source/Interface/Core/Frontend.h:123-135`: `InternalThreadState+0x28 = FrontendDecoder` (confirmed via `ContextImpl::CheckIfBlockIsCacheable`'s own `ldr x0,[x1,#0x28]`); `Decoder+0x68/0x70 = ExecutableRangeBase/End`, `+0x78/+0x79 = ExecutableRangeWritable/HitNonExecutableRange` (adjacent bools, one aligned 4-byte read), `+0x80 = InstStream`, `+0x88 = InstructionSize`. `ByteAddress = InstStream + InstructionSize + Offset(0)` — `PeekByte(0)`'s own first-byte-of-instruction query (`Frontend.cpp:127`).

### RECEIPT (pe10 fex, same self-fault/transcript as C4-30/31/33, commit `c5e0b9c` + this pass's boot)
```
CHALICE C4-34 ThreadState=0x270001000 Decoder=0x2300B0080 ExecutableRangeBase=0x0 ExecutableRangeEnd=0x0 ExecutableRangeWritable=0x0 HitNonExecutableRange=0x1 InstStream=0x0 InstructionSize=0x0
 CHALICE C4-34 ByteAddress(queried)=0x0 enclosed=0x0 exe_entry(0x47661000) match=0x0
```
`Decoder=0x2300B0080` sits in the same `0x230xxxxxx` heap arena as the already-validated `XIntervals` buffer (`begin=0x230040180`, C4-33) — internal cross-check the pointer/offsets are real, not garbage. `HitNonExecutableRange=1` byte-matches `Decoder::ReadByte` (`Frontend.cpp:116`, `HitNonExecutableRange = true;` on a failed `PeekByte`) exactly, at the exact field FEX's own source sets it — proof the offset derivation is correct, not coincidental.

### Donor re-read (this pass, `FEX/FEXCore/Source/Interface/Core/Frontend.cpp`, vendored locally)
- `PeekByte(uint8_t Offset)` (`:126-132`): `uint64_t ByteAddress = reinterpret_cast<uint64_t>(InstStream + InstructionSize + Offset);` — **`InstStream` is a host `const uint8_t*`, cast to an integer and queried directly against `XIntervals` (which stores GUEST addresses, proven C4-33)**. This is R1's exact structural claim, confirmed at the source level, not inferred.
- `CheckRangeExecutable` (`:90-108`): on ANY call (hit or miss) `ExecutableRangeBase=RangeInfo.Base; ExecutableRangeEnd=RangeInfo.Base+RangeInfo.Size;` unconditionally, THEN `if (RangeInfo.Size==0) return false;`. Our receipt shows Base=End=0x0 — i.e. `QueryGuestExecutableRange(Thread, Address=0x0)` returned the tracker's own `{Base:0,Size:0}` "not found" sentinel (`InvalidationTracker.cpp:177-190`, `XIntervals.Query(0x0)` — trivially unenclosed, the lowest interval starts `0x47020000`, C4-33). **Nothing about interval-insert TIMING is implicated** (`XIntervals` was already fully populated+enclosing `exe_entry` at this exact event, same transcript lines 916-918, C4-33) — ruling out R2's literal claim (which requires queried==`exe_entry`, not observed).
- `AdjustAddrForSpecialRegion` (`:1334-1347`) + `DecodeInstructionsAtEntry` (`:1358-1446`): for the ENTRY block (`RIPToDecode==EntryPoint`), `InstStream = _InstStream - EntryPoint + RIP = _InstStream` unmodified — i.e. `InstStream` for `exe_entry`'s own first byte equals `_InstStream` **exactly as passed in to `DecodeInstructionsAtEntry`**, which `Core.cpp:538` calls as `Thread->FrontendDecoder->DecodeInstructionsAtEntry(Thread, GuestCode, GuestRIP, MaxInst)` — the parameter is literally named **`GuestCode`**, FEX's own host-pointer translation of the guest RIP, structurally NOT the guest linear address `XIntervals` stores. Our receipt (`InstStream=0x0`, `InstructionSize=0x0`, first byte of a fresh block) shows this specific `GuestCode` translation came back **null** for `exe_entry` — the sharpest possible instance of "`InstStream` ≠ guest linear addr": not merely a differing valid host pointer, but entirely unpopulated.

### VERDICT: R1 (sharpened) — deciding tell
Queried address = `0x0`, **not** `exe_entry` (rules out R2's literal tell: "queried addr == exe_entry, wrong timestamp") and **not** a coincidental valid-but-wrong address either — it is the donor-proven identity of `GuestCode`/`InstStream`, a HOST-address-space quantity (`Core.cpp:538`, `Frontend.cpp:1358/1376/1446`) fundamentally decoupled from the GUEST-address-space `XIntervals` tree (C4-33) — exactly R1's structural claim. `HitNonExecutableRange=1` is FEX's own byte-exact confirmation this is the live path that fired. `ExecutableRangeBase=End=0x0` is the `{Base:0,Size:0}` not-found sentinel from querying `0x0`, not a stale/reset artifact (source shows both fields are written unconditionally on every `CheckRangeExecutable` call, hit or miss). No interval-timing/insert-ordering evidence anywhere in this receipt or C4-33's — R2 excluded on the facts, not by default.

### Re-gate
`pe8_gate` ret=78 · `pe9_gate` both backends ret=0 · `ramfb_gate` 7/7. Instrumentation-only (new receipt block chained off the existing C4-33 `ThreadState`/`Decoder` reads); `pe10 ret=0xC0000005` (`3221225477`) identical before/after.

## C4-33 — `XIntervals` live-dumped from the floor: it **DOES enclose `exe_entry`**. C4-31/32 "missing interval" thesis overturned; rivals (a)/(b)/(c) refuted; new NoExecOp-vs-enclosed paradox. Live 2026-07-23.

### Deriving the `InvalidationTracker` global address (no donor edit, no guess)
- libwow64fex.dll is **NOT stripped** — `objdump -t` shows a full COFF symbol table (methods `_ZN3FEX7Windows19InvalidationTracker*`, `WowSyscallHandler::QueryGuestExecutableRange`, `BTCpuProcessInit`, …). Major enabler: FEX-internal statics/methods are now addressable.
- The global `std::optional<FEX::Windows::InvalidationTracker> InvalidationTracker` (Module.cpp:130) is in an anon namespace → not in symtab. Recovered by disassembling its consumer `WowSyscallHandler::QueryGuestExecutableRange` (RVA 0x53f8):
```
1800053f8: mov  x1, x2
1800053fc: adrp x0, 0x18034c000
180005400: add  x0, x0, #0x430          ; x0 = &InvalidationTracker
180005404: b    0x18016a910 <InvalidationTracker::QueryExecutableRange>
```
  → `&InvalidationTracker = ImageBase 0x180000000 + 0x34c430` = **RVA 0x34c430**. std::optional stores its value inline; `XIntervals` is member 0 of `InvalidationTracker` (InvalidationTracker.h:55) → the `FEXCore::IntervalList<uint64_t>` = libc++ `std::vector<Interval>` `{Interval* begin@+0, Interval* end@+8, cap@+16}`; `Interval={uint64_t Offset@+0, uint64_t End@+8}` (IntervalList.h:11-23). `count=(end-begin)/16`.

### Floor reader (kmain.c `sync_try_guest_exception`, `CHALICE C4-33`)
`nt_image_base_by_name("libwow64fex.dll",&base,&size)` → `ivt=base+0x34c430`; read begin/end (each two `nt_fault_read32`, arena-aware since heap is >=4GB reserve-commit); iterate intervals, test `exe_entry(0x47661000)∈[Offset,End)`. Gated `static int ivt_dumped` (once).

### RECEIPT (pe10 fex, same self-fault as C4-30/31)
```
CHALICE C4-33 XIntervals ivt=0x4DD8C430 begin=0x230040180 end=0x230040200 count=8
 [47020000,47660000) [47660000,47665000) [48690000,488BD000) [4C910000,4CA30000)
 [4DA40000,4DDA2000) [4EDC0000,4EEF0000) [4FF00000,50030000) [58040000,58041000)
CHALICE C4-33 exe_entry(0x47661000) enclosed=1
```
Map: ntdll / **hello8_x86.exe** / ntdll32 / wow64 / libwow64fex / wow64win / win32u / BOP-trampoline — all 8 module EXECUTE ranges present, correctly spanned. `exe_entry` sits in `[0x47660000,0x47665000)`. Correlated same transcript: `C4-31 SynchronousFaultData=0x00000015020E0B01`=NoExecOp, `Eip=exe_entry Eax=0`, `pe10 ret=0xC0000005`.

### Verdicts
- rival (a) ctor sweep aborts early → **REFUTED** (8 complete intervals). (b) inserted-then-removed → **REFUTED** (present, full span, at fault). (c) HandleImageMap section-walk differs → **REFUTED** (present, correct).
- decode gate re-read: `QueryExecutableRange` (InvalidationTracker.cpp:177-190) — `XIntervals.Query(exe_entry).Enclosed==true` → non-zero Size → `CheckRangeExecutable` (Frontend.cpp:90-108) returns **true** → `ReadByte`/`PeekByte` never set `HitNonExecutableRange` → **frontend cannot emit `NoExecOp` for `exe_entry`**. But the self-fault byte-matches `NoExecOp` (OpcodeDispatcher.cpp:5018-5024, the only PROT|USER|INSTR BreakDefinition). **PARADOX, receipted.**

### New frontier (named, not chased)
NoExecOp fired for an addr NOT XIntervals-enclosed while reconstructed guest `Eip`=exe_entry (which IS). (R1) `PeekByte` queries the HOST `InstStream+off` pointer; if this block's `InstStream` ≠ guest linear addr, it lands outside XIntervals (guest addrs) → false. (R2) a pre-insert warm-up decode. Next: read per-thread `Decoder` state (`InstStream`/`HitNonExecutableRange`/`ExecutableRangeBase`) — now addressable via the non-stripped symtab — or enable LEAD-1 narration.

### Re-gate
`pe8_gate` ret=78 · `pe9_gate` both backends ret=0 · `ramfb_gate` 7/7. Instrumentation-only, `pe10 ret` unchanged.

## C4-32 — HYPOTHESIS REFUTED: the exe main-image "notification" already fires, twice, self-sufficiently; wiring `BTCpuNotifyMapViewOfSection` for the exe would be a no-op. Real gap unchanged from C4-31 (FEX-internal, unreached without donor edit) plus one new, testable secondary lead (FEX's own LogMan/DFmt narration is silent despite `FEX_SILENTLOG=0`, C4-15's own fix). No fix wired (measurement contradicts the brief's premise; wiring would be speculative/pointless per this project's own law). Runtime unchanged this pass (zero source edits — `pe10 ret=3221225477` identical before/after); live 2026-07-19.

### MEASURE 1 — donor notify surface, read end-to-end (file:line)

Five `BTCpu*` notify exports exist (`FEX/Source/Windows/WOW64/Module.cpp:970-1033`): `BTCpuNotifyMemoryDirty/Alloc/Protect/Free` (SMC/permission bookkeeping only — call `InvalidationTracker::HandleMemoryProtectionNotification`/`InvalidateAlignedInterval`, never register a NEW executable *image* range from a PE header) and **`BTCpuNotifyMapViewOfSection`** (`:1009-1013`):
```cpp
NTSTATUS BTCpuNotifyMapViewOfSection(void* Unk1, void* Address, void* Unk2, SIZE_T Size, ULONG AllocType, ULONG Prot) {
  std::scoped_lock Lock(ThreadCreationMutex);
  HandleImageMap(reinterpret_cast<uint64_t>(Address));
  return STATUS_SUCCESS;
}
```
— this IS the export the brief's hypothesis named, and its caller-side convention (wine `dlls/wow64/syscall.c`, real `NtMapViewOfSection` wow64 thunk calling the backend's `BTCpu` vtable entry post-syscall) matches the brief's own description. **But it is not the only, nor the first, registration path.** `HandleImageMap` itself (`Module.cpp:175-180`) is a thin wrapper:
```cpp
void HandleImageMap(uint64_t Address, bool MainImage = false) {
  fextl::string ModulePath = FEX::Windows::GetSectionFilePath(Address); // NtQueryVirtualMemory(MemoryMappedFilenameInformation) -- best-effort, name only
  fextl::string ModuleName = fextl::string {FEX::Windows::BaseName(ModulePath)};
  InvalidationTracker->HandleImageMap(ModuleName, Address);            // THE interval-tree write -- does not need ModuleName
  ImageTracker->HandleImageMap(ModulePath, Address, MainImage);
}
```
`InvalidationTracker::HandleImageMap` (`Source/Windows/Common/InvalidationTracker.cpp:63-92`) does **`RtlImageNtHeader(Address)` directly on live guest memory** — walks the DOS/NT header and section table AT THE ADDRESS PASSED, inserting every `IMAGE_SCN_MEM_EXECUTE` section's `[Address+VA, Address+VA+VirtualSize)` into `XIntervals`. It needs no file-backed section, no notify export, no `ModuleName` (`ModuleName` is used ONLY for a Mono-hack string compare, `:81-91`) — **a pure memory read**, callable from anywhere with the right address.

**`BTCpuProcessInit` (`Module.cpp:510-568`) already calls this wrapper directly, twice, for the two images that matter at process-start, with zero dependency on any notify export:**
```cpp
auto MainModule = reinterpret_cast<__TEB*>(NtCurrentTeb())->Peb->ImageBaseAddress;
HandleImageMap(reinterpret_cast<uint64_t>(MainModule), true);   // :548 -- THE MAIN EXE

auto NtDllX86 = reinterpret_cast<SYSTEM_DLL_INIT_BLOCK*>(GetProcAddress(NtDll, "LdrSystemDllInitBlock"))->ntdll_handle;
HandleImageMap(NtDllX86);                                        // :551 -- the 32-bit ntdll (C4-29's own fix target)
```
`PEB->ImageBaseAddress` is the REAL Windows/Wine convention for WOW64 main-image discovery (the native 64-bit PEB's `ImageBaseAddress` field is set to the *guest 32-bit exe's* base by the real wow64 loader specifically so callers like this one can find it without a notify round-trip) — this is donor-real design, not a floor workaround, and **this floor already wires `PEB.ImageBaseAddress = exe_base` correctly** (C4-29's own receipt: `CHALICE C4-29: PEB.ImageBaseAddress=0x47660000 (exe_base=0x47660000)`, confirmed pre-existing/correct, not the fault in that leg).

**Separately, `InvalidationTracker`'s own constructor (`InvalidationTracker.cpp:18-30`, runs inside `InvalidationTracker.emplace(*CTX, Threads)` at `Module.cpp:541`, BEFORE either `HandleImageMap` call above) does a THIRD, even more blanket registration**: a `VirtualQuery` sweep of the entire address space from 0 upward, calling `HandleMemoryProtectionNotification(Base, Size, Prot)` for every `MEM_COMMIT` region — which inserts the region into `XIntervals` whenever `Prot & (PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)`.

### MEASURE 2 — RECEIPT: does either self-registration path actually fire and cover `exe_entry`, live?

Re-ran `pe10 fex` (`scratch/pe10_probe.py fex`, zero source changes, transcript `/tmp/gaia-pe10-probe.log`) and grepped the EXISTING `CHALICE: NtQueryVirtualMemory addr=... branch=...` receipt line (this floor's own `NtQueryVirtualMemory`, `ntfile.c:1273-1389`, already unconditionally logs every call when `nt_dispatch_log_enabled`) for the constructor's own `VirtualQuery` sweep, which IS observable (unlike the two direct `RtlImageNtHeader` reads, which touch guest memory with no NT call):
```text
CHALICE: NtQueryVirtualMemory addr=0x0000000047020000 branch=image allocbase=0x0000000047020000 regionsize=0x0000000000640000 state=0x0000000000001000 type=0x0000000001000000   <- native ntdll
CHALICE: NtQueryVirtualMemory addr=0x0000000047660000 branch=image allocbase=0x0000000047660000 regionsize=0x0000000000005000 state=0x0000000000001000 type=0x0000000001000000   <- hello8_x86.exe, THE MAIN IMAGE
CHALICE: NtQueryVirtualMemory addr=0x0000000047665000 branch=fallback ...
```
(line 697 of the transcript, ~330 lines / well before `starting FEX (Hangover 11.9)`'s own OWN LATER activity at line ~560 -- correction, this exact sweep runs interleaved with `BTCpuProcessInit` itself, all still ~340 lines before the fault at line 908). `state=0x1000=MEM_COMMIT`; this floor's `NtQueryVirtualMemory` `img` branch (`ntfile.c:1299-1305`) unconditionally reports `AllocProtect=PAGE_EXECUTE_READWRITE` (`0x40`, confirmed matching `chalice/ntids.h:133`, the SAME constant FEX's own `Prot & (...)` mask tests) for the WHOLE registered image span, `regionsize=img->size-(addr-img->base)` -- at `addr==img->base` this is the FULL `0x5000` (20480B) image, which structurally contains `exe_entry` (`hello8_x86.exe+0x1000`, confirmed by `llvm-readobj -S`: `.text VirtualAddress=0x1000 VirtualSize=0xA`, well inside `[0,0x5000)`).

**This directly refutes the brief's premise.** `HandleMemoryProtectionNotification(0x47660000, 0x5000, 0x40)` — called by `InvalidationTracker`'s OWN constructor sweep, using data THIS FLOOR already serves correctly — takes the `Prot & PAGE_EXECUTE_READWRITE` branch (`InvalidationTracker.cpp:39-46`) and inserts `XIntervals.Insert({0x47660000, 0x47665000})`, unconditionally covering `exe_entry`. This happens **before** `BTCpuProcessInit`'s own explicit `HandleImageMap(MainModule, true)` call (a second, narrower, section-precise registration of the same range) and **long before** `BTCpuSimulate`/`CTX->ExecuteThread` ever begins decoding at `exe_entry` (confirmed by transcript line order: sweep ~line 693-793, fault ~line 908). Two independent, already-firing, donor-real mechanisms — **neither is the notify export the brief asked about, and neither needs it** — both should already mark `exe_entry` executable well ahead of the `NoExecOp` self-fault C4-31 identified.

**Cross-checked the decode-time consumer, too** (`FEXCore/Source/Interface/Core/Frontend.cpp:90-101`, `Decoder::CheckRangeExecutable` → `CTX->SyscallHandler->QueryGuestExecutableRange` → `InvalidationTracker::QueryExecutableRange`, `InvalidationTracker.cpp:177-190`): a pure `XIntervals.Query(Address).Enclosed` lookup, no other gate, no additional permission/registration-order check. If `XIntervals` genuinely contains `[0x47660000,0x47665000)` by fault time (per the above, it should), this call must return `Enclosed=true` for `0x47661000` — contradicting the observed `NOEXEC_INST`/`NoExecOp` outcome.

### Verdict vs the brief's own MEASURE→WIRE plan

- **Step 1 (donor-read the notify surface)** — done; `BTCpuNotifyMapViewOfSection` exists exactly as described, BUT is one of *three* registration paths, and not the one that matters for the main exe (real Windows/Wine convention: main-image discovery is PEB-based, self-service; notify exports are for *subsequently mapped* DLLs/sections the backend can't otherwise learn about — exactly why C4-29 needed one for the 32-bit ntdll, which has no equivalent PEB-pointer shortcut in `BTCpuProcessInit`'s own code).
- **Step 2 (receipt: confirm zero notifications ever fire for the exe)** — measured, but the finding is the OPPOSITE of what the brief expected to confirm: it is true that `BTCpuNotifyMapViewOfSection` itself never fires for the exe (this floor's Phase-0 C-direct exe map never goes through a real Wow64 `NtMapViewOfSection` syscall thunk, so there is no `BTCpu`-vtable call site to hit) — but this floor's own live `NtQueryVirtualMemory` receipts prove TWO OTHER, donor-real self-registration paths (`BTCpuProcessInit`'s own `HandleImageMap(PEB.ImageBaseAddress, true)` call, and `InvalidationTracker`'s constructor `VirtualQuery` sweep) already run and already cover `exe_entry`, well before the fault, using data this floor already serves correctly. The premise "nobody ever notified FEX about the exe" is **false** for the specific mechanism FEX's own code uses for main images.
- **Step 3 (wire donor-true notify call after Phase-0 exe map)** — **not attempted.** Given step 2's own finding, wiring `BTCpuNotifyMapViewOfSection(exe_base,...)` (or any equivalent call) would be **redundant** with a registration this floor's boot path already triggers via the correct, donor-real, PEB-based main-image path — it would not add or change any `XIntervals` state, and could not fix the observed fault. Implementing it anyway would be exactly the "speculative change" this project's own law forbids, now that measurement contradicts the hypothesis motivating it.
- **Step 4 (re-run pe10 fex, ladder)** — unchanged from C4-31 (`(a)` interval-registered: now MORE precisely shown to already be true per this task's own receipts, contradicting the `NoExecOp` outcome — see below; `(b)`/`(c)` not reached, terminal identity unchanged: `pe10 ret=3221225477`, RAISE#1 `code=0xC0000005`, `ctx.pc=libwow64fex+0x34E4=BTCpuSimulate+0x10`, all byte-identical to C4-29/30/31's own captures). `(d)` — `map-fex-offsets.sh` not re-run; no new DLL-relative fault address this pass (same identity as 3 prior legs).

### The real, sharpened gap — now genuinely narrower than C4-31 left it

C4-31 named the gap as "`InvalidationTracker` never registers `exe_entry` as executable, despite every checkable input looking correct" and treated it as fully unreached (donor-internal state). This pass goes one step further: **the registration-time INPUTS (`NtQueryVirtualMemory` answers) are not just "look correct" but are LIVE-CONFIRMED, via this floor's own instrumented receipt line, to be exactly what `InvalidationTracker`'s constructor needs to insert the covering interval** — the open question is no longer "was it ever told" (yes, twice) but **"why does the interval not survive/apply by decode time despite two independent, receipted insert calls"**. Candidates, NOT chased this pass (would need reading `FEXCore::IntervalList<uint64_t>`'s live memory layout — undocumented, no symbols, or enabling FEX's own internal diagnostics, see next paragraph — both new instrumentation, time-boxed out): (i) an interval-tree bug specific to two overlapping/nested inserts (constructor's blanket `[base,base+0x5000)` insert, then `HandleImageMap`'s narrower per-section `[base+0x1000,base+0x100A)` insert) that could plausibly corrupt/shrink the tree instead of correctly merging; (ii) something removes/invalidates the exe's interval between `BTCpuProcessInit` completing and `BTCpuSimulate` starting (no floor-side `NtProtectVirtualMemory`/`NtFreeVirtualMemory`/`BTCpuNotify*` call was observed touching the exe's address range anywhere in this pass's transcript — grepped, zero hits — so if this happens, it is FEX-internal, not floor-triggered); (iii) the specific single-byte-region `CheckRangeExecutable(Address,1)` call pattern (`Frontend.cpp:88-108`, `PeekByte`) somehow queries a DIFFERENT address than `exe_entry` itself despite the exception record naming `Rip=exe_entry` (unlikely but not excluded — `WowContext.Eip` is read from `Thread->CurrentFrame->State.rip` at FAULT time, which is provably `exe_entry`, C4-25/26/30's own receipts, but the ORIGINAL `CheckRangeExecutable` call that set `HitNonExecutableRange` could theoretically have queried a nearby address during multi-byte instruction peek — `PeekByte(Offset)` for `Offset>0` -- worth checking against the `.text` section's tiny `0xA`-byte `VirtualSize` if chased further, since a multi-byte read starting near the section's end could legitimately spill past a razor-thin registered range).

**Secondary, unrelated-but-relevant lead surfaced by this pass, not chased further:** FEX's own `LogMan::Msg::DFmt`/`IFmt` calls (level filter is a compile-time `MSG_LEVEL=INFO` constant, `LogManager.h:41`, NOT runtime-gated beyond that) would print exactly this kind of interval-registration detail (`InvalidationTracker.cpp:44`, `LogMan::Msg::DFmt("Add SMC interval: {:X} - {:X}", ...)` — fires whenever `Prot` includes `PAGE_EXECUTE_READWRITE`, i.e. on EVERY image/arena `HandleMemoryProtectionNotification` this floor's blanket-EXECUTE_READWRITE `NtQueryVirtualMemory` answers trigger) directly to this floor's own serial console via `__wine_dbg_output` — **if `FEX::Windows::Logging::Init`'s OWN `GetProcAddress(GetModuleHandleA("ntdll.dll"),"__wine_dbg_output")` lookup actually succeeds.** C4-15 already wired `FEX_SILENTLOG=0` into the guest environment block (`ntboot.c` `PARAMS_ENVIRONMENT_OFF`, confirmed present and unconditional in the current tree) specifically to unlock this. Grepped this pass's full transcript for any `LogMan`-style output (`^D `/`^I `/`^E ` level-prefixed lines, the exact format `MsgHandler` in `Logging.cpp:17-19` produces): **zero occurrences**, despite the env var being set and despite the `Prot`-gated SMC-interval DFmt call structurally firing (per this task's own measured `NtQueryVirtualMemory` receipts) many times during the constructor sweep. This means `WineDbgOut` is NOT actually getting installed (falls through to the `LogFile`/`fopen(LOCALAPPDATA...)` branch, `Logging.cpp:40-41`, which silently fails to nowhere since this floor doesn't implement a real filesystem write path for that location) — **a plausible, cheap, high-value next lead**: if fixed, FEX's own internal narration (including, likely, whatever internal check IS actually failing for `exe_entry`) would print directly to this floor's serial console, probably resolving the sharpened gap above without any further guessing. Named, not attempted (new instrumentation/floor investigation, out of this pass's own time-box).

### Re-gate

Zero source changes this pass (measurement/doc only) — `git status` clean after reverting churned build artifacts (`fake-ntdll.dll/.pdb`, `hello1-8.exe/.pdb`, `mini.dll`, `pehello_bin.c`, source-only-commits law). No gate re-run required (nothing to regress); prior `230edb3` gate state (`pe8_gate ret=78`, `pe9_gate` both backends, `ramfb_gate` 7/7) stands unchanged.

### Commit this pass

Doc-only: `HANDOFF.md`/`FEX-MAP.md` § **C4-32** (this section). No `kernel-arm/zoneA` source touched.

## C4-31 — the consume-check anatomy DONOR-TRACED end-to-end + its OWN input state PROVEN correct+present at the self-fault (`FaultToTopAndGeneratedException=1 Signal=SIGSEGV(11) TrapNo=PF(14) si_code=2 err_code=0x15`, byte-exact match to FEX's own `NoExecOp` `BreakDefinition`) -- REFUTES both suspects the brief posed (A: PC/field-match failure -- not found; B: missing thread-state -- state is present and correct) -- NEW verdict, evidence-driven, beyond the brief's own A/B frame: the consume-check DOES claim/translate this self-fault correctly (FEX's own design), and the resulting real x86 `STATUS_ACCESS_VIOLATION(EXECUTE_FAULT)` at `RIP=exe_entry` then genuinely, correctly goes UNHANDLED at guest SEH (hello8_x86.exe has zero SEH handlers) -- NOT a floor bug in the exception-delivery/consume machinery itself. The REAL newly-named gap is one layer further upstream: FEX's frontend decoder believes `exe_entry` (a correctly `IMAGE_SCN_MEM_EXECUTE`-flagged, header-intact, byte-correct .text section) is NOT EXECUTABLE -- i.e. FEX's `InvalidationTracker` executable-range tracking (`XIntervals`) was never populated for hello8_x86.exe's own image, despite every donor-traced input this pass could verify (PEB.ImageBaseAddress correct, PE headers correctly copied by `PeMapImage`, this floor's own `NtQueryVirtualMemory` correctly reporting `PAGE_EXECUTE_READWRITE` for the registered image, `nt_image_register` running well before FEX's own `BTCpuProcessInit`) looking correct. Named, not chased further this pass (new instrumentation -- reading FEX's own `InvalidationTracker::XIntervals`/`IntervalList` state is not reachable from this floor's own memory without donor edits, per project law; time-boxed). Runtime `<uncommitted at write time, see HANDOFF>`; live 2026-07-19.

### MEASURE 1 -- the consume-check, donor-traced end-to-end (file:line for every claim)

`BTCpuResetToConsistentStateImpl` (`FEX/Source/Windows/WOW64/Module.cpp:871-951`) is FEX's WOW64 "reset to consistent state" callback, invoked by wow64.dll's `Wow64PrepareForException` when native `dispatch_exception` (stock ntdll) is about to hand an exception to guest SEH -- **this IS the consume-check** this task's brief asked about (no separate `RtlAddVectoredExceptionHandler` exists for the WOW64 build -- confirmed: `FEX::DummyHandlers::DummySignalDelegator` (`Source/Tools/CommonTools/DummyHandlers.cpp`) registers NO host signal/exception handler at all, only a thread-local `TLSThread` pointer -- WOW64 FEX relies ENTIRELY on this one BTCpu-API hook as its "signal handler"; `RtlAddVectoredExceptionHandler` only appears in `Source/Windows/Defs/ntdll.def` as an export declaration, never called from `Source/Windows/WOW64/`).

Return contract (`BTCpuResetToConsistentState`, `:952-958`): `true` -> `NtContinue(Context,FALSE)` (silently absorbed, guest never sees an exception -- self-modifying-code/overcommit/call-ret-stack/JIT-guard fast paths only). `false` -> exception delivery CONTINUES to guest SEH, but `Context` may have been rewritten first.

Gate (`:921-923`):
```cpp
if (!Thread || !IsAddressInJit(Context->Pc)) {
  return false;
}
```
`IsAddressInJit` (`Module.cpp:166-172`):
```cpp
bool IsDispatcherAddress(uint64_t Address) {
  const auto& Config = SignalDelegator->GetConfig();
  return Address >= Config.DispatcherBegin && Address < Config.DispatcherEnd;
}
bool IsAddressInJit(uint64_t Address) {
  if (IsDispatcherAddress(Address)) return true;
  auto Thread = GetTLS().ThreadState();
  return Thread->CTX->IsAddressInCodeBuffer(Thread, Address);
}
```
`Config.DispatcherBegin/End` are `Dispatcher::Start`/`Dispatcher::End` (`FEXCore/Source/Interface/Core/Dispatcher/Dispatcher.cpp:2221-2222`), set at `:597-598` (`Start=DispatchPtr` captured at `:61`, essentially the buffer's first emitted instruction; `End=GetCursorAddress()` captured AFTER `EmitDispatcher()` finishes ALL emission, `:598`) -- `GuestSignal_SIGSEGV`'s own emission (`:417`) happens strictly BETWEEN `:61` and `:597/598`, inside the SAME function -- by construction `[Start,End)` spans (at minimum) everything `EmitDispatcher()` emits, which structurally MUST include the self-fault's own address (C4-30's `elr=base+0x2B0`, deep inside the SAME 0x4000 buffer). Not independently live-measured this pass (`SignalDelegator->GetConfig()` lives in FEX's own process memory, no floor-side reader built) -- inferred from the donor's own emission ORDER, not guessed; flagged as the one remaining un-measured link in this chain.

Past the gate (Thread valid + Pc in Dispatcher range, both true per below): `EXCEPTION_ACCESS_VIOLATION`-specific pre-checks (CallRetStack / OvercommitTracker / HandleSuspendInterrupt / JITGuardPage / RWX-self-modifying-code, `:878-916`) don't match this fault's shape (`FaultAddress=far=0`, not a call-ret/overcommit/SMC pattern) -- falls through to `:924-950` ("Reconstructing context"): builds `WowContext` (the guest i386 CONTEXT, incl. `WowContext.Eip` = `Thread->CurrentFrame->State.rip`), then:
```cpp
auto& Fault = Thread->CurrentFrame->SynchronousFaultData;
*Exception = FEX::Windows::HandleGuestException(Fault, *Exception, WowContext.Eip, WowContext.Eax);
```
`HandleGuestException` (`Source/Windows/Common/Exception.h:12-79`) is **the actual field-translation step**: if `Fault.FaultToTopAndGeneratedException` is false, it's a no-op passthrough (`Dst=Src`, unmodified raw host AV). If true, it clears the flag and switches on `Fault.Signal`/`Fault.TrapNo` to build a REAL x86-level `EXCEPTION_RECORD` (`EXCEPTION_ILLEGAL_INSTRUCTION`/`BREAKPOINT`/`SINGLE_STEP`/`PRIV_INSTRUCTION`/`INT_OVERFLOW`, or for `X86_TRAPNO_PF` specifically: leaves `ExceptionCode` untouched (stays whatever the host delivered) but sets `NumberParameters=2, ExceptionInformation[0]=EXCEPTION_EXECUTE_FAULT, ExceptionInformation[1]=Rip` -- "a page-fault raised by an explicit break in JIT code is always an execute fault", `Exception.h:63-68`). Then `BTCpuSetContext(...,&WowContext)` publishes the (possibly-rewritten) guest CONTEXT, `Context::UnlockJITContext(TLS)`, and `memcpy(Context, TLS.EntryContext(), sizeof(*Context))` (`:947`) replaces the HOST ARM64 context with the stable pre-JIT-entry anchor captured at `BTCpuSimulate`'s own `RtlCaptureContext` (`Module.cpp:757-761`) -- **this is the origin of the `BTCpuSimulate+0x10` identity every prior leg (C4-18/21/24/26/27/29) observed at RAISE#1** -- it is a NORMAL, DESIGNED side effect of THIS SAME reconstruction path succeeding, not evidence the fault went unclaimed. `return false` -- exception delivery continues to guest SEH with the now-translated `Exception` record and the anchor host context.

### MEASURE 2b -- the consume-check's OWN input state, LIVE-READ from OUR floor (new receipt, `kmain.c` `sync_try_guest_exception`, CHALICE C4-31)

Offsets (no donor edits; `clang++`-`offsetof` cross-check blocked -- `External/fmt` submodule not checked out in this quarry -- computed by hand from the field list, cross-checked against the donor's own `static_assert(offsetof(CpuStateFrame,SynchronousFaultData)%8==0)`):
- `InternalThreadState::CurrentFrame` is the FIRST member, offset `0` (`FEXCore/include/FEXCore/Debug/InternalThreadState.h:91`) -> `CurrentFrame = *(u64*)ThreadState`.
- `CpuStateFrame::SynchronousFaultData` offset `0x5D8`: `sizeof(CPUState)=0x5C0` (`alignas(64)`, raw field sum 1464 bytes -> rounds to 1472=`0x5C0`, `CoreState.h:74-251`) + `ReturningStackLocation`(8)@`0x5C0` + `InSyscallInfo`(8)@`0x5C8` + `SignalHandlerRefCounter`(u32)@`0x5D0` -> next=`0x5D4`, rounded up to 8 (`alignas(8) SynchronousFaultDataStruct`) = `0x5D8` (`CoreState.h:393-421`).
- Packing (8 bytes, LE): `byte0=FaultToTopAndGeneratedException(bool) byte1=Signal byte2=TrapNo byte3=si_code bytes4-5=err_code(u16) bytes6-7=_pad` -- matches `DEF_OP(Break)`'s own `memcpy(&Constant,&State,8)` packing exactly (`FEXCore/Source/Interface/Core/JIT/MiscOps.cpp:59-77`).

Live capture (`pe10 fex`, `scratch/pe10_probe.py`), at the SAME C4-30 self-fault event:
```text
CHALICE TLS PROBE: ... slot16(ThreadState)=0x0000000270001000 ... CurrentFrame=0x0000000270001140
CHALICE C4-31 SynchronousFaultData raw=0x00000015020E0B01 FaultToTopAndGeneratedException=0x1 Signal=0xB TrapNo=0xE si_code=0x2 err_code=0x15
```
`Signal=0xB=11=FAULT_SIGSEGV` (`FEXCore/include/FEXCore/Core/SignalDelegator.h:14,18`, `= SIGSEGV = 11`). `TrapNo=0xE=14=X86_TRAPNO_PF` (`FEXCore/include/FEXCore/Core/X86Enums.h:119`). `si_code=2`, `err_code=0x15=0b10101=X86_PF_PROT(1)|X86_PF_USER(4)|X86_PF_INSTR(0x10)` (`X86Enums.h:134-138`). **This is a byte-for-byte, field-for-field EXACT match to `OpDispatchBuilder::NoExecOp`'s own `BreakDefinition`** (`FEXCore/Source/Interface/Core/OpcodeDispatcher.cpp:5018-5024`):
```cpp
void OpDispatchBuilder::NoExecOp(OpcodeArgs) {
  BreakOp(Op, FEXCore::IR::BreakDefinition {
    .ErrorRegister = X86State::X86_PF_PROT | X86State::X86_PF_USER | X86State::X86_PF_INSTR,
    .Signal = Core::FAULT_SIGSEGV,
    .TrapNumber = X86State::X86_TRAPNO_PF,
    .si_code = 2, // SEGV_ACCERR
  });
}
```
(the OTHER three `FAULT_SIGSEGV` sites -- `INT n` non-syscall #GP, `INTO`, `HLT` -- all use `TrapNo=GP` or `OF`, `OpcodeDispatcher.cpp:4679-4703`; only `NoExecOp` uses `TrapNo=PF`, so this match is unambiguous, not a coincidence of overlapping trap numbers). `Core.cpp:695-699`: `NoExecOp` fires specifically when a decoded block's FIRST instruction fails to decode with `BlockStatus==NOEXEC_INST` (not `INVALID_INST`/`BAD_RELOCATION`) -- i.e. `Block.Entry == GuestRIP == WowContext.Eip`, matching this leg's own already-captured `WowContext.Eip=0x47661000` (`hello8_x86.exe+0x1000`, = `exe_entry`, C4-25/C4-30's own `CHALICE C4-25 GUEST CTX: Eip=0x47661000` receipt) EXACTLY.

### VERDICT vs the brief's own A/B

**Neither A nor B, as framed.** (B, missing state) is DIRECTLY REFUTED for this fault instance: `SynchronousFaultData` is present, correctly written by `IR::Break` (`MiscOps.cpp:59-77`, unconditional `FaultToTopAndGeneratedException=1` write before every `Break`), and correctly read back by our own floor-side probe with values that exactly, unambiguously match one specific donor `BreakDefinition` (`NoExecOp`) -- the check does NOT "honestly answer not mine" for lack of state; the state says exactly what fired and why. (A, PC/field-match) also NOT found: `Context->Pc` (`elr`) sits inside `[Start,End)` by construction (donor emission order, `Dispatcher.cpp:61/417/597-598`, not independently live-measured this pass -- the one flagged gap above) and `Thread` is proven non-null (the SAME `TlsSlots[16]`/`CurrentFrame` chain that yielded the correct `SynchronousFaultData` read). Both named suspects are, on the evidence gathered, absent.

**New verdict, evidence-driven**: the consume-check WORKS AS DESIGNED for this self-fault -- `HandleGuestException`'s `X86_TRAPNO_PF` branch correctly leaves `ExceptionCode` as delivered (`STATUS_ACCESS_VIOLATION`, matching this pass's own observed `pe10 ret=3221225477`=`0xC0000005` exactly, and C4-29's own identical observation) and stamps `ExceptionInformation=[EXECUTE_FAULT, Rip=exe_entry]` -- a CORRECTLY SYNTHESIZED real x86 exception, handed to guest SEH at the guest's own `exe_entry` RIP. `hello8_x86.exe` (a bare 10-byte freestanding function, `hello8_x86.c`'s own header comment: "no imports, no CRT") has ZERO SEH handlers registered -- so this exception is, correctly, by Windows' own design, UNHANDLED -- terminating the process. **This is not a floor bug in the exception-delivery/consume machinery** -- it is FEX correctly reporting (via a real, designed x86-level exception) that it believes `exe_entry` is not executable.

### The REAL newly-named gap (not chased this pass -- named, evidence-narrowed, time-boxed)

WHY does FEX's frontend (`Decoder::CheckRangeExecutable`, `FEXCore/Source/Interface/Core/Frontend.cpp:89-108`, consulting `CTX->SyscallHandler->QueryGuestExecutableRange` -> WOW64's `InvalidationTracker->QueryExecutableRange` -> `Source/Windows/Common/InvalidationTracker.cpp:177-190`, querying an `IntervalList` named `XIntervals`) believe `exe_entry` (`hello8_x86.exe+0x1000`) is not executable, when every input this pass could verify looks correct:
- `hello8_x86.exe`'s `.text` section genuinely has `IMAGE_SCN_MEM_EXECUTE` set (`llvm-readobj -S hello8_x86.exe`: `Characteristics [0x60000020] IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ`, `VirtualAddress=0x1000 VirtualSize=0xA`, `AddressOfEntryPoint=0x1000` -- exactly matches `exe_entry`).
- `PeMapImage` (`kernel-arm/zoneA/pemod.c:119-171`) copies `SizeOfHeaders` bytes (DOS+PE+COFF+optional headers+section table, `SizeOfHeaders=1024` here) VERBATIM from the file before copying sections -- the PE header/section-table region FEX's `RtlImageNtHeader`+`IMAGE_FIRST_SECTION` walk (`InvalidationTracker::HandleImageMap`, `InvalidationTracker.cpp:63-92`) would read is present and byte-correct in guest memory, not synthesized/truncated.
- `PEB.ImageBaseAddress=exe_base` is correctly wired (C4-29, live-receipted) -- `BTCpuProcessInit`'s `MainModule` (`Module.cpp:547`) reads the SAME value `HandleImageMap`'s PE-section walk would need.
- This floor's own `NtQueryVirtualMemory` (`ntfile.c:1273-1339`) reports `AllocationProtect=Protect=PAGE_EXECUTE_READWRITE` (EXECUTE bit set) for ANY address inside a registered image (`if (img) {...}` branch, unconditional, not section-specific) -- `InvalidationTracker`'s OWN constructor (`InvalidationTracker.cpp:19-30`) independently walks the WHOLE address space via `VirtualQuery`/`NtQueryVirtualMemory` and would ALSO register any committed executable region this way, a SECOND, independent path to the same XIntervals table.
- `nt_image_register(exe_name,...)` (`ntboot.c:325`) runs during Phase-0, provably before FEX/wow64.dll is even mapped (same `nt_boot_process` call, Phase-0 comment `:303`) -- not an ordering/timing gap on this floor's own image-registration side.

Given all of these check out, the remaining candidates are ALL inside FEX's own `InvalidationTracker`/`IntervalList` internals (does the 10-byte `.text` VirtualSize hit some edge case in the interval-tree query; does `RtlImageNtHeader` on a PE32 -- not PE32+ -- "native" `MainModule` address behave unexpectedly; does the constructor's `VirtualQuery` walk terminate or skip past `exe_base` before reaching it) -- **none of these are reachable from this floor's own memory without a donor edit** (per this project's law), and instrumenting them would require either a donor-source probe or a NEW floor-side receipt at a point this pass didn't reach. Named here, not chased further -- next worker's starting point, with the entire consume-check chain above already fully donor-traced so no re-derivation is needed.

---

## C4-30 — the ORIGINAL fault (pre-reset) captured and named: FEX's own GuestSignal_SIGSEGV self-fault ("force a SIGSEGV by loading zero", `Dispatcher.cpp`), inside its fixed dispatcher buffer (0x4000 bytes == `MAX_DISPATCHER_CODE_SIZE`), reached before `BTCpuSimulate` ever dispatches to PeMain's block. Runtime `ec45bd6`; live 2026-07-19.

### Receipt, live (`pe10 fex`, this pass's own new instrumentation)

```text
CHALICE ARENA: esr=0x96000006 ec=0x25 iss=0x6 elr=0x26D0C02B0 far=0x0
CHALICE REGION elr=0x26D0C02B0 -> reserve-commit-arena committed base=0x26D0C0000 size=0x4000
CHALICE REGION far=0x0 -> outside-known-allocators
CHALICE ARENA: refused not-arena
CHALICE C4-30 ORIGINAL FAULT: esr=0x96000006 ec=0x25 status=0xC0000005 elr=0x26D0C02B0 far=0x0 wnr=0
CHALICE REGION elr-orig -> reserve-commit-arena committed base=0x26D0C0000 size=0x4000
CHALICE REGION far-orig -> outside-known-allocators
CHALICE C4-30 ELR WINDOW @elr-32..+32:
  0xA9031B85 0xA9042788 0xA9052F8A 0x29026F9A 0x91070380 0x4C9F2C10 0x4C9F2C14 0x52800001
  0xF9400021 0xD53B4200 0xB9040B80 0xF9005B99 0xA9021F84 0xA9031B85 0xA9042788 0xA9052F8A
```

`esr=0x96000006` decodes: EC=`0x25` (Data Abort, lower EL), ISS=`0x6` -> WnR=`(esr>>6)&1=0` (a READ), DFSC=`0x6` (Translation fault, level 2 — genuinely unmapped, not a permission fault). `elr=0x26D0C02B0` sits `0x2B0` into a `reserve-commit-arena` region base `0x26D0C0000` size `0x4000`, classified `committed` (real, executable, mapped code — not the fault). `far=0x0` classifies `outside-known-allocators` — an unambiguous NULL pointer, not a mis-triaged reserve-only arena address (see triage check below).

### The arena IS FEX's own Dispatcher code buffer — exact size proof

```cpp
// FEXCore/Source/Interface/Core/Dispatcher/Dispatcher.cpp:36
constexpr size_t MAX_DISPATCHER_CODE_SIZE = FEXCore::Utils::FEX_PAGE_SIZE * 4;   // 0x1000*4 = 0x4000

// :39
Dispatcher::Dispatcher(FEXCore::Context::ContextImpl* ctx)
  : Arm64Emitter(ctx, FEXCore::Allocator::VirtualAlloc(MAX_DISPATCHER_CODE_SIZE, true), MAX_DISPATCHER_CODE_SIZE)
```

Our arena region's size (`0x4000`) is an exact, non-coincidental match — the SAME class of dispatcher-trampoline arena C4-17/18/21/27/28 already independently named in earlier legs (base varies boot-to-boot per this floor's bump allocator, size is always `0x4000`).

### The exact instruction, disassembled + donor-matched

Word window index (`elr-32` .. `elr+28`, step 4): index 8 (`w=0`) is the faulting word. Manual ARM64 field decode (bit slicing, cross-checked against the ARM ARM):

- `elr-4 = 0x52800001` → `MOVZ W1, #0` (32-bit write zero-extends into X1)
- `elr+0 = 0xF9400021` → `LDR X1, [X1, #0]` (`size=11(64-bit) opc=01(LDR) imm12=0 Rn=x1 Rt=x1`) — **the fault**: loads through the register JUST zeroed.

Donor match, `FEXCore/Source/Interface/Core/Dispatcher/Dispatcher.cpp`, the `GuestSignal_SIGSEGV` handler (comment verbatim in the donor):

```cpp
{
  // Guest Overflow handler
  // Needs to be distinct from the SignalHandlerReturnAddress
  GuestSignal_SIGSEGV = GetCursorAddress<uint64_t>();
  SpillStaticRegs(TMP1);
  // hlt/udf = SIGILL
  // brk = SIGTRAP
  // ??? = SIGSEGV
  // Force a SIGSEGV by loading zero
  if (CTX->ExitOnHLTEnabled()) {
    ldr(ARMEmitter::XReg::x0, STATE_PTR(CpuStateFrame, ReturningStackLocation));
    add(ARMEmitter::Size::i64Bit, ARMEmitter::Reg::rsp, ARMEmitter::Reg::r0, 0);
    PopCalleeSavedRegisters();
    ret();
  } else {
    LoadConstant(ARMEmitter::Size::i64Bit, ARMEmitter::Reg::r1, 0);
    ldr(ARMEmitter::XReg::x1, ARMEmitter::Reg::r1);
  }
}
```

`LoadConstant(x1, 0)` → `MOVZ W1,#0`; `ldr(XReg::x1, Reg::r1)` → `LDR X1, [X1]` — byte-for-byte, instruction-for-instruction match. We hit the `else` branch, confirming `CTX->ExitOnHLTEnabled()` is FALSE (default config) live on this floor. `elr-16..elr-8` (`0x91070380 0x4C9F2C10 0x4C9F2C14`) are consistent in class with the immediately-preceding `SpillStaticRegs(TMP1)` call (ADD-immediate + SIMD LD1/ST1-style context-spill instructions) — not individually decoded to instruction-level confidence this pass (not needed for the identification; the two-instruction fault signature above is exact and sufficient).

### Why FEX reaches this trampoline — named, not chased (time-boxed)

`GuestSignal_SIGSEGV` is branched to ONLY from `IR::Break(FAULT_SIGSEGV)` (`FEXCore/Source/Interface/Core/JIT/MiscOps.cpp:64-93`, `DEF_OP(Break)`, `case Core::FAULT_SIGSEGV: ldr(TMP1,...Pointers.GuestSignal_SIGSEGV); br(TMP1);`). `IR::Break(FAULT_SIGSEGV)` is itself emitted by the x86 frontend (`OpcodeDispatcher.cpp`) for a closed set of reasons: `INT n` when the literal isn't the syscall gate (`0xCD`, `#GP`, line ~4679), `INTO` (`0xCE`, line ~4689), `HLT` (`0xF4`, line ~4699 — deliberately `#GP`/SIGSEGV not SIGILL), `PermissionRestrictedOp` (`#GP`, line ~5000), `NoExecOp` (non-executable-page instruction FETCH, `#PF`/`SEGV_ACCERR`, line ~5018).

PeMain's own decoded bytes at the boot-seeded `Eip` (re-confirmed intact this pass, GUEST BYTES receipt: `55 89 E5 B8 2A 00 00 00 5D C3` = `push ebp; mov ebp,esp; mov eax,0x2A; pop ebp; ret`) contain none of `INT`/`HLT`/`UD2` — so if this specific `Break` fired while translating THAT exact block, it did not come from decoding those bytes. More likely this fires from a different FEX-internal path (an init-time probe/warm-up compile, or a permission/exec-bit check against an address that isn't `exe_entry`) reached before `BTCpuSimulate` ever executes PeMain's own bytes — see the CPURESERVED receipt below (Eax=0, Esp==Ebp==boot-seed: PeMain never ran).

**Next lead, not chased this pass** (new instrumentation, time-boxed out, no donor edits per this project's law): `CpuStateFrame::SynchronousFaultData` (`FEXCore/include/FEXCore/Core/CoreState.h:414-421`, an 8-byte `{FaultToTopAndGeneratedException, Signal, TrapNo, si_code, err_code}` struct) is written by `IR::Break` (`MiscOps.cpp:64-75`) immediately before branching to the signal trampoline — reading it live from OUR floor (via `TlsSlots[16]`/`ThreadState`, already probed C4-22/23/29) would name the exact `TrapNo`/`si_code` and settle which frontend site fired, entirely from our own side, without touching donor code.

### I386_CONTEXT + stack/stub receipts (MEASURE step 2)

```text
CHALICE C4-25 GUEST CTX: Eip=0x47661000 (hello8_x86.exe+0x1000) Esp=0x488CC000 Eax=0x0
  Ecx=0x0 Edx=0x0 Ebx=0x0 Ebp=0x488CC000 Esi=0x0 Edi=0x0 Cs=0x1B Ss=0x23 EFlags=0x202
CHALICE C4-25 GUEST BYTES @Eip-16..+32: 0 0 0 0 0xB8E58955 0x2A 0xC35D 0 0 0 0 0
CHALICE C4-30 GUEST STACK @Esp-8..+16: 0 0 0x488D0000 0 0 0
CHALICE C4-30 STUB PAGE addr=0x488D0000 written=1 bytes: 0xB8FF6A50 0x2C 0x76FFF3E8 0xF
```

`Eax=0x0`, not `0x2A` — PeMain's own `mov eax,0x2A` never executed. `Esp==Ebp==0x488CC000` — exactly the boot-seeded stack-top (`nt_wow64_cpureserved_set_context`'s own write), unchanged — **PeMain never started executing**. Guest byte hexdump confirms the entry function is intact and benign (same as C4-26 already found): `55 89 E5 B8 2A 00 00 00 5D C3`.

Stack word at `Esp+0` = `0x488D0000` = exactly `nt_wow64_stub_addr` — **the C4-27 stub return-address is still correctly seeded**, undisturbed. Stub-page hexdump (13/`0xD` bytes, `written=1`): little-endian byte sequence `50 6A FF B8 2C 00 00 00 E8 F3 FF 76 0F` = `push eax; push -1; mov eax,0x2C; call rel32`, byte-for-byte the intended design. `rel32=0x0F76FFF3`; `stub_addr(0x488D0000) + 13(0xD) + rel32 = 0x58040000` (hand hex-verified) — correctly resolves forward to this boot's own live-discovered BOP address. **The C4-27 stub is intact, correctly seeded, and simply unreached** — same verdict C4-27 already reached, now doubly confirmed via a completely different fault identity than that leg saw.

### Demand-commit triage check (MEASURE step 3)

`sync_try_arena_demand_commit` (`kmain.c`) fires first (boot.S ordering), sees `far=0x0`, calls `nt_arena_contains(0x0)` → false (0 was never reserved by any `NtAllocateVirtualMemoryEx`/arena call) → `CHALICE ARENA: refused not-arena`. Correct behavior — **not a triage regression**; this task's branch (a) ("far ∈ demand arena → fix triage") does not apply.

### Branch verdict vs the task's own (a)/(b)/(c)

- **(a) does not apply** — far ∉ demand arena, correctly refused.
- **(b) does not apply** — the C4-27 stub/Esp seeding is proven intact and unreached; not the cause. This fault happens before PeMain's own code ever runs.
- **(c) sharpened, not a "missing service"** — the AV is inside FEX's OWN internal dispatcher-generated code, a deliberate self-fault mechanism (not a floor gap on FEX's side) that this floor's general "forward every unclassified fault to guest NT SEH" design (C4-18) cannot distinguish from a genuine guest AV. Same already-repeatedly-named architectural gap (C4-18/21/24/26/27/29) — but for the first time precisely identified as the `GuestSignal_SIGSEGV` marker specifically (not the previously-seen `SignalHandlerReturnAddress`/`hlt#0` marker C4-17/18/21/27 named, nor the now-fixed `TlsSlots[16]=2` sentinel C4-28/29 fixed).

### The full chain, now proven end-to-end for the first time

Our floor delivers this self-fault as a genuine `STATUS_ACCESS_VIOLATION` → stock ntdll's `KiUserExceptionDispatcher` → `dispatch_exception` → wow64's `Wow64PrepareForException` → FEX's `BTCpuResetToConsistentStateImpl` (which `memcpy`'s a NEW reconstructed context anchored at `BTCpuSimulate+0x10` over the real one, `Module.cpp:947`, by FEX's own design, to give guest SEH handlers a stable native-unwind-shaped context) → guest ntdll calls `NtRaiseException` AGAIN with that reconstructed identity → our floor's `RAISE#1` (`ntsvc.c`) only ever saw the anchor (`ctx.pc=libwow64fex+0x34E4=BTCpuSimulate+0x10`), never the original fault. Prior legs (C4-18/21/24/26/27/29) only ever observed the tail of this chain; this pass captured the head, closing the loop.

### Success ladder verdict

(a) **AV explained** — ACHIEVED, first time, full chain proven not inferred. (a) "+ fixed" — NOT attempted: the fix is the identical already-named architectural decision (FEX-internal-fault short-circuit vs full guest-SEH injection) five prior legs explicitly ruled needs Pascal's own design ruling — attempting it here would be the "speculative changes" this project's law forbids. (b) PeMain completes — NOT reached, now explained why (fault fires upstream of BTCpuSimulate ever dispatching to PeMain's block). (c) new frontier mapped + recorded honestly — yes, more precisely than any prior leg.

### Re-gate

`pe8_gate.py` ret=78 PASS; `pe9_gate.py` PASS both backends (wowbox64/fex, both ret=0); `ramfb_gate.py` PASS (7/7). Instrumentation-only change — `pe10 ret=3221225477` (`=0xC0000005`) identical before/after.


## C4-29 — the null image base RESOLVED: FEX's own BTCpuProcessInit read LdrSystemDllInitBlock->ntdll_handle unconditionally; fixed by mapping syswow64/ntdll.dll Phase-0 and populating it C-direct. C4-28's whole fresh fault chain (RtlImageNtHeader(NULL) -> TlsSlots[16]=2 loop) is GONE, no regression. New frontier: back at the SAME pre-existing BTCpuSimulate+0x10 architectural gap C4-18/21/24/26/27 already named -- but reached this time via a genuinely healthy path (real TlsSlots[16] heap pointer, real JIT execution in the reserve-commit arena, clean single-pass NT SEH round-trip). Runtime `c594741`; live 2026-07-19.

- **MEASURE step 1 answer, live-caught** (kmain.c `resolve_line("lr", regs_safe[30])` instrumentation added this task, extending the existing guest-exception-delivery receipt): at the `RtlImageNtHeader(NULL)` fault (`elr=ntdll.dll+0x43DB8 far=0`), the caller LR resolved to `libwow64fex.dll+0x169B40`, `x0=0` (the NULL module-base argument, captured directly). Static-mapped (`map-fex-offsets.sh`) against the FEX donor: `+0x169B40` is the return address right after `bl RtlImageNtHeader` inside `FEX::Windows::InvalidationTracker::HandleImageMap(std::string_view Name, uint64_t Address)` (`Source/Windows/Common/InvalidationTracker.cpp:63`) -- `x3` (the `Address` arg) is moved into `x0` before the call, so `HandleImageMap` itself was invoked with `Address=0`.
- **who calls HandleImageMap(0), donor-grounded** (`FEX/Source/Windows/WOW64/Module.cpp:547-551`, `BTCpuProcessInit`):
  ```cpp
  auto MainModule = reinterpret_cast<__TEB*>(NtCurrentTeb())->Peb->ImageBaseAddress;
  HandleImageMap(reinterpret_cast<uint64_t>(MainModule), true);      // (A) main image
  auto NtDllX86 = reinterpret_cast<SYSTEM_DLL_INIT_BLOCK*>(GetProcAddress(NtDll, "LdrSystemDllInitBlock"))->ntdll_handle;
  HandleImageMap(NtDllX86);                                          // (B) 32-bit ntdll
  ```
  Call (A) reads the **native (64-bit) PEB.ImageBaseAddress** -- already correctly wired (`nt_build_teb`, `image_base=exe_base`, non-null; confirmed by this task's new boot receipt, `CHALICE C4-29: PEB.ImageBaseAddress=0x47660000 (exe_base=0x47660000)`) -- NOT the faulting call. Call (B) reads **`LdrSystemDllInitBlock->ntdll_handle`** (`SYSTEM_DLL_INIT_BLOCK`, wine `include/winternl.h:4422-4438`, field offset `0x48`) -- **this** was the NULL value: this floor's `LdrSystemDllInitBlock` export lived in the native ntdll's data section (registered fine, C4-26 already confirmed the export exists and is lookupable) but its `ntdll_handle` field itself had never been written by anything on this boot path, so `NtDllX86=0` -> `HandleImageMap(0)` -> `RtlImageNtHeader(NULL)`.
- **donor-grounded root cause AND the exact reader/writer contract**, cross-checked against two independent Wine call sites:
  - **writer** (real Windows/Wine, never exercised on this floor): `wine/dlls/ntdll/unix/loader.c:1595-1633` `load_ntdll_wow64_functions(HMODULE module)` -- `pLdrSystemDllInitBlock->ntdll_handle = (ULONG_PTR)module;` where `module` is the **32-bit ntdll.dll**, mapped a SECOND time (after the native one) purely by Wine's own **unix-side** process loader -- a pure static PE-export-table walk (`find_named_export`), **zero guest execution**, entirely analogous to this floor's own Phase-0 (`nt_boot_process` maps native ntdll+exe directly via `PeMapImage`, bypassing any NT syscall).
  - **the PE-side (`dlls/ntdll/loader.c:4404-4416`) `init_wow64()`/`SET_INIT_BLOCK` macro is a red herring** -- it sets 6 function-pointer fields (`KiUserApcDispatcher` etc.) but its own source explicitly has `/* LdrSystemDllInitBlock.ntdll_handle */` **commented out** (never set there); `ntdll_handle` is ONLY ever written by the unix-side `load_ntdll_wow64_functions` above. Confirms this is a unix/floor-side responsibility, not a guest-code (PE-side `init_wow64`) one -- exactly matching this floor's existing native ntdll+exe mapping doctrine.
  - **this floor never had a "map syswow64 ntdll.dll a second time at Phase-0" step at all** -- C4-26 already built the *populate* logic (`ntfile.c` `nt_wow64_populate_init_block_if_ntdll32`, walks the 32-bit ntdll's own export table and writes all 6+`ntdll_handle`+self-pointer fields) but wired it ONLY behind an `NtMapViewOfSection` hook -- and C4-26's own receipt (and this task's fresh confirmation, see below) established that hook never fires on `pe10`'s boot path (no real 32-bit ntdll loader chain ever runs a guest-visible `NtMapViewOfSection` call; the C-seeded WOW64 entry skips straight to `BTCpuSimulate`).
- **fix (`c594741`)** -- `ntboot.c`, inside the existing `if (wow64_mode)` block: map `syswow64/ntdll.dll` directly (`boot_blob_lookup` + `PeMapImage`, same convention as the native ntdll/exe maps a few lines above), `nt_image_register` it, then call `nt_wow64_populate_init_block_if_ntdll32("ntdll.dll", ntdll32_base, ntdll32_size)` **directly, C-side** -- the Wine-equivalent unix-side step, done at the exact Phase-0 point this floor already does everything else host-side. `nt_wow64_populate_init_block_if_ntdll32` (`ntfile.c`) was made non-`static` and prototyped in `ntfile.h`; its existing `NtMapViewOfSection` hook call site is left in place (harmless, Machine/name-gated no-op given the new call already ran first).
- **secondary, NOT this task's target, honestly flagged**: the populate call's own diagnostic line shows `thunk=0 rtlstart=0 kuxd=0 kuad=0 kucd=0 owninit=0` -- every export EXCEPT `ntdll_handle` itself (`base32`, written unconditionally, not via `pe_find_named_export`) came back not-found on the 32-bit ntdll image, despite `pe_find_named_export` working correctly against the SAME 32-bit ntdll blob via a DIFFERENT code path (`pe9`'s own `pe32sec` self-test, `NtQuerySection`, reports the identical mapped size `0x22D000` successfully). Root cause UNVERIFIED this pass (out of scope: the ONE field this task's own fault chain actually reads, `ntdll_handle`, is correctly populated and the fault is gone) -- next worker's lead if `LdrInitializeThunk`/`KiUser*Dispatcher` pointers are ever needed from this struct (C4-27's stub/BOP design does not need them, so nothing currently downstream depends on this gap).
- **PEB32.ImageBaseAddress also fixed this task** (`ntboot.c`, `wr32(peb32+0x08, exe_base)`) -- `nt_build_wow64_teb32`'s own C4-3-era comment had self-flagged this exact gap ("left 0 this atom -- no 32-bit ntdll/exe image mapped yet"); donor-cited `wine include/winternl.h:847`. Confirmed via receipt (`CHALICE C4-29: PEB32.ImageBaseAddress=0x47660000`) NOT to be the reader in this particular fault chain (FEX's `BTCpuProcessInit` reads the 64-bit PEB and `LdrSystemDllInitBlock`, never `TEB32->Peb`, at this stage) -- but it was a genuine, self-documented pre-existing gap on the SAME struct family, fixed alongside since it was directly implicated by the MEASURE brief and cost nothing extra to close correctly (not speculative -- donor-cited offset, matches the already-wired 64-bit sibling field exactly).
- **TlsSlots[16] resolution (MEASURE step 3, verified)**: with the fix, TLS PROBE now reads `slot16(ThreadState)=0x270001000` -- a real heap pointer, NOT the `2` sentinel C4-28 observed. Confirms the earlier ladder-rung-C hypothesis ("BTCpuThreadInit hadn't run yet when something read this slot") was the correct explanation all along, downstream of the SAME root cause: `BTCpuProcessInit` never got past its own `HandleImageMap(NULL)` crash-loop far enough to ever reach `BTCpuThreadInit`. No separate TlsSlots-writer bug existed -- fixing the null-image-base crash let `BTCpuThreadInit` run and correctly populate this slot, exactly as designed (`Module.cpp:626`).
- **fresh pe10-fex outcome, full ladder vs the brief's own (a)-(d)**:
  - **(a) ACHIEVED, receipted**: `BTCpuThreadInit` ran; `TlsSlots[16]=0x270001000` (real pointer, not `2`).
  - **(b) LIKELY ACHIEVED, indirect evidence, not a literal marker**: after thread-init, execution reaches a genuine JIT `LookupCache`/reserve-commit-arena fault (`elr=0x26D0C02B0`, demand-committed, retried -- the SAME class of fault C4-17 first identified as real guest-JIT code-cache activity), followed by several real `nt_dispatch` syscalls (`NtSetInformationThread`/`NtQueryInformationThread ThreadWow64Context`) consistent with a live guest thread round-tripping its own WOW64 context -- strongly suggestive PeMain's JIT'd bytes began executing, though no literal "first guest instruction" print exists this pass to prove it directly (would need new instrumentation, out of this task's scope).
  - **(c) NOT reached**: no `hello8_x86`/`WriteFile`/C4-27-stub marker; terminates via `NtRaiseException id=207`, `code=0xC0000005` (STATUS_ACCESS_VIOLATION, not the old `0xC000001D`), `firstchance=0`, single clean RAISE#1 (no loop -- C4-24's fix still holds), `ctx.pc=libwow64fex.dll+0x34E4` -- **mapped fresh this pass, confirmed IDENTICAL to the pre-C4-28 `BTCpuSimulate+0x10` identity** (`Source/Windows/WOW64/Module.cpp:757`, `RtlCaptureContext`'s own capture point, the SAME architectural gap C4-18/21/24/26/27 all independently named: FEX's internal `hlt #0` `SignalHandlerReturnAddress` dispatcher marker, meant to be absorbed by an FEX-internal signal-return short-circuit, instead routed through this floor's full synthetic NT SEH cycle, which correctly finds no SEH handler for a JIT-internal marker and falls through to unhandled/terminate). `pe10 ret=3221225477` (`=0xC0000005` unsigned).
  - **(d) done, honestly recorded**: `map-fex-offsets.sh stock/wow64-chain/libwow64fex.dll 0x34E4` -> `BTCpuSimulate` (confidence high, embedded-symbol) -- no NEW DLL-relative address this pass; C4-28's entire fresh fault generation (`RtlImageNtHeader(NULL)`, `BTCpuResetToConsistentStateImpl`/`TlsSlots[16]=2`) is gone, confirmed not recurring anywhere in this pass's full transcript.
- **verdict**: this task's own null-image-base bug is CLOSED, receipted end-to-end (caller identity -> donor root cause -> fix -> TlsSlots resolution -> re-gate -> fresh ladder). The REMAINING blocker to `hello8_x86`'s WriteFile marker is the SAME pre-existing, already-named, already-out-of-scope architectural gap five prior legs independently reached and declined to fix speculatively (FEX-dispatcher-marker-vs-NT-SEH routing) -- this pass reaches it via a measurably healthier path (real TLS, real JIT activity, clean single-pass SEH walk) rather than the C4-28-introduced null-pointer detour, which is itself forward progress even though the terminal exit code is unchanged in kind (an unhandled-exception terminate, just from a different, now fully-explained, source fault).
- **re-gate, all green, no regression**: `pe8_gate.py` `ret=78`; `pe9_gate.py` both backends (`wowbox64 ret=0`, `fex ret=0`); `ramfb_gate.py` 7/7; `scratch/backtoback_gate.py` (pe6/7/8 x2 in one boot) PASS on a full ~110s run (first attempt under an external 150s wrapper was cut short mid-transcript, not a real failure -- rerun without the wrapper passed clean, matching HANDOFF's own documented "~2min wall time needed" note).
- **commits this pass**: `51d759e` (instrumentation: caller-LR resolve at guest-exception delivery, extends `sync_try_guest_exception`; PEB32.ImageBaseAddress wire), `c594741` (the fix: syswow64/ntdll.dll Phase-0 map + direct `nt_wow64_populate_init_block_if_ntdll32` call; `nt_wow64_populate_init_block_if_ntdll32` exposed non-static via `ntfile.h`).

## C4-28 — null-page guard gate salvage + fresh pe10-fex anatomy (guard changes the cascade)

- **kmain.c diff adjudication**: uncommitted 3x `puts("DBG a/b/c")` in `mmu_on()`, no functional change — DROPPED (`git checkout`).
- **guard-breaks-boot bug #1 (kmain.c, FIXED, kept: `volatile`)**: with the MMU off, `SCTLR_EL1.M=0` forces the ENTIRE address space to Device-nGnRnE memory type (ARM ARM), which requires natural alignment even for a plain `stp`. `d84c091`'s guard loop (`for i=1..511`, `l2_dev_table[0]` set separately) let the compiler fuse two adjacent element stores into one `stp` starting at an 8-byte-only-aligned offset (`l2_dev_table+8`) straddling a 16B boundary -> `ESR_EL1` EC=`0x25` DFSC=`0x21` (Alignment fault), crashing before `"MMU: on"` ever printed. Reordering the loop (index-0-last) only moved the SAME bug to `l1_table[0]`/`l1_table[1]`; `asm volatile("":::"memory")` barriers reduced but didn't fully suppress it (recurred inside the small `NT_RAM_BLOCKS` loop's own unrolling). `-mstrict-align` (tried, REVERTED) fixes the class architecturally but makes clang lower `fs.c`'s struct-copy code to a libcall `memcpy` this `-nostdlib` kernel doesn't provide (link fails). Final fix: `l1_table`/`l2_dev_table` declared `volatile` — C-semantics guarantee against store fusion, scoped to just these two arrays.
- **guard-breaks-boot bug #2 (ntboot.c, FIXED)**: `RTL_USER_PROCESS_PARAMETERS.CurrentDirectory.DosPath.Buffer` always NULL (field named in a comment since an earlier leg, never wired). Fault site: ntdll's internal `get_full_path_helper` (`map-fex-offsets.sh stock/ntdll.dll 0x51CDC`, embedded-symbol, confidence medium) — `ldr x22,[x24,#0x8]` loads `DosPath.Buffer`, then unconditionally `ldrh w8,[x22,#0x2]` (drive-letter `':'` check on `Buffer[1]`, no NULL guard) — `far=0x2` exactly matches `NULL+2`. Hit by pe6/pe7/pe8 because their `ImagePathName` is a deliberately bare relative exe name (see C4-6's own comment predicting exactly this dependency for the non-`wow64_mode` path). Fix: wire `CurrentDirectory.DosPath = "C:\\"` unconditionally.
- **guard-breaks-boot bug #3 (ntsvc.c, FIXED)**: `TEB.ActivationContextStackPointer` (offset `0x2c8`, well-known public Win64 TEB layout) always NULL. Fault site: ntdll's `RtlFindActivationContextSectionString` (`map-fex-offsets.sh stock/ntdll.dll 0x28180`, embedded-symbol, confidence medium) — `ldr x8,[x18,#0x2c8]; ldr x8,[x8]` dereferences the TEB field directly, only null-checking the RESULT of the load, not the field itself — `far=0x0`. Fix: `nt_build_teb` allocates + wires a small zeroed `ACTIVATION_CONTEXT_STACK` (only offset-0 `ActiveFrame` is read on this path; zeroed satisfies the immediately-following `cbz` fast-empty-stack branch).
- **all three bugs pre-existed** (address 0/near-0 was always semantically wrong to touch) but were masked by the OLD identity-mapped-as-device page 0 (silent garbage reads, never faulted) — exactly the class of bug `d84c091`'s guard exists to catch, now proven catching real bugs AND fixed without regressing anything.
- **re-gate, all green**: `pe8_gate.py` `ret=78`; `pe9_gate.py` both backends (wowbox64 `ret=0`, fex `ret=0`); `ramfb_gate.py` 7/7; `scratch/backtoback_gate.py` (pe6/7/8 x2 in one boot) — needs ~2min wall time, not the gate's own inline timeout, to fully complete. Direct `qemu-system-aarch64 -serial stdio` one-shot boots (bypassing the gate harness) used throughout this pass's debugging to get raw, unbuffered ESR/ELR/FAR/register receipts at each iteration — `pgrep qemu`+kill before/after every one.
- **commit**: `6af950a` (source-only: `build.sh`, `kmain.c`, `ntboot.c`, `ntsvc.c`).

### Fresh pe10-fex anatomy WITH the guard live (`scratch/pe10_probe.py fex`)

The brief's own hypothesis ("the null-page fault may CHANGE the whole cascade") is **confirmed true**: the fault chain this pass is a **completely different identity** from every prior leg (C4-18/20/21/23/24/26/27), none of which ever recurred in this pass's transcript.

```text
FAULT #1 (first in the whole run, right after "starting FEX (Hangover 11.9)" + full CPU-feature-registry walk):
  esr=0x96000006 ec=0x25 iss=0x6 elr=ntdll.dll+0x43DB8 far=0x0000000000000000
  map-fex-offsets.sh stock/ntdll.dll 0x43DB8 (FEX_SOURCE stub, embedded-symbol only, confidence medium)
    => RtlImageNtHeader, start=0x180043d84
    0x43db4: ldur x8,[x29,#-0x18]   (x8 = module base arg, x0)
    0x43db8: ldrh w8,[x8]           <- FAULTS: reads MZ signature; x8=NULL => far=0
  interpretation: something called RtlImageNtHeader(NULL) -- caller not yet instrumented this pass.

FAULT #2..#N (all subsequent faults in the run, repeating, NOT a hang -- NtRaiseException-cycled):
  esr=0x96000006 ec=0x25 iss=0x6 elr=libwow64fex.dll+0x3B1C far=0x0000000000000042
  map-fex-offsets.sh stock/wow64-chain/libwow64fex.dll 0x3B1C (default FEX_SOURCE, embedded-symbol, confidence high)
    => BTCpuResetToConsistentStateImpl(_EXCEPTION_POINTERS*), start=0x180003af0
    +0x24: mov  x20, x18                    (x20 = TEB)
    +0x28: ldr  x21, [x20, #0x1500]          (x21 = TEB->TlsSlots[16], WOW64_TLS_THREAD_STATE)
    +0x2c: ldr  x23, [x21, #0x40]            <- FAULTS (this IS elr+0x3B1C): far = x21+0x40
  live CHALICE TLS PROBE at this fault: slot16(ThreadState)=0x0000000000000002
  arithmetic check: 0x2 + 0x40 = 0x42 = far  -- confirmed exact.
```

- **`TlsSlots[16]` identity**: FEX's own private per-thread `InternalThreadState*` slot (`Source/Windows/include/winternl.h:28-29` `Slot::THREAD_STATE`; `Module.cpp:101-102` `TLS.ThreadState()` accessor: `reinterpret_cast<...*&>(TEB->TlsSlots[Slot::THREAD_STATE])`). The ONLY donor-confirmed write to this exact slot is `Module.cpp:626` inside `BTCpuThreadInit()`: `TLS.ThreadState() = Thread;` where `Thread = CTX->CreateThread(0,0)` — a real heap pointer, never a small integer.
- **`2` is not this floor's own doing** — grepped `TlsSlots` writes across `ntboot.c`/`ntsvc.c`: only slot 1 (`WOW64_TLS_CPURESERVED`) and slot 10 (`WOW64_TLS_WOW64INFO`) are ever floor-written; slot 16 is never touched by this floor at all (read-only, C4-22's own probe).
- **rival verdict, new evidence, still INCONCLUSIVE** — no `BTCpuThreadInit` narration/side-effect visible anywhere in the transcript between the `starting FEX` banner (`BTCpuProcessInit`) and this fault; the theorized real-pointer write (`Module.cpp:626`) does not appear to have executed. Two live candidates, neither confirmed this pass: (i) something reads `TlsSlots[16]` before `BTCpuThreadInit` ever runs (rung-C "abnormal arrival" flavor — `BTCpuResetToConsistentStateImpl` firing before thread-init completed), or (ii) FEX has an undiscovered lazy-init/placeholder-tag convention for this slot not found in this pass's donor read (only the one real-pointer write site was located; no smaller sentinel constant grep-matched nearby in `Module.cpp`). NOT resolved — next worker's starting point, needs `BTCpuThreadInit` call-site instrumentation (new instrumentation, appropriately out of THIS pass's "measure with existing tools first" scope) and/or a `RtlImageNtHeader` caller trace (who passes it NULL).
- **ladder rung: (c)** — new, more precise, genuinely DIFFERENT mapped frontier (not a repeat of C4-18/21/24/26/27's `BTCpuSimulate+0x10` identity, which never recurred once in this full transcript). The guard moved the frontier EARLIER (into FEX's own WOW64-boundary exception-reconstruction machinery, `BTCpuResetToConsistentStateImpl`, and an even-earlier `RtlImageNtHeader(NULL)` call) rather than deeper — real forward information for the syscall-hinge investigation, though the underlying A/B/C rivals from the original C4-28 brief are not yet adjudicated against this new anatomy.

---

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
