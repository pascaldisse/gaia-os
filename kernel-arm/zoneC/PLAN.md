# Zone C — the Keystone: ZealC/HolyC compiler → aarch64

**Goal:** the ZealC compiler emits aarch64, so GAIA-OS grows in HolyC and the
C bootstrap scaffolding melts away. Self-hosting inside the OS = milestone M4.

## Anatomy of the 22,067 lines (measured)

| Layer | Files | ~Lines | Port effort |
|---|---|---|---|
| Front end (arch-blind) | Lex, LexLib, Parse* | ~6,000 | **zero** — carries over |
| Optimizer | OptLib, OptPass012/3/4/5/6 | ~4,000 | near-zero |
| Late passes / regalloc | OptPass789A (84 x86 refs) | ~1,350 | rework register model |
| Backend (x86-64 emission) | BackA/B/C, BackFA/FB, BackLib, Templates (185 refs) | ~5,000 | **replace** — the real work |
| Inline assembler (x86 syntax) | Asm*, UAsm | ~3,500 | defer (see C3) |
| Glue | CMain, CInit, CExterns, CHash, CMisc | ~2,200 | light touches |

## Donor: Aiwnios ARM backend (6,818 lines C, built & working on this Mac)
- `c/arm64_asm.c` (1,260 ln): clean aarch64 instruction encoders —
  `ARM_addImmX`, `ARM_adrX`, `ARM_cmpImmX`, ... One function per instruction form.
- `c/arm_backend.c` (5,322 ln): IR→encoder lowering patterns, prolog/epilog,
  calling convention, register mapping. Our lowering *reference*, not a copy-paste
  (Aiwnios IR ≠ ZealC IC), but every hard decision has a worked example here.

## Phases
- **C0 — encoders:** port `arm64_asm.c` → `Arm64Asm.ZC`. Mechanical 1:1
  translation, testable standalone (emit bytes, compare against clang -c output).
- **C1 — IC audit:** document ZealC's intermediate code ops (OpCodes.DD,
  CompilerB.HH) — the contract the new backend must honor.
- **C2 — backend:** new `BackA64*.ZC` lowering IC→encoders. Register model:
  map ZealC's virtual regs onto x19-x28 (callee-saved) + x0-x17 scratch;
  keep HolyC's own calling convention (stack args, like Aiwnios TOS mode) for
  compatibility with existing HolyC code idioms.
- **C3 — inline asm:** x86 `Asm*.ZC` excluded from the ARM build; ARM asm
  syntax is a later luxury. Kernel asm needs are already covered by Zone A stubs.
- **C4 — bootstrap chain:**
  1. Run ZealC front end + new ARM backend *hosted inside Aiwnios* on this Mac
     (Aiwnios runs HolyC natively on Apple Silicon today).
  2. Cross-compile HolyC modules → flat binary linked into the Zone A kernel.
  3. M4: compiler runs *inside* GAIA-OS, compiles itself. Self-hosting. Temple.

## Size honesty
New code to write: ~1,300 ln encoders (mechanical) + ~2,500-4,000 ln backend
(the thinking part) + glue. The 10k lines of front end + optimizer come free.
