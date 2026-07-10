# C4 — hosted bootstrap design (Shrine HolyC + BackA64 inside Aiwnios)

Decision record (nyari, 2026-07-10), built on four recon sweeps: collision
map, kernel-header dependency extraction (425 names), Aiwnios boot/CLI
semantics, and the Shrine-vs-Aiwnios layout diff.

## Evidence summary

1. **Aiwnios is a full HolyC compiler in C** (c/lex.c, parser.c, optpass.c,
   arm_backend.c …) plus a HolyC-side self-hosting compiler in `Src/`
   (NRoot's rewrite). `--bootstrap`: C compiler compiles
   `Src/FULL_PACKAGE.HC` → resident HolyC compiler AOT-compiles itself via
   `Cmp()` → new `HCRT2.BIN`. Normal boot just `Load()`s the image.
   CLI: `-t <dir>` boot drive, `-c` cmdline mode with positional boot
   files run through `ExeFile`.
2. **Function redefinition is a silent shadow, newest wins**
   (c/parser.c:2996-3012 → c/hash.c HashAdd prepends, HashFind takes head).
3. **Layouts**: leaf IC types identical (CICType, CICArg,
   CIntermediateCode fields, COptReg, CAOTCtrl, CAOTAbsAddr; MDG_*, RT_*,
   RTF_*, CCF_*, CMT_*, CMF_*, IC_BODY_SIZE, shared IC_* values all equal).
   Containers diverge: Aiwnios `CCodeCtrl.coc_head` is `CQue` (16 B), not
   an embedded `CIntermediateCodeBase`; `CDbgInfo.body` is `U8*[]` not
   `U32[]`; `CCmpCtrl` gains inserted `lb_static` (+trailing
   `asm_local_scope`); `CCodeMisc` gains inserted `addr2` (+`offset`,
   `rpn`); `IC_ICS_NUM` 0xB9 vs 0xBF; Aiwnios adds ~18 opcodes.
4. Consequence: **Aiwnios's HolyC front end does not emit the Shrine IC
   contract** our backend consumes (CRPN/CQue pipeline). Shadowing
   `OptPass789A` in a live session would feed BackA64 a foreign IC stream.

## Decision: wholesale shadow — load the full Shrine compiler world

Do NOT graft BackA64 onto Aiwnios's front end. Instead, into a booted
Aiwnios, load in order:

1. **Shrine header extract** — Shrine's compiler-contract classes and
   defines (CompilerA.HH + the KernelA/B/C.HH subset from the 425-name
   dependency report), *redefining* (shadowing) Aiwnios's versions.
   From then on every subsequently compiled source resolves fields,
   sizeof and constants against **Shrine layouts, self-consistently**.
2. **Shrine front end (KEEP set)**: Lex, PrsLib, PrsExp, PrsStmt, PrsVar,
   OptPass012, OptPass3-6, OptLib, AsmLib, AsmResolve, CInit, CExts, CMain.
3. **zoneC backend**: A64Defs.HH, Arm64Asm.HC, BackA64Lib.HC, BackA64.HC,
   BackA64Br.HC, BackA64F.HC, OptPass789A64.HC, A64Stubs.HC.

Why this is sound:
- Shrine's CMain allocates its own CCmpCtrl etc. with Shrine sizeof; the
  host's C code never touches those objects. Host interaction is only via
  runtime services taking scalars/pointers/strings (MAlloc, CAlloc, Free,
  Print, Hash*, QueVectU8*, FileRead, StrCpy, throw, SysTry/SysUntry …)
  — all present in Aiwnios per the collision map.
- Our C2 backend was written against Shrine's contract → zero rework.
- All shadowed names (COCCompile, Lex, ExeFile internals…) resolve
  newest-wins for subsequent lookups; the host keeps its own already-
  resolved addresses and stays alive.

## GATE: PASSED (recon 2026-07-10)

Both redefinition kinds are silent shadows, same newest-wins pattern as
functions:
- CLASS: c/parser.c:4003-4043 — existing full class discarded
  (`bungis=NULL`), fresh CHashClass HashAdd'd, no duplicate abort.
- #define: c/lex.c:800-829 — fresh CHashDefineStr HashAdd'd
  unconditionally, no check at all. No #undef in HolyC lexer.
- `#assert` is a warned no-op (lex.c:642). Sized-class prefix
  (`U16 class`) IS supported despite no PRSF_ flag: Aiwnios's own
  Src/KernelA.HH:1612 uses `U16 class CICType` and bootstraps.

## Refinement: cross-compiler frame (shadow vs alias partition)

The naive shadow breaks at the host boundary: Shrine's front end reads
`Fs->hash_table` and walks CHash* entries that the HOST created with
Aiwnios layouts. Shadowing CTask/CHash* with Shrine layouts would apply
Shrine offsets to host objects. Resolution — the loaded Shrine world is
a **self-contained cross-compiler**:

- **SHADOW set** (Shrine layouts, from the extract): the compiler
  contract — CICType, CICArg, CIntermediateCode(Base), CCodeCtrl,
  CCmpCtrl, CDbgInfo, CCodeMisc, CAOTCtrl, CAOTAbsAddr, CAsmArg, lexer
  state classes, and all IC_/MDG_/RT_/RTF_/CCF_/CMT_/CMF_ defines. Plus
  Shrine's own CHash* + hash functions so the compiler owns a private
  symbol universe (AOT symbols go into cc->htc tables under
  CCF_AOT_COMPILE, never the host table — TempleOS kernel-build style).
- **ALIAS set** (do NOT shadow; compile against the host's live defs):
  CTask and anything host services create/consume. Field-name
  compatibility for the `Fs->` accesses the KEEP set makes is the one
  open audit (in flight).
- CDbgInfo.body: DECIDED — widen `U32 body[1]` → `U8 *body[1]` in the
  extract (we control it; OptPass789A64 stores full pointers; Aiwnios
  made the same widening).
- JIT smoke's calls into host services (Print) get patched as scalar
  addresses — safe across the boundary.

## Runbook (M4 path)

1. `aiwnios -c -t <bootdir> LoadA64.HC` — loader does the ordered
   includes above.
2. Smoke: JIT `"Hello GAIA\n";` through Shrine's COCCompile + BackA64
   (in-memory buf), execute, compare output.
3. Patch `back_a64_sin/cos/tan/atan_addr` with host libm-backed
   addresses before any float trans op compiles.
4. AOT: drive Shrine's `Cmp()`-equivalent with CCF_AOT_COMPILE to emit
   modules for the Zone A kernel.
5. **M4**: the loaded compiler compiles its own source tree through
   BackA64 — self-host closed.

## Open items

- Shrine header extract must compile hosted: strip kernel-only pieces
  (interrupt/task internals) not in the 425-name usage set.
- IC_ENTER/LEAVE frame-data source + IC_MOV_FS/GS gap: surfaced by the
  smoke test; fix in BackA64Br/front-end glue when observed.
- OptPass3 pool audit vs A64Defs.HH REG_A64_POOL_LO/HI.
- CDbgInfo.body: our OptPass789A64 stores full pointers; with Shrine's
  U32 body under the shadow this truncates on 64-bit host heaps —
  decide at smoke time (widen in the extract to U8* like Aiwnios did,
  since we control the extract).
