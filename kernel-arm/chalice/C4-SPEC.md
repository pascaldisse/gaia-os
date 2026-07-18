# CHALICE C4 — x86 door, leg 1: 32-bit x86 hello.exe over the NT floor

Goal: a genuine 32-bit x86 (Machine 0x14C) hello.exe, unmodified, loads and
prints via Hangover's translator chain mounted above our proven ARM64 NT
floor (pe8/hello7, HEAD dada57f). MXO-primary (32-bit ≈99% of target games,
per HANDOFF NEXT). Anchor: HANDOFF.md laws · SLICE3-SPEC.md exemplar format.

## Recon receipts (this leg, session 2026-07-18)

### 1. Donor artifact inventory — Hangover 11.9 release, EXACT

Source: `hangover-11.9` release tar (`hangover_11.9_debian13_trixie_arm64.tar`,
fetched+inspected this session, 260MB) contains **4 separate .deb payloads**,
not 1 — `fetch-stock-dlls.sh` only ever pulled the main
`hangover-wine_11.9~trixie_arm64.deb`. The CPU-backend debs are separate:
- `hangover-wine_11.9~trixie_arm64.deb` — the Wine build itself, TWO PE
  guest-arch trees: `usr/lib/wine/aarch64-windows/` (native 64-bit host-side
  DLLs, ~600 files) AND `usr/lib/wine/i386-windows/` (32-bit guest DLLs,
  ~600 files, parallel set — ntdll/kernel32/kernelbase/win32u/user32/gdi32/
  wow32/etc, all real x86 PE).
- `hangover-libwow64fex_11.9_arm64.deb` — `usr/lib/wine/aarch64-windows/
  libwow64fex.dll` (FEX-backed x86 CPU-simulation module).
- `hangover-libarm64ecfex_11.9_arm64.deb` — `usr/lib/wine/aarch64-windows/
  libarm64ecfex.dll` (FEX-backed x64/ARM64EC CPU-simulation module — the
  OTHER Wow64 path, x64-on-arm64, NOT this leg — Teardown's future door).
- `hangover-wowbox64_11.9_arm64.deb` — `usr/lib/wine/aarch64-windows/
  wowbox64.dll` (Box64-backed x86 CPU-simulation module — same interface
  as libwow64fex.dll, alternate engine).

Verified via `llvm-objdump -f` + a raw Machine-field read (`e_lfanew@0x3c`→
`+4` u16 LE) on every candidate, this session:

| file | dir | format | Machine | role |
|---|---|---|---|---|
| `ntdll.dll` | `i386-windows` | coff-i386 | `0x14C` | 32-bit guest ntdll |
| `kernel32.dll` | `i386-windows` | coff-i386 | `0x14C` | 32-bit guest kernel32 |
| `kernelbase.dll` | `i386-windows` | coff-i386 | `0x14C` | 32-bit guest kernelbase |
| `win32u.dll` | `i386-windows` | coff-i386 | `0x14C` | 32-bit guest win32u (C3 territory) |
| `wow32.dll` | `i386-windows` | coff-i386 | `0x14C` | legacy 16-on-32 shim, NOT needed |
| `wow64.dll` | `aarch64-windows` | coff-arm64 | `0xAA64` | **native-side WOW64 dispatcher** (runs in the 64-bit host process, translates the 32-bit process's syscalls — Wine donor confirms, see §2) |
| `wow64win.dll` | `aarch64-windows` | coff-arm64 | `0xAA64` | native-side win32k-syscall WOW64 shim (C3/graphics territory, not this leg) |
| `libwow64fex.dll` | `aarch64-windows` | coff-arm64 | `0xAA64` | **x86 CPU-sim backend, FEX engine** — full BTCpu* interface (21 exports incl. `BTCpuSimulate`/`GetContext`/`SetContext`/`ProcessInit`/notify-hooks) |
| `wowbox64.dll` | `aarch64-windows` | coff-arm64 | `0xAA64` | **x86 CPU-sim backend, Box64 engine** — same BTCpu* interface, smaller export set (14, no notify-hooks except Dirty/Free/Protect/UnmapView), imports `ntdll.dll`+`wow64.dll` |
| `xtajit64.dll` | `aarch64-windows` | **coff-arm64ec** (`0xA641`) | ARM64EC CPU-sim, x64-on-arm64 path — NOT this leg |
| `libarm64ecfex.dll` | `aarch64-windows` | coff-arm64ec (`0xA641`) | FEX ARM64EC backend — NOT this leg |

**No `wow64cpu.dll` or `xtajit.dll` (Microsoft's real names) ship anywhere.**
Confirmed by grep: vanilla Wine's own `get_cpu_dll_name()`
(`wine/dlls/wow64/syscall.c:909-923`) resolves the x86-on-arm64 case to
`xtajit.dll` — a Microsoft-proprietary module upstream Wine has never shipped
(no `wine/dlls/xtajit/` dir exists, only `xtajit64/` for the ARM64EC case).
Hangover fills this gap with its own two BTCpu-compatible replacements
(`libwow64fex.dll`, `wowbox64.dll`) and (per its own build, not visible in
our vanilla `wine/` donor — that donor is wine-mirror HEAD 11.13, NOT
Hangover's fork) patches the loader to look up one of these two names
instead of the hardcoded `xtajit.dll` — mechanism unconfirmed (registry key
or env var most likely, Wine convention), open item for atom 1.

**Engine choice (Pascal's ruling per HANDOFF §NEXT, "FEX vs Box64" — still
open):** both ship in 11.9, same interface, drop-in alternates. This spec
treats them as interchangeable at the mount-point (one `BTCpu*` table wire),
so the choice doesn't block atom sequencing — bind whichever Pascal picks,
same atoms either way.

fetch-stock-dlls.sh: **NOT yet extended** (time-boxed out this leg) — still
only pulls the 4-DLL aarch64-windows subset from the main wine deb. Needed
extension for atom 1: add `i386-windows/{ntdll,kernel32,kernelbase}.dll` +
one CPU backend (`libwow64fex.dll` or `wowbox64.dll`, Pascal's call) +
`wow64.dll` to the fetch set, from the SAME already-downloaded main deb
(no new asset) plus the one small companion deb for the chosen backend.

### 2. Boot-protocol deltas (WOW64 process init vs our proven 64-bit boot)

Traced in donor `wine/` (mirror 11.13 — architecture-level, version-skew
caveat per SLICE3-SPEC §3C still applies at implementation time):

- **`get_cpu_dll_name()`** (`dlls/wow64/syscall.c:909`): on ARM64 native,
  resolves to `xtajit.dll` (Hangover: `libwow64fex.dll`/`wowbox64.dll`).
  `load_64bit_module()` (`syscall.c:1011`) loads it into the **native
  (64-bit) address space**, not the 32-bit one — our kernel's DLL-territory
  mapping path (pemod/`nt_territory_alloc`) already handles arbitrary ARM64
  PE maps; no new mapping primitive needed, just point it at a 3rd image.
- **`map_wow64cpu()`** (`dlls/ntdll/loader.c:4365`): opens
  `\??\C:\windows\sysnative\wow64cpu.dll` (real Windows path convention —
  Hangover's equivalent open call resolves to its own substituted name) —
  our NtOpenFile name-resolution (blob-store/FS-store, `ntfile.c`) needs
  this path recognized/aliased, same "strip prefix" logic as slice-3's
  `\??\C:\windows\system32\*.dll`.
  `xtajit64.dll` (`loader.c:4278`) is the sibling open for the ARM64EC case
  — same code shape, different leg.
- **`struct _PEB32`** and **`struct _TEB32`** (`include/winternl.h:833`,
  `:1061`): the 32-bit process needs 32-bit-layout PEB/TEB in ADDITION to
  the 64-bit ones our floor already builds — WOW64 processes carry BOTH
  (native 64-bit TEB with a `WowTebOffset`-style link, plus a 32-bit TEB the
  guest x86 code actually reads via `fs:`-equivalent). `nt_build_teb`
  (`ntboot.c`) must grow a second (32-bit-shaped, smaller-offset) TEB/PEB
  pair, cross-linked.
- **`WOW64_CPURESERVED`** (`winternl.h:4375`) + `CROSS_PROCESS_WORK_LIST`
  (`:333`, referenced from TEB `CrossProcessWorkList` `+0x010` — a NEW TEB
  field, not in our current 64-bit-only `nt_build_teb`): per-thread
  CPU-simulation scratch area (`RtlWow64GetCpuAreaInfo`, `winternl.h:5476`)
  the `BTCpu*` functions read/write directly — this is the actual "32-bit
  register file" the CPU backend simulates against. New kernel allocation +
  TEB wiring, size/layout TBD from Hangover source (not in vanilla donor —
  `RtlWow64GetCpuAreaInfo` body lives in `ntdll/unix/`, needs a targeted
  read next atom).
- **`Wow64ApcRoutine`** (`winternl.h:4413`): APC delivery gains a WOW64-aware
  variant — soft-fail-OK for the first mount (no APCs on the linear
  hello-print path, same "soft-fail-OK" doctrine as slice-3 §Architecture 6).
- **Which NT services gain WOW64-specific behavior:** per `dlls/wow64/*.c`
  file split — `process.c` (Nt*Process/Thread create, needs 32/64 dual
  CONTEXT), `virtual.c` (Nt*VirtualMemory — 32-bit address-space truncation,
  our flat 0.75GB territory is already well under 4GB so no truncation
  logic needed YET, just pointer-width narrowing in the thunks), `file.c`
  (32/64 struct-size differences, `struct32.h` has the shape catalog),
  `registry.c` (same), `sync.c`/`system.c` (same pattern). **Net: WOW64
  isn't new NT services, it's a THUNK LAYER in `wow64.dll`/`wow64win.dll`
  that narrows 32-bit-shaped syscall args to our EXISTING 64-bit services
  before calling `nt_dispatch`** — our floor's job is to host wow64.dll's
  own thunking correctly, not reimplement it. This is the single most
  important architectural fact of this leg: **we do NOT hand-roll a
  parallel 32-bit NT service table** — Wine's `wow64.dll` does that
  translation for us, the same way `nt_dispatch` already serves plain
  64-bit Wine code.

## Atoms (serial, per HANDOFF discipline: never start atom N+1 uncommitted)

### Atom C4-1 — chain inventory + mount prep (THIS SESSION, partial)
- [x] Enumerate exact 32-bit chain (table above), committed in this file.
- [x] 32-bit x86 hello PE toolchain proof: `hello8_x86.exe`
      (`zig cc -target x86-windows-gnu`, same freestanding pattern as
      `hello1.c`) — Machine `0x14C` verified.
- [ ] Extend `fetch-stock-dlls.sh`: add i386-windows ntdll/kernel32/
      kernelbase + wow64.dll + ONE CPU backend to `stock/` (Pascal's engine
      choice needed first — blocks nothing else in this atom list, backend
      binds are late in the chain).
- Gate: none yet (no kernel code touched) — `pe8_gate.py` must stay PASS
  once any zoneA file changes (none this atom).

### Atom C4-2 — native WOW64 image trio maps
- `nt_boot_process` grows an optional WOW64 mode: map `wow64.dll` +
  `wow64win.dll` + chosen CPU-backend DLL (all coff-arm64, existing pemod
  path) into DLL territory alongside 64-bit ntdll — 3 more image-registry
  entries, no new primitive.
- Gate: new verb (`pe9`?) that boots to "wow64.dll mapped" + "CPU backend
  mapped" serial lines only — no guest code runs yet, just prove the 3
  images load+relocate clean over our existing territory allocator.

### Atom C4-3 — 32-bit TEB/PEB + CrossProcessWorkList
- `nt_build_teb` grows the 32-bit-shaped sibling TEB/PEB pair (§2 above) +
  `CrossProcessWorkList` field on the 64-bit TEB.
- Gate: serial dump of both TEB pointers + cross-link field, sanity-checked
  offsets against `winternl.h` `_TEB32`/`_PEB32` (LLP64 note: this is the
  ONE place our kernel code must be 32-bit-struct-aware, not just LLP64).

### Atom C4-4 — BTCpu wiring + `sysnative`/`SysWOW64` path aliasing
- `NtOpenFile` name resolution: recognize `\??\C:\windows\sysnative\*`
  (native-arch DLL alias) and a 32-bit-tree path (mapped to `i386-windows`
  stock dir) distinctly — two lookup roots, not one.
- Wire the CPU backend's `BTCpu*` table the same way slice-3 wired
  unixlib's `__wine_syscall_dispatcher`/`__wine_unixlib_handle`
  (`find_named_export` + direct writes) — `wow64.dll` calls `BTCpuProcessInit`
  etc. directly once it resolves the backend module, no kernel dispatch
  needed for this part.
- Gate: "CHALICE: BTCpu wired" serial line, `BTCpuProcessInit` observably
  called (log line inside the shim or a controlled fault if not — measure,
  don't guess, per HANDOFF method).

### Atom C4-5 — x86 hello prints
- `hello8_x86.exe` (already built, atom 1) run through the full chain:
  32-bit ntdll → kernel32 → `WriteFile`/`ExitProcess`, actually executing
  AS 32-bIT CODE via the CPU backend's `BTCpuSimulate`.
- Gate: **the actual pass criterion for this whole leg** — serial line from
  32-bit code, `pe9 ret=42` (or whatever hello8 prints), zero regression on
  `pe8_gate.py`'s full required-green list.

## Sizing
- C4-1 remainder (fetch script extension) ≈ 60-90 loc, one sitting.
- C4-2/C4-3 ≈ 300-400 loc combined (mostly TEB32/PEB32 struct + mapping
  glue, patterned on existing `nt_build_teb`/`nt_boot_process`).
- C4-4/C4-5 unsized — first real "does BTCpuSimulate actually run 32-bit
  code under our territory model" unknown, likely the hardest atom of the
  whole C4 slice (register-file/context-switch correctness matters here in
  a way pe1-pe8 never needed, since a REAL CPU backend is simulating, not
  just importing/calling ARM64 code).
