# ZealC Intermediate Code (IC) Contract

Extraction/documentation reference for writing a new ZealC backend. All quoted
code is verbatim from `zealos/src/Compiler/` and `zealos/src/Kernel/KernelA.HH`
(paths below are relative to `/Users/pascaldisse/projects/gaia-os/`). No
methodology or design commentary — this is a description of the compiler's
existing IC contract, extracted by reading the source and its two consuming
backends (`OptPass012..6` peephole/lowering passes and `OptPass789A` final
x86 code generator).

Sources read:
- `zealos/src/Compiler/CompilerA.HH` — `IC_*` opcode `#define`s (the numbering).
- `zealos/src/Compiler/CompilerB.HH` — public compiler API (no IC content).
- `zealos/src/Kernel/KernelA.HH` — actual `CIntermediateCode` struct layout,
  `RT_*` raw types, `MDf_*`/`MDF_*` addressing-mode flags, `ICF_*` flags,
  `REG_*` register numbers (the struct is **not** in the Compiler/ headers;
  it lives in the kernel header because `CIntermediateCode` is forward
  declared as a kernel-wide class).
- `zealos/src/Compiler/CInit.ZC` — `intermediate_code_table[]`, the
  authoritative per-opcode arg-count/metadata table.
- `zealos/src/Compiler/OptPass789A.ZC` — the x86 codegen walk (`switch [tmpi->ic_code]`).
- `zealos/src/Compiler/OptPass012.ZC`, `OptPass3.ZC`, `OptPass4.ZC`,
  `OptPass5.ZC`, `OptPass6.ZC`, `OptLib.ZC`, `ParseExp.ZC`, `ParseLib.ZC` —
  used to confirm which opcodes are lowered away before codegen and how.

---

## 1. The `CIntermediateCode` struct

`CIntermediateCode` is declared with real inheritance
(`CIntermediateCode : CIntermediateCodeBase`) — ZealC/HolyC lays inherited
fields out first, contiguously, so a pointer to a `CIntermediateCode` is also
a valid `CIntermediateCodeBase*`. One instance = one IC list node (a
doubly-linked queue element via `next`/`last`).

Verbatim, from `zealos/src/Kernel/KernelA.HH:1782-1832`:

```c
U16 class CICType
{
	U8		raw_type, mode;
};

class CICArg
{
	CICType type;
	U16 	reg; //low is reg, high is index_reg+scale<<6
	I64 	disp;
};

class CICTreeLinks
{
	CHashClass			*arg1_class, *arg2_class;
	CIntermediateCode	*arg1_tree, *arg2_tree;
	CHashClass			*class2;
};

class CIntermediateCodeBase
{
	CIntermediateCode	*next, *last;
	U16 				 ic_code,
						 ic_precedence;
	I16 				 ic_count,
						 ic_last_start;
};

class CIntermediateCode:CIntermediateCodeBase
{
	I64			 ic_flags,
				 ic_data,
				 ic_line;
	CHashClass	*ic_class, *ic_class2;
	CICArg		 arg1, arg2, res;
	U8			 arg1_type_pointed_to; //Used for IST_ASSIGN and IST_DEREF ic_codes.
	union
	{
		U8 ic_body[IC_BODY_SIZE];
		//Tree Links are created in OptPass012.  An ADD opcode, for example,
		//points back to its two earlier arg CIntermediateCode's.
		//Tree links get destroyed during Pass789A when they get overwrites
		//by machine code.	(Saves room to union the output machine code buffer
		//with these links, since they are not needed after pass4.
		//Tree links are used during passes 012 and 3 for determining types.
		CICTreeLinks t;
	};
#assert !($$ & 7)
};
```

`IC_BODY_SIZE` = `0x83` (131 bytes) — `KernelA.HH:1778`.

### Field table

**Inherited from `CIntermediateCodeBase` (6 fields):**

| Field | Type | Meaning (inferred from usage) |
|---|---|---|
| `next`, `last` | `CIntermediateCode*` | Doubly-linked queue pointers. The whole function body is one IC list, `cc->coc.coc_head` is the sentinel head (`ic_code == IC_END`), inserted via `QueueInsert(tmpi, cc->coc.coc_head.last)` in `ICAdd()` (`ParseLib.ZC:86-105`). |
| `ic_code` | `U16` | The opcode — one of the `IC_*` values (§2 below). Low 16 bits of the `opcode_and_precedence` argument passed to `ICAdd`. |
| `ic_precedence` | `U16` | Operator precedence, high 16 bits of the same argument (`opcode_and_precedence.u16[1]` in `ICAdd`); used by the parser to encode `PREC_*` (`CompilerA.HH:340-357`), not read by the backend. |
| `ic_count` | `I16` | Length in bytes of the machine code this node has emitted so far into `ic_body` (grows across passes 7-10 as `ICU8`/`ICU16`/etc. append bytes; reset to 0 at the top of each pass in `OptPass789A.ZC:66`). |
| `ic_last_start` | `I16` | Byte offset in `ic_body` where the *previous* sub-instruction started, used to roll back partial emission when `ICF_DEL_PREV_INS` fires (peephole delete-previous-instruction optimization, `OptPass789A.ZC:1244-1249`). |

**Own to `CIntermediateCode` (10 more field slots, one being a union):**

| Field | Type | Meaning (inferred from usage) |
|---|---|---|
| `ic_flags` | `I64` | Bitfield of `ICF_*` flags (§4) — signedness/float conversion requests, "result unused," short-jump, lock prefix, pass-trace marker, etc. |
| `ic_data` | `I64` | Generic opcode-specific payload: immediate value (`IC_IMM_I64`), pointer to a `CCodeMisc` label (`IC_LABEL`, `IC_JMP`, `IC_SUB_CALL`), pointer to a `CHashFun`/`CHashGlobalVar`/`CAOTHeapGlobal` (`IC_CALL_EXTERN`, `IC_CALL_IMPORT`, `IC_ABS_ADDR`, `IC_HEAP_GLOBAL`), stack-adjust byte count (`IC_ADD_RSP`, `IC_ENTER`), or a `CAOTCtrl`-owned inline-asm blob (`IC_ASM`). Its meaning is opcode-dependent and must be read per-opcode. |
| `ic_line` | `I64` | Source line number, set from `cc->last_line_num` at `ICAdd()` time; used for debug-info line tables in `OptPass789A.ZC:1253-1258`. |
| `ic_class` | `CHashClass*` | The result's static type/class (e.g. used to recover the raw type for `IC_MOV_FS`/`IC_MOV_GS` via `CompRawType(tmpi->ic_class)`). |
| `ic_class2` | `CHashClass*` | A second class slot, used where two operand types matter (seen paired with `t.class2` in `CICTreeLinks`). |
| `arg1`, `arg2` | `CICArg` | The two source operands (see `CICArg` below). Whether both, one, or neither are populated is dictated by the opcode's arg count (§2, `intermediate_code_table[].arg_count`, one of `IS_0_ARG`/`IS_1_ARG`/`IS_2_ARG`/`IS_V_ARG`). |
| `res` | `CICArg` | The destination operand. `res.type.mode == 0` means "no result" (checked throughout `OptPass789A.ZC`, e.g. line 1220 `if (tmpi->res.type.mode)`). |
| `arg1_type_pointed_to` | `U8` | For `IST_DEREF`/`IST_ASSIGN`-class opcodes: the raw type the dereferenced pointer points to (set in `OptPass3.ZC` when folding `IC_LEA`+`IC_DEREF` into a plain `IC_MOV`). |
| `ic_body` / `t` (union) | `U8[0x83]` / `CICTreeLinks` | Dual-purpose storage. Early passes (0-3) use `t` (`arg1_tree`/`arg2_tree`/`arg1_class`/`arg2_class`/`class2`) to keep a back-pointer tree of the operands' producing IC nodes, for type inference and constant folding. From pass 4 onward this space is reused (unioned) as `ic_body`, the raw encoded x86 machine-code bytes for this IC node, appended to by `ICU8`/`ICU16`/`ICU24`/`ICU32`/`ICU64` helper macros/functions during `OptPass789A`. |

**Field count:** 6 inherited (`CIntermediateCodeBase`) + 10 declared in
`CIntermediateCode` (`ic_flags`, `ic_data`, `ic_line`, `ic_class`,
`ic_class2`, `arg1`, `arg2`, `res`, `arg1_type_pointed_to`, and the
`ic_body`/`t` union counted once) = **16 total field slots** (17 if the two
alternatives inside the union are counted separately).

### Related structs

```c
U16 class CICType
{
	U8		raw_type, mode;
};

class CICArg
{
	CICType type;
	U16 	reg; //low is reg, high is index_reg+scale<<6
	I64 	disp;
};
```

- `CICType.raw_type` — one of `RT_*` (§4).
- `CICType.mode` — one of `MDf_*`/`MDF_*` addressing-mode flags (§4); `0`
  means "unused operand."
- `CICArg.reg` — packed register field: low byte is the base register
  (`REG_*`, §4), high byte is `index_reg + scale << 6` for SIB-style
  indexed addressing.
- `CICArg.disp` — displacement / immediate value, depending on `type.mode`
  (`MDF_IMM` → immediate value; `MDF_DISP`/`MDF_SIB`/`MDF_RIP_DISP32` →
  byte displacement).

There is also a **separate, per-opcode static metadata struct** — not part
of a `CIntermediateCode` instance, but a 186-entry table indexed by
`ic_code`, quoted in full in §2:

```c
class CIntermediateStruct
{
	U8		 arg_count, res_count, type;
	Bool	 fpop, not_const, pad[3];
	U8		*name;
};
```
(`CompilerA.HH:13-18`, table instance `intermediate_code_table[IC_ICS_NUM]`
in `CInit.ZC:26`.)

---

## 2. IC opcode table

Source of the numbering: `zealos/src/Compiler/CompilerA.HH:20-238`
(`#define IC_END 0x00` … `#define IC_POPCNT 0xB9`, terminated by
`#define IC_ICS_NUM 0xBA`). **Total opcode count: 186** (`0x00`–`0xB9`
inclusive; `IC_ICS_NUM` itself is a count, not an opcode).

`arg count` is the authoritative value from `intermediate_code_table[]` in
`CInit.ZC:26-213` (`IS_0_ARG`=0, `IS_1_ARG`=1, `IS_2_ARG`=2, `IS_V_ARG`=3
"variable"). This is the number of **source** operands (`arg1`/`arg2`); it
does not count `res`, which is separately flagged by the table's
`res_count` column (0 or 1) and is present on almost every arithmetic/data
opcode.

Semantics are one-liners inferred from how each opcode is consumed —
primarily the final-codegen `switch [tmpi->ic_code]` in
`OptPass789A.ZC:103-1219` (the x86 lowering), and, for opcodes never seen
there, from the optimizer passes that eliminate them earlier (marked
"pre-codegen" below — see §3). Opcodes whose real behavior could not be
pinned down from any consuming code are marked `?`.

| # | Name | Args | Semantics |
|---|---|---|---|
| 0x00 | IC_END | 0 | List sentinel / terminator. `cc->coc.coc_head.ic_code = IC_END` (`ParseLib.ZC:113`); the codegen `while (tmpi->ic_code)` loop (`OptPass789A.ZC:49`) stops here. No code emitted. |
| 0x01 | IC_NOP1 | 0 | No-op placeholder inserted by peephole passes; freed and skipped during codegen (`OptFree(tmpi); goto op789A_next;`). |
| 0x02 | IC_END_EXP | 1 | Marks the end of one top-level expression statement (`ICAdd(cc, IC_END_EXP, 0, 0, ICF_RES_NOT_USED)`, `ParseExp.ZC:338`). Pre-codegen only — used by `OptLag`/lookback helpers in `OptLib.ZC` to bound how far backward a peephole optimization may look within the same expression. |
| 0x03 | IC_NOP2 | 0 | Second no-op form (`OptSetNOP2` also applies a stack-delta side effect, `OptLib.ZC:29`); freed like `IC_NOP1` in codegen. |
| 0x04 | IC_LABEL | 0 | Defines a branch target. `ic_data` is a `CCodeMisc*` label; codegen sets `lb->addr = rip` (`OptPass789A.ZC:318-328`). |
| 0x05 | IC_CALL_START | 0 | Marks the start of a call's argument-push region; codegen treats it like `IC_PUSH_REGS` (`ICPushRegs(tmpi, tmpi->ic_data)`). |
| 0x06 | IC_CALL_END | 0 res=1 | Marks the end of a call; pops the saved-register set (`ICPopRegs`) and, if a result is expected, moves `RAX` into `res`. |
| 0x07 | IC_CALL_END2 | 0 | Alternate call-end marker; no code emitted (falls through to the shared `end:` flag-clear). |
| 0x08 | IC_RETURN_VAL | 1 | Move `arg1` into `RAX` as the function's return value (shares the `IC_RAX_SET` case body). |
| 0x09 | IC_RETURN_VAL2 | 0 | No-op (shares the `IC_RAX_GET` case body — value already in `RAX`). |
| 0x0A | IC_IMM_I64 | 0 res=1 | Integer immediate constant, value in `ic_data`. Pre-codegen only: constant-folded/propagated directly into operands by `OptLib.ZC`/`OptPass012.ZC`, never reaches `OptPass789A`. |
| 0x0B | IC_IMM_F64 | 0 res=1 | Float immediate constant (`ic_data(F64)`); same pre-codegen-only treatment as `IC_IMM_I64`. |
| 0x0C | IC_STR_CONST | 0 res=1 | Address of a string/data constant. `ic_data` is a `CCodeMisc*` (`CMT_STR_CONST`); codegen emits a RIP-relative LEA to it (shares case body with `IC_GET_LABEL`). |
| 0x0D | IC_ABS_ADDR | 0 res=1 | Absolute 64-bit address load: `MOV RAX, imm64` (`ic_data`), registered as an AOT relocation (`CAOTAbsAddr`) when compiling ahead-of-time. |
| 0x0E | IC_ADDR_IMPORT | 0 res=1 | Address of an imported symbol; emits `MOV EAX, imm32` + sign-extend (`0xC06348` = `MOVSXD RAX,EAX`) and records an `CAOTImportExport` relocation. |
| 0x0F | IC_HEAP_GLOBAL | 0 res=1 | Address of a heap-allocated global (`ic_data` = `CAOTHeapGlobal*`); emits `MOV RAX, 0` and records a `CAOTHeapGlobalRef` relocation patched later. |
| 0x10 | IC_SIZEOF | 0 res=1 | `sizeof(type)` result. `ic_data` carries the size; created directly by the parser (`ParseExp.ZC:25,35,52,210`) and folded away by `OptPass012`/`OptLib` (`OptFixSizeOf`) before codegen — never reaches `OptPass789A`. |
| 0x11 | IC_TYPE | 0 res=1 | `?` — appears only as a pass-through/leaf case in `OptPass012.ZC:1498`, `OptPass3.ZC:494`, `OptPass4.ZC:216`; no `ICAdd(cc, IC_TYPE, ...)` call site was found anywhere in `zealos/src/`, so its producer and exact payload could not be confirmed from usage. |
| 0x12 | IC_GET_LABEL | 0 res=1 | Address-of-label result; shares codegen case body with `IC_STR_CONST` (RIP-relative LEA to `ic_data`'s `CCodeMisc*`). |
| 0x13 | IC_RBP | 0 res=1 | Leaf node meaning "the value of `RBP`" (base-pointer / local-variable frame base). Pre-codegen only: `OptPass3.ZC:136-138` rewrites it directly to `IC_REG` with `arg1.reg = REG_RBP`. |
| 0x14 | IC_REG | 0 res=1 | Leaf node meaning "value already sitting in a specific machine register" (`arg1.reg`/`res.reg`). Produced by lowering `IC_RBP` and by local-variable register allocation folding `IC_LEA`+`IC_DEREF` (`OptPass4.ZC:242-258`). Pre-codegen only; never reaches `OptPass789A`'s switch (it's consumed as a plain operand by whatever node references it). |
| 0x15 | IC_FS | 0 | Load the FS-segment stack pointer into `RBP`: zero `RAX`, `MOV RBP, FS:[RSP]` (`0x8B4864`). |
| 0x16 | IC_MOV_FS | 0 res=1 | Store `res` at an FS-segment displacement (`0x64` prefix + `ICMov` using `ic_class`'s raw type). |
| 0x17 | IC_GS | 0 | Same as `IC_FS` for the GS segment (`MOV RBP, GS:[RSP]`, `0x8B4865`). |
| 0x18 | IC_MOV_GS | 0 res=1 | Same as `IC_MOV_FS` for the GS segment (`0x65` prefix). |
| 0x19 | IC_LEA | 0 res=1 | Load effective address of `arg1` into `res` (`ICLea`). |
| 0x1A | IC_MOV | 1 res=1 | Move `arg1` → `res` (`ICMov`); the workhorse data-movement opcode. |
| 0x1B | IC_TO_I64 | 1 | Convert `arg1` to `I64` in place (`ICToI64`). |
| 0x1C | IC_TO_F64 | 1 | Convert `arg1` to `F64` in place (`ICToF64`). |
| 0x1D | IC_TO_BOOL | 1 | Convert `arg1` to a 0/1 boolean (`ICToBool`). |
| 0x1E | IC_TOUPPER | 1 | Uppercase-conversion (`ICToUpper`) — HolyC/ZealC's built-in char-uppercasing operator. |
| 0x1F | IC_POSTFIX_TYPECAST | 1 res=1 | Reinterpret `arg1`'s bits as `res`'s type via a plain `ICMov` (postfix-typecast operator, e.g. `x@I32`). |
| 0x20 | IC_ADDR | 1 res=1 | `&x` address-of; shares codegen case body with `IC_MOV` (its address value is already resolved into `arg1` by earlier passes). |
| 0x21 | IC_COM | 1 res=1 | Bitwise complement (`~x`), `ICUnaries(tmpi, SLASH_OP_NOT, ...)`. |
| 0x22 | IC_NOT | 1 res=1 | Logical NOT (`ICNot`). |
| 0x23 | IC_UNARY_MINUS | 1 res=1 | Unary negation; float path (`ICFUnaryMinus`) or integer `NEG` (`ICUnaries(..., SLASH_OP_NEG, ...)`) depending on `res.type.raw_type`. |
| 0x24 | IC_DEREF | 1 res=1 | Pointer dereference (`ICDeref`); `arg1_type_pointed_to` holds the pointee's raw type. |
| 0x25 | IC_DEREF_PP | 1 res=1 | `(*p)++` postfix increment through a pointer (`ICDerefPostIncDec`, `SLASH_OP_INC`). |
| 0x26 | IC_DEREF_MM | 1 res=1 | `(*p)--` postfix decrement through a pointer (`ICDerefPostIncDec`, `SLASH_OP_DEC`). |
| 0x27 | IC__PP | 1 res=1 | Postfix `x++`; integer (`ICPostIncDec`) or float (`ICFPostIncDec`, `CMP_TEMPLATE_INC`) depending on `ICF_USE_INT`. |
| 0x28 | IC__MM | 1 res=1 | Postfix `x--`; same split (`ICPostIncDec`/`ICFPostIncDec`, `CMP_TEMPLATE_DEC`). |
| 0x29 | IC_PP_ | 1 res=1 | Prefix `++x` (`ICPreIncDec`/`ICFPreIncDec`, `CMP_TEMPLATE_INC`). |
| 0x2A | IC_MM_ | 1 res=1 | Prefix `--x` (`ICPreIncDec`/`ICFPreIncDec`, `CMP_TEMPLATE_DEC`). |
| 0x2B | IC_SHL | 2 res=1 | `arg1 << arg2` (`ICShift`, opcode table `0xE0D1E0D3E0C1` = SHL forms of `D1/D3/C1 /4`). |
| 0x2C | IC_SHR | 2 res=1 | `arg1 >> arg2`, split signed (`SAR`, `0xF8...`) vs unsigned (`SHR`, `0xE8...`) via the two opcode-table args to `ICShift`. |
| 0x2D | IC_SHL_CONST | 1 res=1 | `x <<= const`-style fixed shift-left by an immediate (`ICShift` with `arg2` synthesized as `MDF_IMM`). |
| 0x2E | IC_SHR_CONST | 1 res=1 | Fixed shift-right by an immediate, same signed/unsigned split as `IC_SHR`. |
| 0x2F | IC_POWER | 2 res=1 | `arg1 ** arg2` (float power, `ICFPow`). |
| 0x30 | IC_MUL | 2 res=1 | `arg1 * arg2`; integer (`ICMul`) or float (`ICFMul`) via `ICF_USE_INT`. |
| 0x31 | IC_DIV | 2 res=1 | `arg1 / arg2`; integer (`ICDiv`) or float (`ICFDiv`). |
| 0x32 | IC_MOD | 2 res=1 | `arg1 % arg2`; integer (`ICMod`) or float (`ICFMod`). |
| 0x33 | IC_AND | 2 res=1 | Bitwise AND (`ICAddEct` with opcode `0x23` = `AND r,rm`). |
| 0x34 | IC_OR | 2 res=1 | Bitwise OR (`ICAddEct`, opcode `0x0B`). |
| 0x35 | IC_XOR | 2 res=1 | Bitwise XOR (`ICAddEct`, opcode `0x33`). |
| 0x36 | IC_ADD | 2 res=1 | `arg1 + arg2`; integer (`ICAddEct`, opcode `0x03`) or float (`ICFAdd`). |
| 0x37 | IC_SUB | 2 res=1 | `arg1 - arg2`; integer (`ICSub`) or float (`ICFSub`). |
| 0x38 | IC_ADD_CONST | 1 res=1 | `arg1 + immediate` (`ICAddSubEctImm`, `ic_data` = the constant, opcode selector `0x0003`). |
| 0x39 | IC_SUB_CONST | 1 res=1 | `arg1 - immediate` (`ICAddSubEctImm`, selector `0x052B`). |
| 0x3A | IC_EQU_EQU | 2 res=1 | `arg1 == arg2` (`ICComp`, `SETE`/`0x75,0x75`). |
| 0x3B | IC_NOT_EQU | 2 res=1 | `arg1 != arg2` (`ICComp`, `0x74,0x74`). |
| 0x3C | IC_LESS | 2 res=1 | `arg1 < arg2`; integer (`ICComp`) or float (`ICFCmp`, `CMP_TEMPLATE_LESS`). |
| 0x3D | IC_GREATER_EQU | 2 res=1 | `arg1 >= arg2`; integer/float split as above. |
| 0x3E | IC_GREATER | 2 res=1 | `arg1 > arg2`; integer/float split. |
| 0x3F | IC_LESS_EQU | 2 res=1 | `arg1 <= arg2`; integer/float split. |
| 0x40 | IC_PUSH_CMP | 0 res=1 | Marker used by the "50<i<j<=100" chained-comparison expansion (`ICF_PUSH_CMP`/`ICF_POP_CMP`); no code of its own — participates in the shared post-switch float-result conversion logic. |
| 0x41 | IC_AND_AND | 2 res=1 | Short-circuit `&&` (`ICAndAnd`). |
| 0x42 | IC_OR_OR | 2 res=1 | Short-circuit `\|\|` (`ICOrOr`). |
| 0x43 | IC_XOR_XOR | 2 res=1 | Logical XOR of two boolean expressions (`ICXorXor`). |
| 0x44 | IC_ASSIGN | 2 res=1 | `*arg1 = arg2` (`ICAssign`) — the general assignment-through-pointer opcode. |
| 0x45 | IC_ASSIGN_PP | 2 res=1 | Assign-then-postincrement compound form (`ICAssignPostIncDec`, `SLASH_OP_INC`). |
| 0x46 | IC_ASSIGN_MM | 2 res=1 | Assign-then-postdecrement compound form (`ICAssignPostIncDec`, `SLASH_OP_DEC`). |
| 0x47 | IC_SHL_EQU | 2 res=1 | `*arg1 <<= arg2` (`ICShiftEqu`). |
| 0x48 | IC_SHR_EQU | 2 res=1 | `*arg1 >>= arg2` (`ICShiftEqu`, signed/unsigned split). |
| 0x49 | IC_MUL_EQU | 2 res=1 | `*arg1 *= arg2`; integer (`ICMulEqu`) or float (`ICFOpEqu`, `SLASH_OP_FMUL`). |
| 0x4A | IC_DIV_EQU | 2 res=1 | `*arg1 /= arg2`; integer (`ICDivEqu`) or float (`ICFOpEqu`, `SLASH_OP_FDIV`). |
| 0x4B | IC_MOD_EQU | 2 res=1 | `*arg1 %= arg2`; integer (`ICDivEqu` with mod flag) or float (`ICFModEqu`). |
| 0x4C | IC_AND_EQU | 2 res=1 | `*arg1 &= arg2` (`ICAndEqu`). |
| 0x4D | IC_OR_EQU | 2 res=1 | `*arg1 \|= arg2` (`ICOrEqu`). |
| 0x4E | IC_XOR_EQU | 2 res=1 | `*arg1 ^= arg2` (`ICXorEqu`). |
| 0x4F | IC_ADD_EQU | 2 res=1 | `*arg1 += arg2`; integer (`ICAddSubEctEqu`) or float (`ICFOpEqu`, `SLASH_OP_FADD`). |
| 0x50 | IC_SUB_EQU | 2 res=1 | `*arg1 -= arg2`; integer/float split as above (`SLASH_OP_FSUB`). |
| 0x51 | IC_JMP | 0 | Unconditional jump to label `ic_data` (`CCodeMisc*`); codegen picks short (`EB`) vs near (`E9`) form and can self-degrade to `IC_NOP1` if the jump target is itself (zero-offset short jump). |
| 0x52 | IC_SUB_CALL | 0 | Internal near `CALL` to a compiler-generated subroutine label (`ic_data` = `CCodeMisc*`), opcode `0xE8`. |
| 0x53 | IC_SWITCH | 2 | Bounds-checked `switch` dispatch (`ICSwitch(tmpi, rip, FALSE, ...)`). |
| 0x54 | IC_NOBOUND_SWITCH | 2 | Unchecked `switch` dispatch (`ICSwitch(tmpi, rip, TRUE, ...)`) — no default/bounds branch. |
| 0x55 | IC_ADD_RSP | V | Adjust `RSP` by `ic_data` bytes (`ICAddRSP`) — stack cleanup after a call or block exit. |
| 0x56 | IC_ADD_RSP1 | V | Second `RSP`-adjust marker; falls through as a no-op in the final switch (adjustment already folded into a sibling node). |
| 0x57 | IC_ENTER | 0 | Function prologue: `PUSH RBP; MOV RBP,RSP`, optional local-frame `RSP` reservation, register-save (`ICPushRegs`) and register-variable preload from `reg_offsets`. |
| 0x58 | IC_PUSH_REGS | 0 | Push the register mask given in `ic_data` (`ICPushRegs`); shares case body with `IC_CALL_START`. |
| 0x59 | IC_POP_REGS | 0 | Pop the register mask given in `ic_data` (`ICPopRegs`). |
| 0x5A | IC_LEAVE | 0 | Function epilogue: register restore, `LEAVE`/`POP RBP`, extra `RSP` cleanup for interrupt handlers (`IRET`) or `argpop` calling convention (`RET imm16`), else plain `RET`. |
| 0x5B | IC_RET | 0 | Plain `RET` (`0xC3`). |
| 0x5C | IC_CALL | 0 | Direct call to absolute address `ic_data`; near `CALL rel32` if it fits in 32 bits (or always for AOT), else `MOV RBX,imm64; CALL RBX`. |
| 0x5D | IC_CALL_INDIRECT | 0 | Call through a stack-relative pointer at `[RSP+ic_data]` (`CALL disp[RSP]`). |
| 0x5E | IC_CALL_INDIRECT2 | 0 | Call through an absolute pointer loaded into `RBX` (`MOV RBX,imm64; CALL [RBX]`), with AOT relocation. |
| 0x5F | IC_CALL_IMPORT | 0 | Call an imported symbol (`ic_data` = `CHashFun*`); either `MOV RBX,imm64;CALL RBX` (`OPTf_USE_IMM64`) or `CALL rel32`, each registering a `CAOTImportExport` relocation. |
| 0x60 | IC_CALL_EXTERN | 0 | Call an extern symbol in a static module (`CALL rel32` with relocation via `CExternUsage`). |
| 0x61 | IC_ASM | 0 | Inline-asm block: copies pre-assembled bytes from a `CAOTCtrl*` blob (`ic_data`) into the output, fixing up its internal relocations (`CompFixUpAOTAsm`/`CompFixUpJITAsm`). |
| 0x62 | IC_PUSH | 1 | Push `arg1` onto the stack (`ICPush`). |
| 0x63 | IC_POP | 0 | Pop the top of stack into `RAX` (`POP RAX`, `0x58`). |
| 0x64 | IC_CLFLUSH | 1 | `CLFLUSH [RAX]` on address `arg1` (cache-line flush). |
| 0x65 | IC_INVLPG | 1 | `INVLPG [RAX]` on address `arg1` (TLB invalidate). |
| 0x66 | IC_IN_U8 | 1 | `IN AL, port` — port from `arg1` (immediate or `DX`-loaded register). |
| 0x67 | IC_IN_U16 | 1 | `IN AX, port` (0x66 operand-size prefix variant). |
| 0x68 | IC_IN_U32 | 1 | `IN EAX, port`. |
| 0x69 | IC_OUT_U8 | 2 | `OUT port, AL` — port from `arg2`, value from `arg1`. |
| 0x6A | IC_OUT_U16 | 2 | `OUT port, AX`. |
| 0x6B | IC_OUT_U32 | 2 | `OUT port, EAX`. |
| 0x6C | IC_RFLAGS_GET | 0 | `PUSHF; POP RAX` — read flags register into `RAX`. |
| 0x6D | IC_CARRY | 0 | Read the carry flag as a 0/1 integer: `SETB AL; AND EAX,1`. |
| 0x6E | IC_RFLAGS_SET | 1 | `PUSH arg1; POPF` — write `arg1` into the flags register. |
| 0x6F | IC_RAX_GET | 0 res=1 | No-op: value already resides in `RAX` (shares case body with `IC_RETURN_VAL2`). |
| 0x70 | IC_RAX_SET | 1 | Move `arg1` into `RAX` (shares case body with `IC_RETURN_VAL`). |
| 0x71 | IC_RBP_GET | 0 res=1 | `MOV RAX, RBP`. |
| 0x72 | IC_RBP_SET | 1 | `MOV RBP, arg1`. |
| 0x73 | IC_RSP_GET | 0 res=1 | `MOV RAX, RSP`. |
| 0x74 | IC_RSP_SET | 1 | `MOV RSP, arg1`. |
| 0x75 | IC_RIP | 0 res=1 | Load the current instruction pointer: `MOV RAX, imm64` where the immediate is patched to `rip2 + offset` (self-referential absolute address), AOT-relocated. |
| 0x76 | IC_RDTSC | 0 | Expands the `CMP_TEMPLATE_RDTSC` code template (`RDTSC`-based cycle counter read). |
| 0x77 | IC_BT | 2 | Bit test (`BT`), via `ICBitOps` with opcodes `0xA30F`/`0x20BA0F`. |
| 0x78 | IC_BTS | 2 | Bit test-and-set (`BTS`); shares case body with `IC_LBTS` (locked variant — lock prefix comes from `ICF_LOCK`, not a separate opcode path). |
| 0x79 | IC_BTR | 2 | Bit test-and-reset (`BTR`); shares case body with `IC_LBTR`. |
| 0x7A | IC_BTC | 2 | Bit test-and-complement (`BTC`); shares case body with `IC_LBTC`. |
| 0x7B | IC_LBTS | 2 | Locked bit test-and-set — same codegen path as `IC_BTS` (see above). |
| 0x7C | IC_LBTR | 2 | Locked bit test-and-reset — same codegen path as `IC_BTR`. |
| 0x7D | IC_LBTC | 2 | Locked bit test-and-complement — same codegen path as `IC_BTC`. |
| 0x7E | IC_BSF | 1 res=1 | Bit-scan-forward (`BSF`), with a `NOT RAX` correction if the flag says "not found" (`JNE` past a `NOT`). |
| 0x7F | IC_BSR | 1 res=1 | Bit-scan-reverse (`BSR`), same "not found" correction. |
| 0x80 | IC_QUEUE_INIT | 1 | Initialize an intrusive-queue head (`ICQueueInit`). |
| 0x81 | IC_QUEUE_INSERT | 2 | Insert into an intrusive queue (`ICQueueInsert`). |
| 0x82 | IC_QUEUE_INSERT_REV | 2 | Insert into an intrusive queue, reverse order (`ICQueueInsertRev`). |
| 0x83 | IC_QUEUE_REMOVE | 1 | Remove from an intrusive queue (`ICQueueRemove`). |
| 0x84 | IC_STRLEN | 1 res=1 | String length via the `CMP_TEMPLATE_STRLEN` code template. |
| 0x85 | IC_BR_ZERO | 1 | Branch if `arg1 == 0` (`ICTestAndBranch`, `JE`/`0x74840F`). |
| 0x86 | IC_BR_NOT_ZERO | 1 | Branch if `arg1 != 0` (`ICTestAndBranch`, `JNE`/`0x75850F`). |
| 0x87 | IC_BR_CARRY | 0 | Branch if carry flag set (`ICFlagBranch`, `JB`/`0x72820F`). |
| 0x88 | IC_BR_NOT_CARRY | 0 | Branch if carry flag clear (`ICFlagBranch`, `JAE`/`0x73830F`). |
| 0x89 | IC_BR_EQU_EQU | 2 | Compare `arg1==arg2` and branch (`ICCompAndBranch`); float path via `ICFCmpAndBranch` when `ICF_USE_F64`. |
| 0x8A | IC_BR_NOT_EQU | 2 | Compare `arg1!=arg2` and branch. |
| 0x8B | IC_BR_LESS | 2 | Compare `arg1<arg2` and branch; signed/unsigned/float condition codes chosen by `ICCompAndBranch`/`ICFCmpAndBranch`. |
| 0x8C | IC_BR_GREATER_EQU | 2 | Compare `arg1>=arg2` and branch. |
| 0x8D | IC_BR_GREATER | 2 | Compare `arg1>arg2` and branch. |
| 0x8E | IC_BR_LESS_EQU | 2 | Compare `arg1<=arg2` and branch. |
| 0x8F | IC_BR_EQU_EQU2 | 2 res=1 | Chained-comparison ("2" = second link) form of `IC_BR_EQU_EQU`, `TRUE` passed as the "chained" flag to `ICCompAndBranch`. |
| 0x90 | IC_BR_NOT_EQU2 | 2 res=1 | Chained form of `IC_BR_NOT_EQU`. |
| 0x91 | IC_BR_LESS2 | 2 res=1 | Chained form of `IC_BR_LESS`. |
| 0x92 | IC_BR_GREATER_EQU2 | 2 res=1 | Chained form of `IC_BR_GREATER_EQU`. |
| 0x93 | IC_BR_GREATER2 | 2 res=1 | Chained form of `IC_BR_GREATER`. |
| 0x94 | IC_BR_LESS_EQU2 | 2 res=1 | Chained form of `IC_BR_LESS_EQU`. |
| 0x95 | IC_BR_AND_ZERO | 2 | Branch if `arg1 & arg2 == 0` (`ICAndBranch`, `JE`). |
| 0x96 | IC_BR_AND_NOT_ZERO | 2 | Branch if `arg1 & arg2 != 0` (`ICAndBranch`, `JNE`). |
| 0x97 | IC_BR_MM_ZERO | 1 | Predecrement `arg1` then branch if zero (`ICPreIncDec` DEC + `ICFlagBranch` `JE`) — `while(--x)`-style loop test. |
| 0x98 | IC_BR_MM_NOT_ZERO | 1 | Predecrement `arg1` then branch if not zero (same pattern, `JNE`). |
| 0x99 | IC_BR_AND_AND_ZERO | 2 | `?` — appears only in optimizer pass-through lists (`OptPass012.ZC`), no dedicated case found in `OptPass789A.ZC`; likely lowered into a chain of `IC_BR_ZERO`/`IC_AND_AND` nodes before codegen but the exact rewrite site was not located. |
| 0x9A | IC_BR_AND_AND_NOT_ZERO | 2 | `?` — same caveat as `IC_BR_AND_AND_ZERO`. |
| 0x9B | IC_BR_OR_OR_ZERO | 2 | `?` — same caveat (short-circuit `\|\|` branch-fusion opcode, no codegen case found). |
| 0x9C | IC_BR_OR_OR_NOT_ZERO | 2 | `?` — same caveat. |
| 0x9D | IC_BR_BT | 2 | Bit-test-and-branch-if-set (`ICBrBitOps`, `BT`/`0xA30F`, branch `JB`/`0x72820F`). |
| 0x9E | IC_BR_BTS | 2 | Bit-test-and-set, then branch if the tested bit was set (`ICBrBitOps`, `BTS`/`0xAB0F`). |
| 0x9F | IC_BR_BTR | 2 | Bit-test-and-reset, then branch if set (`ICBrBitOps`, `BTR`/`0xB30F`). |
| 0xA0 | IC_BR_BTC | 2 | Bit-test-and-complement, then branch if set (`ICBrBitOps`, `BTC`/`0xBB0F`). |
| 0xA1 | IC_BR_NOT_BT | 2 | Bit test, branch if clear (`ICBrBitOps`, `JAE`/`0x73830F`). |
| 0xA2 | IC_BR_NOT_BTS | 2 | Bit-test-and-set, branch if bit was clear. |
| 0xA3 | IC_BR_NOT_BTR | 2 | Bit-test-and-reset, branch if bit was clear. |
| 0xA4 | IC_BR_NOT_BTC | 2 | Bit-test-and-complement, branch if bit was clear. |
| 0xA5 | IC_SWAP_U8 | 2 | Swap two `U8` operands in place (`ICSwap`). |
| 0xA6 | IC_SWAP_U16 | 2 | Swap two `U16` operands (`ICSwap`; width taken from operand type, same helper as `IC_SWAP_U8`). |
| 0xA7 | IC_SWAP_U32 | 2 | Swap two `U32` operands (`ICSwap`). |
| 0xA8 | IC_SWAP_I64 | 2 | Swap two 64-bit operands (`ICSwap`). Note: `CInit.ZC`'s debug-name field for this entry is `"SWAP_U64"`, inconsistent with the `#define` name `IC_SWAP_I64` — cosmetic only, doesn't affect dispatch (see §5 footnote). |
| 0xA9 | IC_ABS_I64 | 1 res=1 | Integer absolute value: `TEST RAX,RAX; JNS +3; NEG RAX`. |
| 0xAA | IC_SIGN_I64 | 1 res=1 | Sign function via `CMP_TEMPLATE_SIGN_I64` code template. |
| 0xAB | IC_MIN_I64 | 2 res=1 | Signed 64-bit min (`ICMinMax`, `CMOVL`/`0x4F`... condition code `0x4F`=`CMOVG`, selecting the smaller). |
| 0xAC | IC_MAX_I64 | 2 res=1 | Signed 64-bit max (`ICMinMax`, `0x4C`=`CMOVL`). |
| 0xAD | IC_MIN_U64 | 2 res=1 | Unsigned 64-bit min (`ICMinMax`, `0x47`=`CMOVA`). |
| 0xAE | IC_MAX_U64 | 2 res=1 | Unsigned 64-bit max (`ICMinMax`, `0x42`=`CMOVB`). |
| 0xAF | IC_MOD_U64 | 2 res=1 | Unsigned 64-bit modulo (`ICModU64`). |
| 0xB0 | IC_SQR_I64 | 1 res=1 | Signed integer square (`ICSqr`, `SLASH_OP_IMUL`). |
| 0xB1 | IC_SQR_U64 | 1 res=1 | Unsigned integer square (`ICSqr`, `SLASH_OP_MUL`). |
| 0xB2 | IC_SQR | 1 res=1 | Float square, via `CMP_TEMPLATE_SQR` code template (`ICFTemplateFun`). |
| 0xB3 | IC_ABS | 1 res=1 | Float absolute value (`CMP_TEMPLATE_ABS`). |
| 0xB4 | IC_SQRT | 1 res=1 | Float square root (`CMP_TEMPLATE_SQRT`). |
| 0xB5 | IC_SIN | 1 res=1 | Float sine (`CMP_TEMPLATE_SIN`, x87 `FSIN`-based template). |
| 0xB6 | IC_COS | 1 res=1 | Float cosine (`CMP_TEMPLATE_COS`). |
| 0xB7 | IC_TAN | 1 res=1 | Float tangent (`CMP_TEMPLATE_TAN`). |
| 0xB8 | IC_ATAN | 1 res=1 | Float arctangent (`CMP_TEMPLATE_ATAN`). |
| 0xB9 | IC_POPCNT | 1 res=1 | Population count (`POPCNT`, with `F3` prefix). |

**Opcode count: 186** (0x00–0xB9). Of these, **7 never appear in the final
x86 codegen switch** (`IC_END_EXP`, `IC_IMM_I64`, `IC_IMM_F64`, `IC_SIZEOF`,
`IC_TYPE`, `IC_RBP`, `IC_REG`) — they are pure front-end/optimizer forms,
resolved or rewritten during passes 0-6 before pass 7 (see §3). **179**
opcodes have a concrete case in `OptPass789A.ZC`'s dispatcher, several
sharing one case body via C `case A: case B:` fallthrough (e.g.
`IC_ADDR`/`IC_MOV`; `IC_RETURN_VAL`/`IC_RAX_SET`; `IC_BTS`/`IC_LBTS`).

---

## 3. How the x86 backend walks the IC list

Top-level driver (`zealos/src/Compiler/ParseLib.ZC:289-353`,
`COCCompile()`), quoted structurally:

```c
cc->pass = 0;
OptPass012(cc);      // pass 1: constant fold / dead-node elim (twice)
OptPass012(cc);      // pass 2
OptPass3(cc, reg_offsets);   // pass 3: tree-link type inference, RBP->REG lowering
OptPass4(cc, reg_offsets, _type);  // pass 4: register allocation
OptPass5(cc);         // pass 5
OptPass6(cc);         // pass 6: peephole cleanup
OptPass789A(cc, reg_offsets, NULL, NULL);  // pass 7: size estimate
OptPass789A(cc, reg_offsets, NULL, NULL);  // pass 8: size estimate refine
code_size = OptPass789A(cc, reg_offsets, NULL, NULL);  // pass 9 (looped until stable)
...
code_size = OptPass789A(cc, reg_offsets, res, _debug); // pass 10: final, buf != NULL
```

`OptPass789A(cc, reg_offsets, buf, _debug)` (`OptPass789A.ZC:1`) is the
single entry point that does the actual IC → x86 translation. It walks
`tmpi = cc->coc.coc_head.next` forward via `tmpi->next` in a
`while (tmpi->ic_code) { ... tmpi = tmpi_next; }` loop (`OptPass789A.ZC:45-1275`),
dispatching on `switch [tmpi->ic_code]` (ZealC's `switch [x]` = jump-table
switch, `OptPass789A.ZC:103`). Each case calls a small `IC*`-prefixed
helper (`ICMov`, `ICAddEct`, `ICComp`, `ICShift`, `ICSwitch`, …, mostly in
`BackA.ZC`/`BackB.ZC`/`BackC.ZC`/`BackLib.ZC`) that appends raw x86 bytes to
`tmpi->ic_body` via `ICU8`/`ICU16`/`ICU24`/`ICU32`/`ICU64`. Passes 7-9 run
with `buf == NULL` purely to compute `ic_count` (instruction length) and
stabilize branch-offset sizing (short vs near jumps); pass 10 supplies a
real output buffer and `MemCopy`s each node's `ic_body` into it
(`OptPass789A.ZC:1250-1251`). A `default:` case throws a compiler exception
for any `ic_code` the dispatcher doesn't recognize (line 1215-1218) — i.e.
an unhandled opcode is a hard compile error, not a silent skip.

---

## 4. Register / type notation

**Raw types** — `RT_*`, `zealos/src/Kernel/KernelA.HH:1708-1723`:

```c
#define RT_I0 			2
#define RT_U0 			3
#define RT_I8 			4
#define RT_U8 			5
#define RT_I16			6
#define RT_U16			7
#define RT_I32			8
#define RT_U32			9
#define RT_I64			10
#define RT_PTR			10 //Signed to allow negative error codes.
#define RT_U64			11
#define RT_F32			12 //Not implemented
#define RT_UF32 		13 //Not implemented, Fictitious
#define RT_F64			14
#define RT_UF64 		15 //Fictitious
#define RT_RTS_NUM		16
#define RTF_UNSIGNED	1
#define RTG_MASK		0xFF
```
Stored in `CICType.raw_type` (a `CICArg`'s or `res`'s scalar type). Note
`RT_PTR == RT_I64` (pointers are just signed 64-bit values).

**Addressing mode** — `CICType.mode`, values `MDf_*`/`MDF_*`,
`KernelA.HH:1727-1744`:

```c
#define MDf_STACK 		8
#define MDf_IMM 		9
#define MDf_REG 		10
#define MDf_DISP		11
#define MDf_SIB 		12
#define MDf_RIP_DISP32	13

#define MDF_NULL				0x0000
#define MDF_STACK 				0x0100
#define MDF_IMM 				0x0200
#define MDF_REG 				0x0400
#define MDF_DISP				0x0800
#define MDF_SIB 				0x1000
#define MDF_RIP_DISP32			0x2000
#define MDG_MASK				0xFF00
```
`mode == MDF_NULL` (0) means the operand slot is unused (`res.type.mode`
checked this way throughout `OptPass789A.ZC`, e.g. line 1220). `mode |
raw_type` is stored packed as one `I64`/`U16` value in call sites like
`MDF_REG + RT_I64` (seen constantly, e.g. `ICMov(tmpi, MDF_REG + RT_I64,
REG_RAX, 0, ...)`), i.e. mode and raw type are additive bit-disjoint fields
of the same word.

**Registers** — `REG_*`, `KernelA.HH:1931-1946`:

```c
#define REG_RAX 		0
#define REG_RCX 		1
#define REG_RDX 		2
#define REG_RBX 		3
#define REG_RSP 		4
#define REG_RBP 		5
#define REG_RSI 		6
#define REG_RDI 		7
#define REG_R8			8
#define REG_REGS_NUM	16

#define REG_RIP 		16			//Used by compiler, not really it's num
#define REG_NONE		32			//noreg flag sets it to this
#define REG_ALLOC 		33			//reg flag sets it to this
#define REG_UNDEF 		I8_MIN
```
(`R9`–`R15` follow `REG_R8` implicitly at 9-15, matching x86-64 register
numbering; not individually `#define`d here but used as `REG_R8`+n in
`BackLib.ZC`.) Stored in `CICArg.reg`, low byte = base register, high byte
= `index_reg + scale << 6` for SIB addressing. Register group masks used
for prologue/epilogue save-sets: `REGG_CLOBBERED`, `REGG_SAVED`,
`REGG_STACK_TMP`, `REGG_LOCAL_VARS`, `REGG_LOCAL_NON_PTR_VARS`
(`KernelA.HH:1948-1952`).

**IC-level flags** — `ICF_*`, `KernelA.HH:1746-1776` (selected, full list in
file): `ICF_RES_TO_F64`/`ICF_RES_TO_INT`/`ICF_ARG1_TO_F64`/`ICF_ARG1_TO_INT`/
`ICF_ARG2_TO_F64`/`ICF_ARG2_TO_INT` drive automatic int↔float conversion of
operands before/after the opcode's own codegen runs (applied generically at
the top and bottom of the `OptPass789A` dispatch loop, not opcode-specific);
`ICF_USE_INT`/`ICF_USE_F64`/`ICF_USE_UNSIGNED` select which of an opcode's
integer/float/signedness code paths to take (seen throughout §2's "split"
entries); `ICF_SHORT_JMP`, `ICF_LOCK`, `ICF_PASS_TRACE`, `ICF_DEL_PREV_INS`
control codegen mechanics rather than operand semantics.

---

## 5. Observed inconsistencies (flagged, not resolved)

- `intermediate_code_table[]`'s debug `name` string field (in `CInit.ZC`)
  does not always match the corresponding `#define IC_*` name at the same
  table index: index `0xAC` (`IC_MAX_I64`) is labeled `"U64_MIN"` and index
  `0xAD` (`IC_MIN_U64`) is labeled `"I64_MAX"` — the two labels appear
  transposed. Index `0xA8` (`IC_SWAP_I64`) is labeled `"SWAP_U64"`. These
  are cosmetic (only used for pass-trace pretty-printing via `ICPut`), not
  used for dispatch, but a new backend author reading `CInit.ZC` in
  isolation should trust the `#define` names in `CompilerA.HH`, not the
  table's `name` strings.
- `IC_TYPE` (0x11) has no discoverable producer in `zealos/src/` — flagged
  `?` in §2 rather than guessed.
- `IC_BR_AND_AND_ZERO`/`IC_BR_AND_AND_NOT_ZERO`/`IC_BR_OR_OR_ZERO`/
  `IC_BR_OR_OR_NOT_ZERO` (0x99-0x9C) have no case in the final
  `OptPass789A.ZC` switch, unlike their siblings `IC_BR_AND_ZERO`/
  `IC_BR_AND_NOT_ZERO` which do — flagged `?` in §2.
