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

## GATE (recon pending): class/#define shadow semantics

The design assumes the C-side parser shadows CLASS definitions and
#define redefinitions as silently as functions. If class redefinition
errors instead → fallback: rename-prefix Shrine's diverging container
classes (mechanical batch rename in the extract + front end, ghoulable).
If #define redefinition errors → wrap the extract's defines or boot with
a trimmed bootstrap; note IC_ICS_NUM and the 18 extra opcodes are the
only value conflicts.

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
