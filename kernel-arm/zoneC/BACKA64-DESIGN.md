# BackA64 — the aarch64 backend for Shrine HolyC (Zone C, phase C2)

Design document. This is the thinking part of C2: every decision the
implementation needs, made here, so the code phase is mechanical.

**What it replaces:** `shrine/Compiler/BackA.HC BackB.HC BackC.HC BackFA.HC
BackFB.HC BackLib.HC` and the codegen `switch` in `OptPass789A.HC`.
**What it keeps:** everything else. Passes 0–6 (`OptPass012/3/4/5/6`) are
arch-neutral except for the register table in pass 3 and the `REG_*`
constants — see §2. The IC contract is `IC-HolyC.md` (canonical, from
shrine); `IC-ZealC.md` is cross-reference only.
**Instruction encoders:** `Arm64Asm.HC` (C0 deliverable, 221 fns from
Aiwnios), plus the two amendments in §8.

---

## 1. Ground rules

1. **Same pass architecture.** `COCCompile()` keeps its pipeline. BackA64 is
   a new set of emitter helpers behind the same `switch(tmpi->ic_code)` in a
   new `OptPass789A` body. Each node emits into `tmpi->ic_body[0..ic_cnt)`
   (`IC_BODY_SIZE`=0x83=131 bytes = 32 ARM instructions — ample; the fattest
   lowering below is ~14 instructions). Passes 7–9 re-walk until branch
   forms converge; pass 10 MemCpy's bodies into the real buffer.
2. **All ARM instructions are 4 bytes.** `ic_cnt` is always a multiple of 4.
   Convergence in passes 7–9 is about *which* branch sequence is emitted
   (short `B.cc` vs. inverted-branch trampoline, §6), not byte-count of one
   instruction.
3. **SP is pushable.** GAIA-OS sets `SCTLR_EL1.{M,C,I}` only — **SA=0**, so
   SP need not stay 16-aligned between calls. `str xN,[sp,#-8]!` /
   `ldr xN,[sp],#8` are the exact PUSH/POP equivalents. This is a deliberate
   OS-level ruling (our OS, our trap config) that preserves Shrine's 8-byte
   stack-slot model and the front end's arg-offset math unchanged.
4. **Raw system words.** Ops with no Arm64Asm encoder (MRS/MSR/DC/TLBI/WFI/
   ERET-adjacent) are emitted as literal 32-bit words via `ICU32()` — a
   comment names the instruction at each site. No new encoder unless the op
   takes register operands in ≥3 lowerings (that's why FSQRT/FABS get
   encoders and TLBI does not).

---

## 2. Register model

| ARM | role | x86 analogue |
|---|---|---|
| `x0` | primary accumulator / int+ptr return value | RAX |
| `x1` | second operand | RCX/RDX |
| `x2` | third operand / address scratch | RBX |
| `x9`–`x15` | backend-internal scratch (address synth, swaps, div temps) | — |
| `x16`,`x17` | call/branch veneers (MOVZ/MOVK+BLR sequences) | — |
| `x18` | **platform register — never touched** | — |
| `x19`–`x28` | **OptPass3 register-allocated locals** (10 regs, callee-saved) | RSI,RDI,R10–R15 |
| `x29` | frame pointer | RBP |
| `x30` | link register | return-addr slot |
| `sp` (31) | stack pointer | RSP |
| `d0` | float accumulator ("ST0") | x87 ST0 |
| `d1` | float second operand ("ST1") | x87 ST1 |
| `d8`–`d15` | reserved for future float reg-alloc — **v1 leaves floats stack/memory-resident** | — |

`REG_*` constants become the ARM numbering directly: `REG_Xn = n`,
`REG_RAX→0`, `REG_RBP→29`, `REG_RSP→31`. OptPass3's allocatable pool becomes
`{19..28}` (a *gain*: 10 regs vs x86's 6). `CICArg.reg` low byte carries the
Xn number; the SIB high byte carries `index_reg | scale<<6` unchanged.

Sign/size discipline: all values live **widened to 64 bits in registers**
(Shrine's model too). Narrow loads sign/zero-extend at the load (`ldrsb/
ldrb/ldrsh/ldrh/ldrsw/ldr w`); narrow stores truncate at the store
(`strb/strh/str w`). `RTF_UNSIGNED` on `raw_type` picks the extend flavor
and the U/S division/compare/branch forms — exactly the switch Shrine does.

---

## 3. Frame + calling convention (TOS-style HolyC, Aiwnios "TOS mode")

The front end already computes arg offsets assuming the x86 frame
`[RBP]=old-RBP, [RBP+8]=ret-addr, args at [RBP+16+n]`. We reproduce that
shape bit-for-bit so **zero front-end changes** are needed:

**Caller** (per call):
```
; each IC_PUSH:            str  xN, [sp, #-8]!        ; leftmost arg pushed first
; IC_CALL (absolute, JIT): movz/movk x16, <addr>×(1-4) ; §6 sizing
                           blr  x16
; IC_CALL (near, same buf): bl  <rel26>                ; when in ±128MB, passes 7-9
```
Callee pops args (TempleOS convention) — the caller does **not** ADD SP.

**IC_ENTER** (prologue):
```
str  x30, [sp, #-8]!          ; ret addr    -> [x29+8] after frame set
str  x29, [sp, #-8]!          ; old FP      -> [x29]
mov  x29, sp
sub  sp, sp, #<locals>        ; addImmX/subImmX; >4095 -> movz x9 + sub reg
stp  x19, x20, [sp, #-16]! …  ; pairs, only regs OptPass3 actually assigned
```
**IC_LEAVE** (epilogue):
```
ldp  … x20, x19               ; restore used pairs
mov  sp, x29
ldr  x29, [sp], #8
ldr  x30, [sp], #8
add  sp, sp, #<argc*8>        ; the RET-imm16 arg pop
ret                           ; Ff_INTERRUPT -> eret instead (ARM_eret)
```

**Return value:** x0 always — including F64 *as raw bits* (`fmov x0, d0`
before LEAVE; `fmov d0, x0` at the consumer). This mirrors Shrine's
everything-in-RAX convention and keeps `IC_GET_RVAL*` trivial.

**IC_PUSH/IC_POP:** `strPreImmX(rt, 31, -8)` / `ldrPostImmX(rt, 31, 8)`.
Float variants go through `fmov` to an X reg — the stack is untyped I64
slots, same as Shrine. **IC_PUSH_REGS/IC_POP_REGS** (`ic_data` = reg mask):
walk the mask, emit `stp`/`ldp` pairs, odd tail gets a single `str`/`ldr`.
**IC_ADD_RSP:** `addImmX(31,31,n)`. Pass 5/6 stack bookkeeping is untouched
(slots are still 8 bytes).

---

## 4. Operand materialization — `BackA64Lib.HC`

The whole backend funnels through four helpers; get these right and the 185
opcodes are table-filling.

**`A64LoadArg(I64 rx, CICArg *a, I64 raw_type)`** — bring an operand into Xrx:
- `MDF_REG`: `movRegX` (skip if same reg).
- `MDF_IMM` (`disp` is the value): shortest of — `movzImmX` (+`movkImmX`
  per nonzero 16-bit chunk), `movnImmX` for mostly-ones, `orrImmX` from
  XZR when `LogicalImmX`-encodable. Helper `A64MovImm(rx, val)`; worst
  case 4 instructions.
- `MDF_DISP` (`reg`+`disp`): scaled-unsigned `ldrRegImmX` when
  `0 ≤ disp ≤ 32760` and 8-aligned (analogous per size); else `ldur*` when
  `-256 ≤ disp ≤ 255`; else `A64MovImm(x9, disp); addRegX(x9, x9, base);
  ldr [x9]`.
- `MDF_SIB` (`base + index<<scale`): ARM shifted-register loads require
  shift == log2(access size) or 0. When x86 scale matches → one
  `ldrRegRegShiftX` (etc.). Mismatch → `addShiftRegX(x9, base, index,
  scale); ldr [x9, #0]`.
- `MDF_RIP_DISP32` (globals): JIT knows the absolute address →
  `A64MovImm(x9, addr); ldr [x9]`. (AOT later hooks the same site with a
  `CAOTAbsAddr` reloc on the MOVZ/MOVK group.)
- `MDF_STK`: pop — `ldrPostImmX(rx, 31, 8)`.
- Size/sign of the load from `raw_type`: I8→`ldrsbX` U8→`ldrb`(+zero-ext ok)
  I16→`ldrshX` U16→`ldrh` I32→`ldrswX` U32→`ldr w` I64/U64/PTR→`ldr x`.

**`A64StoreRes(I64 rx, CICArg *a, I64 raw_type)`** — mirror image with
`strb/strh/str w/str x` and the same DISP/SIB/abs-address fallbacks
(scratch x10 so a loaded x9 address can still be live).

**`A64LoadArgF(CICArg*)` / `A64StoreResF(CICArg*)`** — d0/d1 via
`ldrRegImmF64`/`strRegImmF64`, or through `fmovI64F64` when the value sits
in an X reg / on the stack.

**`A64Deref(rx, a)`** — address-only variant (for lvalue RMW ops and
LEA-alikes): synthesizes the effective address without loading.

**Condition map** (`A64CC(cond, unsigned)`), used by every compare/branch:

| x86 | ARM signed | ARM unsigned |
|---|---|---|
| JZ/JE | EQ | EQ |
| JNZ/JNE | NE | NE |
| JL / JB | LT | LO |
| JGE / JAE | GE | HS |
| JG / JA | GT | HI |
| JLE / JBE | LE | LS |
| JC / JNC | — | CS / CC |

Float compares (`fcmp`) use the same GE/GT/… codes; NaN falls out unordered
exactly as Shrine's x87 comparisons did (no extra NaN branch in v1).

---

## 5. Lowering table — all 185 opcodes by family

**(a) Never reach codegen** — assert-throw if seen in pass 7+:
`IC_END_EXP, IC_IMM_I64, IC_IMM_F64, IC_SIZEOF, IC_TYPE, IC_RBP, IC_REG,
IC_BR_AND_AND_ZERO/NOT_ZERO, IC_BR_OR_OR_ZERO/NOT_ZERO` (expanded in pass
0-2), `IC_SHORT_JMP*` x86-isms if produced (they are pass-internal).
`IC_NOP1/IC_NOP2` → emit nothing. `IC_LABEL` → record address, emit nothing.
`IC_END` → sentinel.

**(b) Data movement**
| IC | lowering |
|---|---|
| `IC_ASSIGN*` (all sizes) | `A64LoadArg(x0, arg2)` → `A64StoreRes(x0, res)` |
| `IC_DEREF*` | covered by LoadArg modes |
| `IC_LEA`-alikes (`IC_ADDR`) | `A64Deref(x0, arg1)` → store |
| `IC_MOV_*_REG` etc. | `movRegX` |
| `IC_SWAP_U8/16/32/I64` | load a→x9, load b→x10, store x10→a, x9→b |
| `IC_PUSH/POP/PUSH_REGS/POP_REGS/ADD_RSP` | §3 |
| `IC_GET_RVAL*` | value already in x0; extend per size if narrow |

**(c) Integer arithmetic** (binary: load arg1→x0, arg2→x1, op, store res;
compound-assign `IST_ASSIGN` forms: `A64Deref` the lvalue once, load-op-store
through x9):
| IC | ARM |
|---|---|
| ADD / SUB | `addRegX` / `subRegX` (imm arg2 → `addImmX/subImmX` when 0-4095) |
| MUL / SQR_I64 / SQR_U64 | `mulRegX` (low 64 bits — Shrine semantics) |
| DIV signed/unsigned | `sdivRegX` / `udivX` |
| MOD / MOD_U64 | `sdivRegX(x9,x0,x1)` (or `udivX`); `mulRegX(x9,x9,x1)`; `subRegX(x0,x0,x9)` — no MSUB encoder, composed |
| AND / OR / XOR | `andRegX/orrRegX/eorRegX`; imm forms via `LogicalImmX` else materialize |
| NOT (~) | `mvnRegX` |
| NEG | `subRegX(x0, xzr=31? no — use `negsRegX`)` |
| SHL / SHR / SAR | `lslvRegX/lsrvRegX/asrvRegX`; imm counts → `lslImmX/lsrImmX/asrImmX` |
| ABS_I64 | `cmpImmX(x0,0)`; `negsRegX(x9,x0)`→clobbers flags — order: `negsRegX(x9,x0)` **first**, then `cmpImmX(x0,0)`, `cselX(x0, x0, x9, GE)` |
| SIGN_I64 | `cmpImmX(x0,0)`; `csetX(x9,GT)`; `csetX(x10,LT)`; `subRegX(x0,x9,x10)` |
| MIN/MAX I64/U64 | `cmpRegX(x0,x1)`; `cselX(x0,x0,x1, LT/GT/LO/HI)` |
| `++`/`--` (IC_MM/PP families) | Deref lvalue, `ldr`, `addImmX/subImmX #1` (scaled by ptr size from `ic_class2` — pass 0-2 already scaled), `str` |

**(d) Bit ops** (`arg1`=value/lvalue, `arg2`=bit index):
- Bit index masked: `andImmX(x1, x1, 63)` (encodable logical imm).
- `IC_BT` (value): `lsrvRegX(x9, x0, x1)`; `andImmX(x0, x9, 1)`.
- `IC_BTS/BTR/BTC` (RMW): `A64MovImm(x9,1)`; `lslvRegX(x9,x9,x1)`; old-bit
  via `lsrv+and` into x0; then `orrRegX/`bic(=`andRegX` of `mvnRegX`)/
  `eorRegX` and store back.
- `IC_LOCK_BTS/BTR/BTC`: `ldaxrX`/`stlxrX` retry loop around the same RMW
  (encoders exist; loop is 5 instructions + `cbnzX` on the status reg).
- Fused `IC_BR_BT*` family: same extract, then `tbnz/tbz` when the bit index
  is an immediate, else `cbz/cbnzX` on the extracted bit.

**(e) Compares + branches**
- Value-producing `IC_EQU_EQU/NOT_EQU/LESS/…`: `cmpRegX` + `csetX(A64CC)`.
- `IC_BR_ZERO/NOT_ZERO`: `cbzX/cbnzX` (±1MB; far form §6).
- Fused `IC_BR_EQU_EQU…LESS_EQU`: `cmpRegX` (`fcmp` + `fmov` loads when
  `ICF_USE_F64`) + `bcc(A64CC)`.
- Chained `*2` forms (`IC_PUSH_CMP` partner): `cset` the current compare,
  `andRegX` with the popped previous flag, store to `res` — no branch,
  exactly Shrine's semantics.
- `IC_BR_AND_ZERO/AND_NOT_ZERO`: `andsRegX(x9,x0,x1)` + `bcc(EQ/NE)`.
- `IC_BR_MM_ZERO/NOT_ZERO`: Deref, `ldr`, `subImmX #1`, `str`, `cbz/cbnz`.
- `IC_BR_CARRY/NOT_CARRY`: `bcc(CS/CC)` — preceded by ops that set C
  (our BT lowering sets x0 not C, so pass 0-2's producers of these are
  audited at implementation time; fallback: `cbnzX/cbzX` on the extracted
  bit reg).
- `IC_JMP`: `b` (±128MB, always fits a JIT buffer). `IC_JMP_TABLE`:
  bounds-check (`cmpImmX` + `bcc(HI)` to default), `A64MovImm(x9, table)`,
  `ldrRegRegShiftX(x9, x9, idx, 1)`, `br(x9)`; the table itself stays a
  `CMT_JMP_TABLE` misc entry of absolute I64 addresses (pass-10 emits it
  after the code, unchanged).

**(f) Calls** — §3. Five variants: `IC_CALL` (imm addr) → veneer or `bl`;
`IC_CALL_IND` → `A64LoadArg(x16, arg1)`; `blr(16)`. `IC_RET` folds into
LEAVE.

**(g) Float** (d0/d1 accumulator discipline, memory-resident, mirrors x87
stack without the stack):
| IC | ARM |
|---|---|
| FADD/FSUB/FMUL/FDIV | `faddReg/fsubReg/fmulReg/fdivReg (d0,d0,d1)` |
| FNEG | `fnegReg` |
| I64→F64 (`IC_TO_F64`) | `scvtf` (`ucvtf` if RTF_UNSIGNED) |
| F64→I64 (`IC_TO_I64`) | `fcvtzs` (truncate-toward-zero = x86 CVTTSD2SI/Shrine) |
| float CMP | `fcmp(d0,d1)` + `csetX`/`bcc` |
| `IC_SQR` | `fmulReg(d0,d0,d0)` |
| `IC_ABS` | `ARM_fabsReg` — **amendment A1** |
| `IC_SQRT` | `ARM_fsqrtReg` — **amendment A1** |
| `IC_SIN/COS/TAN/ATAN` | runtime call: `fmov x0,d0`(arg via stack per §3); veneer-BLR to kernel `Sin/Cos/Tan/ATan`; result bits → d0. aarch64 has no hw transcendentals; TempleOS's x87 templates become library calls. The kernel math lib is a Zone-A/E deliverable (poly approximations, ~1 page). |
| float imm | `ldrLabelF64` from the `CMT_FLOAT_CONSTS` pool (mechanism unchanged) |

**(h) x86-specific opcodes — explicit rulings**
| IC | ruling |
|---|---|
| `IC_IN_U8/16/32`, `IC_OUT_U8/16/32` | **compile-time `throw('CmpErr')`** with message "no port I/O on aarch64 — use MMIO". No silent NOP: code that reaches these must be ported, not humored. |
| `IC_RDTSC` | raw word `MRS Xt, CNTVCT_EL0` = `0xD53BE040|Rt` |
| `IC_CLFLUSH` | raw `DC CIVAC, Xt` = `0xD50B7E20|Rt`, then `DSB SY` `0xD5033F9F` |
| `IC_INVLPG` | raw `TLBI VAAE1, Xt` (`0xD5088760|Rt`? — verify exact encoding at impl time against ARM ARM C5.5) + `DSB ISH` + `ISB` |
| FS/GS get/set (per-task ptrs) | `TPIDR_EL1` for GS-role (kernel per-cpu), `TPIDR_EL0` for FS-role: raw `MRS/MSR` words. GAIA-OS task struct pointer lives in TPIDR_EL1 (Zone A already owns x18? no — x18 unused; TPIDR is the ruling). |
| `IC_GET_RFLAGS/SET_RFLAGS` | `MRS/MSR NZCV` (+DAIF merged into bit positions documented in the impl) — only NZCV+DAIF are meaningful to HolyC code |
| `IC_GET/SET_CARRY` | `csetX(x0, CS)` / set: `cmpImmX` trick — `subsImmX(xzr-form)` loading C from bit 0 via `negsRegX(x9,x0)`… impl detail; documented fallback: keep a virtual carry in x15 if audit shows cross-node C liveness |
| `IC_INTERRUPT on/off` | raw `MSR DAIFSet, #2` / `MSR DAIFClr, #2` |
| `IC_HALT` | raw `WFI` = `0xD503207F` |

---

## 6. Branch sizing + convergence (passes 7–9)

All conditional forms have limited range: `B.cc`/`CBZ/CBNZ` ±1MB,
`TBZ/TBNZ` ±32KB, `B` ±128MB. Per branch node, pass 7 assumes the short
form; each subsequent pass re-measures the displacement (targets tracked
exactly as Shrine does via `ic_data` label resolution):
- fits → keep short form;
- doesn't → **inverted-condition trampoline**: `b.!cc +8 ; b <target>`
  (2 words). Once widened, never re-narrowed (monotonic, so `code_size`
  shrink-loop in `COCCompile` terminates exactly as before).
Absolute-address calls always emit the fixed-size 5-word veneer
(`movz+movk×3+blr`) — constant size, no oscillation, patchable in place.

---

## 7. Files + phase plan (implementation atoms)

| file | content | est. |
|---|---|---|
| `BackA64Lib.HC` | §4 helpers, condition map, `A64MovImm`, raw-word emit, ICU32-into-ic_body plumbing | ~400 ln |
| `BackA64.HC` | families (b)(c)(d) int ops | ~700 ln |
| `BackA64Br.HC` | (e)(f) branches, calls, ENTER/LEAVE, jump tables | ~500 ln |
| `BackA64F.HC` | (g) float + (h) system ops | ~350 ln |
| `OptPass789A64.HC` | the driver switch — mechanical: one case per opcode calling the family helper | ~600 ln |

Order: Lib → (int ops ‖ branches ‖ float) → driver. After this design, the
four family files are ghoul-able (zero decisions left); the Lib and driver
I write myself first as the reference idiom.

## 8. Amendments to Arm64Asm.HC (A1)

Two encoders Aiwnios never needed (it also calls libm):
```holyc
I64 ARM_fsqrtReg(I64 d, I64 n) { return ARM_ERR_CHK(0x1E61C000 | n<<5 | d); } // FSQRT Dd,Dn
I64 ARM_fabsReg (I64 d, I64 n) { return ARM_ERR_CHK(0x1E60C000 | n<<5 | d); } // FABS  Dd,Dn
```
(Exact error-macro shape to match the file's existing single-instruction
encoders like `ARM_fnegReg` — copy its body pattern.)

## 9. What C3/C4 need from this

- **C3**: the ARM build excludes `Asm*.HC` (x86 inline-asm chain) and
  `BackA/B/C/FA/FB/BackLib.HC`; includes the §7 files. `Lex.HC`,
  `PrsLib/PrsExp.HC`, `OptPass012/3/4/5/6`, `CInit.HC`, `CHash.HC` compile
  unmodified except the `REG_*`/pool constants (§2) and dropping the
  x86-templates table (`CMP_TEMPLATE_*` → §5g rulings).
- **C4**: run this backend hosted inside Aiwnios (`aiwnios --bootstrap`,
  binary at `aiwnios/aiwnios.app/Contents/MacOS/aiwnios`), JIT-compile a
  HolyC hello through it, then cross-emit modules for the Zone A kernel.
  M4 = this compiler compiles its own five §7 files inside GAIA-OS.
