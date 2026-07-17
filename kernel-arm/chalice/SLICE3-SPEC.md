# CHALICE SLICE 3 — stock ntdll→kernelbase→kernel32 chain over the NT floor

Goal: Wine's own PE ntdll.dll initializes on GAIA-OS, loads kernel32/kernelbase
through our NT services, and a console exe prints via the full chain.
Anchor: HANDOFF.md (contract+laws) · slice-2 floor @ 89cf6f0.
Recon receipts: room chat-mroybsby 07-17 — brief A (Wine boot contract),
brief B (floor audit). File:line cites below = those briefs.

## Laws (inherited, non-negotiable)
- Vocabulary: OS-compatibility register only (map sections, resolve imports,
  implement NT services).
- QEMU: NEVER on stdio. Gate = `python3 scratch/chalice_gate.py` (unix-socket
  harness) + `scratch/ramfb_gate.py`. Build loop: the gate script itself runs
  build.sh — you never run QEMU by hand.
- ⚠ LLP64: PE-side (`aarch64-windows-gnu`) `unsigned long` = 32-bit. Every
  pointer-target integer in PE-side test code = explicit 64-bit (SIZE_T
  semantics). By-value 32-bit args are safe; by-pointer width mismatch is not.
- Donors read-only: wine/ reactos/ proton/.
- Serial atoms: never start the next atom while one is uncommitted.
- Commit per green atom; NEVER push.

## Architecture verdict (from recon A — do not re-derive)
1. **Phase 0 = kernel side, direct calls, no dispatcher.** Wine's unix side
   maps PE ntdll.dll + main exe itself (`unix/loader.c:1707,1420`,
   `virtual_map_module` direct call), allocs TEB/stack, maps KUSER, builds
   process params — THEN jumps into PE code. GAIA equivalent: pemod does the
   initial ntdll+exe mapping directly. The file/section services are only
   needed for what PE ntdll loads LATER (kernel32, kernelbase).
2. **Jump contract** (`unix/signal_arm64.c:1522-1581`): build synthetic
   CONTEXT {X0=exe_entry, X1=arg, X18=teb, Sp=stack_top, Pc=RtlUserThreadStart},
   push CONTEXT onto the new stack (16-aligned), enter PE at LdrInitializeThunk
   with x0=&pushed_ctx, x18=teb. PE side runs loader_init, then
   NtContinue(ctx, TRUE) unwinds to RtlUserThreadStart → kernel32 →
   exe entry. Our NtContinue already works (slice 2).
3. **unixlib = pointer writes, no syscall** (`unix/loader.c:1558-1594`): after
   mapping ntdll.dll, kernel looks up named exports and WRITES:
   `*__wine_syscall_dispatcher = <nt_dispatch entry>` ·
   `*__wine_unixlib_handle = <unix_call_funcs table>`. Also must exist as
   exports: LdrInitializeThunk, RtlUserThreadStart, KiUser*Dispatcher,
   DbgUiRemoteBreakin, LdrSystemDllInitBlock, __wine_ctrl_routine,
   __wine_unix_call_dispatcher. unix_call_funcs (8): load_so_dll,
   unwind_builtin_dll, dbg_write, server_call, server_fd_to_handle,
   server_handle_to_fd, spawnvp, system_time_precise — console subset:
   dbg_write (serial), system_time_precise (KUSER), rest = clean
   STATUS_NOT_IMPLEMENTED stubs until a gate proves otherwise.
4. **PE-side loader does its own imports/Ldr.** loader_init sets peb->LdrData
   from PE data (`loader.c:4458`) — kernel must NOT pre-build a fancy Ldr for
   the stock path. Import fixup happens in PE code; the kernel only serves
   NtOpenFile → NtCreateSection(SEC_IMAGE) → NtMapViewOfSection →
   NtProtectVirtualMemory → NtQueryVirtualMemory (`loader.c:2709,2737,2822,
   1191,2361`).
5. **Console needs no conhost** iff ProcessParameters.StandardOutput is a
   valid inherited handle — GetStdHandle reads the PEB, no service call.
6. Soft-fail-OK: registry keys (defaults used), NLS/apiset (degrades),
   events on the linear print-and-exit path.

## Atoms (serial)

### Atom 3A — DLL machinery + loader-path services (sourcing-independent)
Kernel work, zoneA:
- **Blob store**: manifest-driven embed (build.sh xxd loop → generated table
  `{name, bytes, len}`), lookup by case-insensitive name. Replaces 3
  hardcoded externs. New file ok (ntfile.c or store in ntsvc.c).
- **Handle table**: flat array, kernel-wide; kinds {console, file, section}.
  Pseudo stdout 0x14 preserved. NtClose releases.
- **NtOpenFile / NtReadFile**: name (UNICODE_STRING in OBJECT_ATTRIBUTES,
  strip path prefix, e.g. `\??\C:\windows\system32\kernel32.dll` →
  `kernel32.dll`) → blob store handle. NtReadFile: offset+length copy,
  IO_STATUS_BLOCK.Information = bytes. LLP64 discipline in any test caller.
- **NtCreateSection**: SEC_IMAGE over a file handle only; anything else →
  STATUS_INVALID_PARAMETER. Section object = ref to blob.
- **NtMapViewOfSection**: run pemod's mapping machinery (headers, sections,
  DIR64 relocs) at a fresh page-aligned base in the DLL territory
  (0x45000000→0x7ffe0000 free gap, brief B §4 — new bump allocator there,
  NOT the 64MB heap). NO import resolution kernel-side (verdict 4). Relocs
  applied kernel-side → return STATUS_SUCCESS (not IMAGE_NOT_AT_BASE).
  Out: base + view size. Registry entry recorded.
- **Image registry**: {name, base, size, entry, is_dll} per mapped image
  (incl. Phase-0 maps later). Name→base lookup.
- **Export parser** (pemod growth): find_named_export(base, "Name") walking
  IMAGE_DIR_EXPORT — name table, ordinal table, address table. Forwarders:
  detect (RVA inside export dir) → return distinct error for now.
- **NtProtectVirtualMemory**: flat arena = no page protection; validate args,
  record, return old=PAGE_READWRITE, STATUS_SUCCESS. Documented deviation.
- **NtQueryVirtualMemory**: MemoryBasicInformation only — answer from image
  registry + vm arena; unknown addr → one sane free region. Else
  STATUS_INVALID_INFO_CLASS.
- **NtAllocateVirtualMemory growth** (RtlCreateHeap prerequisite): support
  RESERVE then COMMIT-at-given-base two-step — *BaseAddress != NULL inside an
  already-tracked region + MEM_COMMIT → STATUS_SUCCESS bookkeeping no-op
  (flat arena is always committed). Untracked base → keep rejecting.

Gate 3A (extend hello set + chalice_gate.py):
- `mini.dll` (zig cc, no imports): exports `MiniAdd(a,b)=a+b+58`,
  DllMain-free (EXTRADLLFLAGS-equivalent: entry returns 1).
- `hello4.exe` (zig cc, ntstub path like hello3): NtOpenFile("mini.dll") →
  NtCreateSection → NtMapViewOfSection → walks mini.dll's export dir ITSELF
  (~40 loc test-side parser, LLP64-clean) → calls MiniAdd(1,1) → expects 60 →
  NtWriteFile "CHALICE: the cup links" → NtClose file+section →
  NtTerminateProcess(60).
- Shell verb `pe4`; serial lines: all slice-2 lines + `CHALICE: the cup links`
  + `pe4 ret=60`. chalice_gate.py `need` list extended. ramfb gate stays green.
- mkpe.sh builds mini.dll + hello4; build.sh manifest embeds all blobs.

### Atom 3B — boot protocol (fake-ntdll dress rehearsal)
Kernel work:
- `nt_boot_process(exe_name, ntdll_name)`: Phase-0 emulation — map ntdll blob
  + exe blob via pemod (registry entries), alloc thread stack (64K in DLL
  territory) + TEB/PEB as today PLUS: TEB.NtTib StackBase/StackLimit,
  ProcessParameters CommandLine/ImagePathName UNICODE_STRINGs + hStdInput/
  hStdError, PEB.ImageBaseAddress=exe. Ldr = leave minimal (verdict 4).
- unixlib wiring: find_named_export writes into mapped ntdll:
  __wine_syscall_dispatcher ← dispatcher entry, __wine_unixlib_handle ←
  kernel unix_call_funcs table. Missing export → clear serial error line.
- `__wine_unix_call_dispatcher` entry (asm or C shim): Wine signature
  (handle, code, args) → table[code](args). Implement dbg_write → serial,
  system_time_precise → KUSER InterruptTime-derived; others → 0xC0000002.
- CONTEXT push + enter: exact §Architecture 2 shape. New verb `pe5 <exe>`.
Gate 3B:
- `fake-ntdll.dll` (zig): exports the required name set; LdrInitializeThunk
  body = __wine_unix_call(dbg_write "CHALICE: thunk enters") → NtContinue(ctx)
  (stub path); RtlUserThreadStart = tail-call x0 entry. KiUser*/others = ret
  stubs. `hello5.exe`: plain entry, prints "CHALICE: the chain speaks" via
  NtWriteFile, exits 99.
- Serial: slice-2+3A lines + `CHALICE: thunk enters` + `CHALICE: the chain
  speaks` + `pe5 ret=99`. Proves jump contract, unixlib call path, CONTEXT
  protocol — before any stock binary exists.

### Atom 3C — stock adoption (opens after 3B green + artifact decision)
- **Source DECIDED (recon C, verified by download + PE-header inspection):**
  Hangover release `hangover-11.9` distro tar → `hangover-wine_*.deb` →
  `data.tar.xz:./usr/lib/wine/aarch64-windows/{ntdll,kernel32,kernelbase}.dll`
  — genuine Machine=0xAA64 (NOT ARM64EC). Extract:
  `curl -L <asset>.tar | tar -xO '<deb>' > x.deb && ar x x.deb && tar -xJf
  data.tar.xz ./usr/lib/wine/aarch64-windows/...`. License LGPL 2.1.
  WineHQ/distros ship NO arm64 PE builds (verified 404). llvm-mingw
  (`20260616-ucrt-macos-universal`) = toolchain fallback for synthetic
  payloads only.
  ⚠ Version skew: shipped DLLs = wine 11.9; recon-A receipts came from the
  wine/ donor checkout — at 3C re-verify contract details against the actual
  binaries (export set, unixlib table arity), not the donor tree.
- Embed the 3 DLLs, `pe6` boots exe against stock ntdll. Expected gap classes:
  keyed events, TLS info classes, extended query infos, arena growth, CRT
  probes. Each gap: implement smallest honest handler, re-gate. Splits into
  3C-1 (artifacts+first walk w/ fault log) and 3C-2+ (gap burn-down) as needed.

## Sizing / worker notes
- 3A ≈ 450-550 loc across ntsvc.c/pemod.c/build.sh/mkpe.sh + 2 test binaries —
  one sonnet atom w/ checkpoint commit before test loop; if summon cap hits,
  continuation resumes at test loop (pattern: slice-2 postmortem).
- 3B ≈ 300-400 loc. 3C unsized until artifacts.
- Every new PE-side file: LLP64 note in header comment.
