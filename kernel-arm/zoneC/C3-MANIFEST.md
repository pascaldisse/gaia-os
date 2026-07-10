# C3 — x86 exclusion manifest for the aarch64 HolyC compiler build

Decision record (nyari, 2026-07-10), from the C3 recon sweep of
`shrine/Compiler/` (113 functions in the candidate cut set, 9 with callers
outside it, 104 with zero outside references).

## Cut — never loaded in the ARM build

| file | why |
|---|---|
| `BackA.HC` `BackB.HC` `BackC.HC` | x86 int/branch code emission — replaced by `BackA64.HC` + `BackA64Br.HC` |
| `BackFA.HC` `BackFB.HC` | x86 float emission — replaced by `BackA64F.HC` |
| `BackLib.HC` | x86 emission helpers — replaced by `BackA64Lib.HC` |
| `OptPass789A.HC` | x86 pass 7-9/10 driver — replaced by `OptPass789A64.HC` |
| `Asm.HC` `AsmInit.HC` | x86 inline assembler + opcode tables |
| `UAsm.HC` | x86 unassembler (disassembly listings) |

## Kept whole — dependency check PASSED (2026-07-10)

| file | why kept |
|---|---|
| `AsmLib.HC` | `AOTStoreCodeU8/U32/U64/U8At` called from arch-neutral CMain/PrsStmt/PrsVar (AOT byte-storage, arch-neutral machinery) |
| `AsmResolve.HC` | `AOTGlblsResolve` called from arch-neutral code |

Verified: none of the 88 functions defined in the 10 cut files — nor
`inst_hash`/`uasm_hash`/`asm_hash`/any `*_tbl` — appears in either file.
AsmLib defines `AOTStoreCodeU8/U32/U64/U8At, AsmLexExpression, AsmLineLst`;
AsmResolve defines `AsmResolve, AOTLocalsResolve, AOTGlblsResolve,
AsmUnresolvedAdd`. Fully self-contained.

## The 9 outside-referenced symbols — resolution via `A64Stubs.HC`

Zero edits to arch-neutral Shrine files. The shim defines, under the
original names:

| symbol | call sites | resolution |
|---|---|---|
| `OptPass789A` | `PrsLib.HC:283-306`, `CExts.HC:22` | thin wrapper → `OptPass789A64` (same 4-arg signature) |
| `Un` | `CMain.HC:297`, `PrsLib.HC:312-318`, `PrsStmt.HC:187` | no-op stub (no aarch64 unassembler yet; listings print a notice) |
| `PrsAsmBlk` | `PrsStmt.HC:915,952` | throws — `asm{}` blocks unsupported until the aarch64 assembler front end exists (post-M4) |
| `AsmHashLoad` / `UAsmHashLoad` | `CMain.HC:698-699` | no-op stubs (no x86 opcode tables to hash-load) |
| `AOTStoreCodeU8/U64/U8At`, `AOTGlblsResolve` | CMain/PrsStmt/PrsVar | satisfied by keeping `AsmLib.HC`/`AsmResolve.HC` (above) |

## Constants

- `REG_RAX/REG_RBP/REG_RSP/REG_REGS_NUM`: defined in kernel headers (not the
  Compiler dir), used by arch-neutral OptPass3/4/5/6, CInit, PrsLib, OptLib,
  PrsExp. ARM build supplies `A64Defs.HH` **instead of** the x86 values
  (REG_RAX=0, REG_RBP=29, REG_RSP=31, REG_REGS_NUM=32; pool x19-x28).
- `CMP_TEMPLATE_*` (`CompilerA.HH:315-333`): sweep done — all 27 uses are
  inside cut files (BackFA.HC ×3, BackFB.HC ×6, OptPass789A.HC ×18). The
  defines stay as dead-but-harmless.
- `REGS_NUM` (unprefixed): zero occurrences — nothing to do.

## Open items carried to C4

- Actual load-order file (`LoadA64.HC`) is a C4 deliverable: it depends on
  the consolidated header set for the hosted (Aiwnios) bootstrap, where
  Shrine's kernel headers can't be included wholesale.
- OptPass3 pool audit: verify its non-allocatable-register logic against
  `REG_A64_POOL_LO/HI` (it may hardcode x86 registers).
- `IC_MOV_FS`/`IC_MOV_GS` (0x16/0x18): handled by no family file — driver
  throws if the front end emits them.
- `IC_ENTER`/`IC_LEAVE` frame-data source (packed `ic_data` vs `cc` fields):
  reconcile against the front end at C4.
