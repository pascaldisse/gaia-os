# HolyC Compiler Intermediate Code (IC) — Backend Contract

Extraction/documentation reference for the TempleOS/Shrine HolyC compiler's
intermediate representation, as carried in `shrine/Compiler/`. Purpose: give a
new backend author the exact data it must consume — the IC node struct, every
`IC_*` opcode, how the existing x86-64 backend walks the list, and the
type/register notation used inside operands.

Sources read: `shrine/Compiler/CompilerA.HH`, `CompilerB.HH`,
`shrine/Kernel/KernelA.HH` (the struct actually lives here, not in
CompilerA/B.HH — see note below), `CInit.HC`, `PrsLib.HC`, `PrsExp.HC`,
`OptPass012.HC`, `OptPass3.HC`, `OptPass4.HC`, `OptPass5.HC`, `OptPass6.HC`,
`OptPass789A.HC`, `OptLib.HC`, `BackA.HC`, `BackB.HC`, `BackC.HC`, `BackLib.HC`.

---

## 1. The `CIntermediateCode` struct

**Correction to source locations:** `CompilerA.HH` and `CompilerB.HH` contain
the `IC_*` opcode `#define`s and a *metadata* struct called
`CIntermediateStruct` (one static row per opcode — arity/result-count/name).
The actual **per-instruction node struct that the IC linked list is built
from** is `CIntermediateCode`, declared in `shrine/Kernel/KernelA.HH:1657-1687`
(it is forward-declared `extern class CIntermediateCode;` at
`KernelA.HH:15`, and referenced by `$LK,"CIntermediateCode",...$` doc-links
from the compiler sources). Both structs are quoted below since a backend
needs both: `CIntermediateCode` for the node contract, `CIntermediateStruct`
for the per-opcode arity/name table.

### 1.1 `CIntermediateCode` (KernelA.HH:1657-1687) — the real IC node

```c
class CIntermediateCodeBase
{
  CIntermediateCode *next,*last;
  U16   ic_code,
        ic_precedence;
  I16   ic_cnt,
        ic_last_start;
};

class CIntermediateCode:CIntermediateCodeBase
{
  I64   ic_flags,
        ic_data,
        ic_line;
  CHashClass *ic_class,*ic_class2;
  CICArg arg1,arg2,res;
  U8    arg1_type_pointed_to; //Used for IST_ASSIGN and IST_DEREF ic_codes.
  union {
    U8 ic_body[IC_BODY_SIZE];
    //Tree Links are created in OptPass012.  An ADD opcode, for example,
    //points back to its two earlier arg CIntermediateCode's.
    //Tree links get destroyed during Pass789A when they get overwritten
    //by machine code.  (Saves room to union the output machine code buffer
    //with these links, since they are not needed after pass4.)
    //Tree links are used during passes 012 and 3 for determining types.
    CICTreeLinks t;
  };
#assert !($$&7)
};
```

`CIntermediateCode` is allocated in `PrsLib.HC:ICAdd()` (`MAlloc(sizeof(CIntermediateCode))`)
and appended to a **circular doubly-linked list** whose sentinel head is
`cc->coc.coc_head` (`ic_code` of the sentinel is set to `IC_END` in
`PrsLib.HC:COCInit()`, `QueInit()`-initialized). `next`/`last` are the queue
links; the list is walked head-to-tail during every optimizer/codegen pass.

**Field-by-field annotation (from real usage across the sources):**

| Field | Type | One-line meaning (grounded in usage) |
|---|---|---|
| `next`, `last` | `CIntermediateCode*` | Circular doubly-linked queue pointers; list sentinel is `cc->coc.coc_head` (`COCInit`, `QueIns`/`QueDel`, walked in every `OptPass*`/`OptPass789A`). |
| `ic_code` | `U16` | The opcode (`IC_*` value below). `0` (`IC_END`) is the sentinel/terminator the walk loops test (`while(tmpi->ic_code)`). |
| `ic_precedence` | `U16` | Operator precedence (`PREC_*`) captured at node-creation time from the binary-op table (`PrsLib.HC:ICAdd`, set from `opcode_and_precedence.u16[1]`); used by `OptPass012` (e.g. `PREC_ASSIGN` check at `OptPass012.HC:1260`) to reason about chained-assignment/operator folding. |
| `ic_cnt` | `I16` | Running byte length of machine code emitted **for this node** into `ic_body` during `OptPass789A` (`ICU8/16/24/32/64` in `BackLib.HC` all do `ic_body[ic_cnt++]=...`); reset to 0 at the top of each codegen pass. |
| `ic_last_start` | `I16` | Byte offset within this node's own emitted code where the *last* sub-instruction started; used by the lag/peephole merger (`OptLag1`, `BackLib.HC`) to detect and collapse adjacent redundant instructions across passes 7-9. `-1` means "nothing merged yet". |
| `ic_flags` | `I64` | Bitmask of `ICF_*` flags (int/float dispatch, byval, short-jmp, lock prefix, pass-trace, etc. — see `KernelA.HH:1602-1632`); read constantly by both optimizer and backend to pick codegen paths (e.g. `ICF_USE_INT` chooses int vs. float codegen for `IC_MUL`/`IC_ADD`/etc.). |
| `ic_data` | `I64` | Opcode-specific payload (overloaded per opcode): immediate value for `IC_IMM_I64`, `CCodeMisc*` label pointer for `IC_LABEL`/`IC_JMP`/branches, register-save bitmask for `IC_PUSH_REGS`/`POP_REGS`, byte count for `IC_ADD_RSP`, `CHashGlblVar*`/`CHashFun*` for import/call nodes, etc. |
| `ic_line` | `I64` | Source line number, set from `cc->last_line_num` at node creation (`PrsLib.HC:ICAdd`); consumed in `OptPass789A.HC:1075` to build the `CDbgInfo` line→rip table. |
| `ic_class` | `CHashClass*` | The HolyC result type/class of this node's value (drives `raw_type` (`RT_*`) dispatch throughout the backend, e.g. `tmpi->ic_class->raw_type&RTF_UNSIGNED` in `BackA.HC`); set during `OptPass012` type propagation. |
| `ic_class2` | `CHashClass*` | Secondary class context, copied from the tree-link field `t.class2` in `OptPass4.HC:351` once tree links are no longer needed for `arg1_tree`/`arg2_tree` walking; used e.g. by `ICAssignPostIncDec` (`BackB.HC:431`) for the pointee class of a post-inc/dec through a pointer. |
| `arg1`, `arg2` | `CICArg` | The instruction's (up to two) source operands — addressing mode, register/index, displacement/immediate. Arity (0/1/2/variable) per opcode is defined by `intermediate_code_table[ic_code].arg_cnt`. |
| `res` | `CICArg` | The instruction's result/destination operand (only meaningful when `intermediate_code_table[ic_code].res_cnt==1`); backend code generators write here as the final step of most opcodes (`ICMov(tmpi,tmpi->res.type,tmpi->res.reg,tmpi->res.disp,...)`). |
| `arg1_type_pointed_to` | `U8` | Raw type (`RT_*`) of the value **pointed to** by `arg1`, used when the opcode is classified `IST_ASSIGN` or `IST_DEREF` in the metadata table (compound assignment through a pointer, e.g. `IC_ADD_EQU`, `IC_MUL_EQU`, `IC_SHL_EQU`) — `arg1` itself just encodes the pointer's own addressing mode, not what it points to. |
| `ic_body[IC_BODY_SIZE]` | `U8[0x83]` | Union member: the raw emitted x86-64 machine-code bytes for this node, written by `ICU8/ICU16/ICU24/ICU32/ICU64` (`BackLib.HC`) during `OptPass789A`, then `MemCpy`'d into the final output buffer. `IC_BODY_SIZE` = `0x83` (131) bytes — the assert `#assert !($$&7)` enforces the whole struct stays 8-byte aligned. |
| `t` (`CICTreeLinks`) | union w/ `ic_body` | Parse-time expression-tree back-pointers, valid only during passes 0-3 (destroyed/overwritten by machine code from pass 4 on — see fields below). |

### 1.2 Supporting structs referenced by the fields above (KernelA.HH)

```c
class CICType
{
  U8    raw_type,mode;
};

class CICArg
{
  CICType type;
  U16   reg; //low is reg, high is index_reg+scale<<6
  I64   disp;
};

class CICTreeLinks
{
  CHashClass *arg1_class,*arg2_class;
  CIntermediateCode *arg1_tree,*arg2_tree;
  CHashClass *class2;
};
```

- `CICType.raw_type` — an `RT_*` value (see §4), optionally combined with
  `RTF_UNSIGNED`.
- `CICType.mode` — an `MDF_*`/`MDG_*` addressing-mode flag combination (STK /
  IMM / REG / DISP / SIB / RIP_DISP32 — see §4); `CICArg.type` is tested with
  plain bitwise ops throughout the backend (e.g. `t2&MDG_REG_DISP_SIB_RIP`),
  confirming `CICType` is treated as a packed `U16`.
- `CICArg.reg` — for register operands, the low byte is the register number
  (`REG_RAX` etc.); for SIB/indexed addressing the high byte packs
  `index_reg + scale<<6` (comment quoted verbatim above).
- `CICArg.disp` — displacement for `MDF_DISP`/`MDF_SIB`/`MDF_RIP_DISP32`
  modes, or the literal immediate value for `MDF_IMM`.
- `CICTreeLinks.arg1_tree`/`arg2_tree` — back-pointers to the
  `CIntermediateCode` node that *produced* each operand, letting `OptPass012`
  walk a lazily-reconstructed expression tree over the otherwise-flat
  instruction list (used for constant folding and type derivation).
- `CICTreeLinks.arg1_class`/`arg2_class`/`class2` — the resolved HolyC class
  of each operand's producing subexpression, consumed by `OptPass3`/`OptPass4`
  (e.g. `OptPass3.HC:75,85`, `OptPass4.HC:351`).

### 1.3 `CIntermediateStruct` — per-opcode metadata row (CompilerA.HH:13-18)

```c
class CIntermediateStruct
{
  U8 arg_cnt,res_cnt,type;
  Bool fpop,not_const,pad[3];
  U8 *name;
};
```

Populated as the 185-entry array `intermediate_code_table[IC_ICS_NUM]` in
`CInit.HC:16-202` (one row per `IC_*` opcode, in opcode-value order). Fields:

| Field | Meaning |
|---|---|
| `arg_cnt` | `IS_0_ARG`(0) / `IS_1_ARG`(1) / `IS_2_ARG`(2) / `IS_V_ARG`(3, variable) — arity, used as the "arg count" column in §2. |
| `res_cnt` | 0 or 1 — whether the opcode writes a result into `res`. |
| `type` | `IST_NULL`(0) / `IST_DEREF`(1) / `IST_ASSIGN`(2) / `IST_CMP`(3) — parser-level syntactic class, e.g. `IST_CMP` drives the chained-comparison logic (`a<b<c`) in `PrsExp.HC:225`, `IST_DEREF`/`IST_ASSIGN` drive `arg1_type_pointed_to` usage and exception-context printing (`CExcept.HC:196`). |
| `fpop` | Whether the opcode pops the x87 float stack (float ops: MUL/DIV/ADD/SUB/SQR/ABS/SQRT/SIN/COS/TAN/ATAN). |
| `not_const` | Whether the opcode's result is **not** eligible for constant folding (side-effecting/impure: address-of, string consts, register/flag ops, calls, assignment forms, etc.). |
| `pad[3]` | Padding to align `name`. |
| `name` | Opcode's bare name (without the `IC_` prefix), used by `ICPut`/trace printing. |

`intermediate_code_table[tmpi->ic_code].arg_cnt`/`.type` are read live by the
parser and optimizer (e.g. `OptPass5.HC:17` uses `arg_cnt==IS_V_ARG` to know
an opcode's abstract-stack effect; `PrsExp.HC:155,201,225` use `.type` to
drive deref-chaining, assignment-chaining and comparison-chaining parse
logic).

---

## 2. IC opcode table (185 opcodes, `IC_END`=0x00 .. `IC_ATAN`=0xB8)

Value/name pairs are the verbatim `#define`s from `CompilerA.HH:20-236`.
"Args" is the `arg_cnt` category from `intermediate_code_table` in `CInit.HC`
(`0`/`1`/`2`/`V`=variable); semantics are grounded in the dispatch switch in
`OptPass789A.HC` (the x86-64 code generator) and the `IC*` helper functions in
`BackA/B/C/BackLib/BackFA/BackFB.HC`, cross-checked against `OptPass012/3/4/5/6.HC`
for opcodes that are folded/rewritten away before reaching codegen. `(res)`
marks opcodes with `res_cnt==1` (they write `tmpi->res`).

| Value | Name | Args | Semantics |
|---|---|---|---|
| 0x00 | `IC_END` | 0 | List-terminator sentinel (`coc_head.ic_code`); loop guards (`while(tmpi->ic_code)`) stop here. Emits nothing. |
| 0x01 | `IC_NOP1` | 0 | No-op placeholder left where an opcode was eliminated (constant-folded/dead); `OptFree`'d when reached in `OptPass789A`. |
| 0x02 | `IC_END_EXP` | 1 | Marks end of a top-level expression statement, discarding its leftover value. Not dispatched in `OptPass789A`'s switch — resolved/consumed earlier by the statement-level parser (`PrsStmt.HC`). |
| 0x03 | `IC_NOP2` | 0 | Second no-op/placeholder variant, treated identically to `IC_NOP1` by the peephole passes (`OptLib.HC:58,79`). No construction site found in this codebase — exact distinction from `IC_NOP1` is **`?`**. |
| 0x04 | `IC_LABEL` | 0 | Defines a jump-target label at the current `rip` (`lb->addr=rip`); handles `CMF_POP_CMP` stack fixup and ASM-label rip offsetting. |
| 0x05 | `IC_CALL_START` | 0 | Marks the start of call-argument evaluation; codegen identical to `IC_PUSH_REGS` (saves caller-clobbered regs, `ic_data`=mask). |
| 0x06 | `IC_CALL_END` | 0 (res) | Marks the end of a call: restores saved regs (`ICPopRegs`), moves `RAX`→`res` if a result is expected. |
| 0x07 | `IC_CALL_END2` | 0 | Variant of `CALL_END` used when the call's boolean result was fused straight into a following branch (`IC_BT`/`IC_CARRY` fusion, `OptLib.HC:287`); no-op at codegen time. |
| 0x08 | `IC_RETURN_VAL` | 1 | Moves `arg1` into `RAX` as the function's return value (same path as `IC_SET_RAX`). |
| 0x09 | `IC_RETURN_VAL2` | 0 | Marks that the return value is already in `RAX`; emits nothing (same path as `IC_GET_RAX`). |
| 0x0A | `IC_IMM_I64` | 0 (res) | Signed 64-bit integer literal (`ic_data`). Constant-folded directly into consuming nodes' `MDF_IMM` operand during `OptPass012`; normally does not survive as a standalone node to `OptPass789A`. |
| 0x0B | `IC_IMM_F64` | 0 (res) | 64-bit float literal (`ic_data` reinterpreted `F64`); same fold-away treatment as `IC_IMM_I64`. |
| 0x0C | `IC_STR_CONST` | 0 (res) | Reference to a string-literal constant (`ic_data`→`CCodeMisc`, `CMT_STR_CONST`); address materialized via RIP-relative `LEA`. |
| 0x0D | `IC_ABS_ADDR` | 0 (res) | Loads an absolute 64-bit address (`mov reg,imm64`) into `res`; AOT-relocatable (`CAOTAbsAddr`). |
| 0x0E | `IC_ADDR_IMPORT` | 0 (res) | Loads the address of an imported/external symbol (`mov reg,imm32`, sign-extended); patched via `CAOTImportExport`. |
| 0x0F | `IC_HEAP_GLBL` | 0 (res) | Loads the address of a heap-resident global variable; patched via `CAOTHeapGlblRef` once heap layout is final. |
| 0x10 | `IC_SIZEOF` | 0 (res) | Compile-time `sizeof(type)` constant. Not dispatched in `OptPass789A` — resolved to `IC_IMM_I64` earlier. |
| 0x11 | `IC_TYPE` | 0 (res) | Compile-time type/class token. Not dispatched in `OptPass789A` — resolved to an immediate earlier. |
| 0x12 | `IC_GET_LABEL` | 0 (res) | Loads the runtime address of a label into `res` via RIP-relative `LEA` (shares the `IC_STR_CONST` codegen path). |
| 0x13 | `IC_RBP` | 0 (res) | Placeholder for "value of an RBP-relative local"; resolved into a concrete `MDF_DISP+RBP` `CICArg` during `OptPass3` register/stack allocation — does not reach `OptPass789A` as its own opcode. |
| 0x14 | `IC_REG` | 0 (res) | Placeholder for "value of a register-allocated local"; resolved into a concrete `MDF_REG` `CICArg` during `OptPass3`/`OptPass4`. |
| 0x15 | `IC_FS` | 0 | Reads the FS segment base/value into `res`. |
| 0x16 | `IC_MOV_FS` | 0 (res) | Loads `res` from `FS:[ic_data]` (segment-relative memory), typed per `ic_class`. |
| 0x17 | `IC_GS` | 0 | Reads the GS segment base/value into `res`. |
| 0x18 | `IC_MOV_GS` | 0 (res) | Loads `res` from `GS:[ic_data]`, typed per `ic_class`. |
| 0x19 | `IC_LEA` | 0 (res) | Computes the effective address of `arg1` (no dereference) into `res` (`ICLea`). |
| 0x1A | `IC_MOV` | 0 (res) | Generic move of `arg1` into `res` (`ICMov`); the workhorse data-movement opcode, also the codegen path for `IC_ADDR` and `IC_HOLYC_TYPECAST`. |
| 0x1B | `IC_TO_I64` | 1 | In-place `F64`→`I64` conversion of `arg1` (`ICToI64`). |
| 0x1C | `IC_TO_F64` | 1 | In-place `I64`→`F64` conversion of `arg1` (`ICToF64`). |
| 0x1D | `IC_TO_BOOL` | 1 | Normalizes `arg1` to a 0/1 boolean (`ICToBool`). |
| 0x1E | `IC_TOUPPER` | 1 | Converts an ASCII char value to uppercase in place (`ICToUpper`). |
| 0x1F | `IC_HOLYC_TYPECAST` | 1 (res) | HolyC typecast: bit-reinterprets `arg1` into `res` via plain `ICMov` (no numeric conversion). |
| 0x20 | `IC_ADDR` | 1 (res) | Address-of (`&x`); dispatched through the same `ICMov` path as `IC_MOV`. |
| 0x21 | `IC_COM` | 1 (res) | Bitwise complement (`~x`) (`ICUnaries`, `SLASH_OP_NOT`). |
| 0x22 | `IC_NOT` | 1 (res) | Logical NOT (`!x`) (`ICNot`). |
| 0x23 | `IC_UNARY_MINUS` | 1 (res) | Arithmetic negation (`-x`); integer `NEG` or float path per `res` type. |
| 0x24 | `IC_DEREF` | 1 (res) | Pointer dereference (`*p`): loads the pointee into `res` (`ICDeref`). |
| 0x25 | `IC_DEREF_PP` | 1 (res) | Post-increment through a dereferenced pointer (`(*p)++`) (`ICDerefPostIncDec`, `SLASH_OP_INC`). |
| 0x26 | `IC_DEREF_MM` | 1 (res) | Post-decrement through a dereferenced pointer (`(*p)--`). |
| 0x27 | `IC__PP` | 1 (res) | Post-increment of a plain lvalue (`x++`): reads old value then increments (`ICPostIncDec`/`ICFPostIncDec`). |
| 0x28 | `IC__MM` | 1 (res) | Post-decrement of a plain lvalue (`x--`). |
| 0x29 | `IC_PP_` | 1 (res) | Pre-increment (`++x`): increments then yields new value (`ICPreIncDec`/`ICFPreIncDec`). |
| 0x2A | `IC_MM_` | 1 (res) | Pre-decrement (`--x`). |
| 0x2B | `IC_SHL` | 2 (res) | Shift-left: `res=arg1<<arg2` (`ICShift`). |
| 0x2C | `IC_SHR` | 2 (res) | Shift-right (signed/unsigned per class/`ICF_USE_UNSIGNED`). |
| 0x2D | `IC_SHL_CONST` | 1 (res) | Shift-left by a compile-time constant (`ic_data`) — folded form of `IC_SHL`. |
| 0x2E | `IC_SHR_CONST` | 1 (res) | Shift-right by a compile-time constant. |
| 0x2F | `IC_POWER` | 2 (res) | Exponentiation (HolyC `` a`b ``); float path (`ICFPow`). |
| 0x30 | `IC_MUL` | 2 (res) | Multiplication (`ICMul` int / `ICFMul` float, per `ICF_USE_INT`). |
| 0x31 | `IC_DIV` | 2 (res) | Division (`ICDiv`/`ICFDiv`). |
| 0x32 | `IC_MOD` | 2 (res) | Signed modulo (remainder from `IDIV`) (`ICMod`). |
| 0x33 | `IC_AND` | 2 (res) | Bitwise AND. |
| 0x34 | `IC_OR` | 2 (res) | Bitwise OR. |
| 0x35 | `IC_XOR` | 2 (res) | Bitwise XOR. |
| 0x36 | `IC_ADD` | 2 (res) | Addition (`ICAddEct` int / `ICFAdd` float). |
| 0x37 | `IC_SUB` | 2 (res) | Subtraction (`ICSub`/`ICFSub`). |
| 0x38 | `IC_ADD_CONST` | 1 (res) | Addition by a compile-time constant (`ic_data`); also an LEA-combining target in the pass 4/6 peepholes. |
| 0x39 | `IC_SUB_CONST` | 1 (res) | Subtraction by a compile-time constant. |
| 0x3A | `IC_EQU_EQU` | 2 (res) | Equality compare (`==`); sets `res` to 0/1 (`ICCmp`) unless fused into a `BR_*` by `OptBrZero`/`OptBrNotZero`. |
| 0x3B | `IC_NOT_EQU` | 2 (res) | Inequality compare (`!=`). |
| 0x3C | `IC_LESS` | 2 (res) | Less-than (`<`); int (`ICCmp`) or float (`ICFCmp`). |
| 0x3D | `IC_GREATER_EQU` | 2 (res) | Greater-or-equal (`>=`). |
| 0x3E | `IC_GREATER` | 2 (res) | Greater-than (`>`). |
| 0x3F | `IC_LESS_EQU` | 2 (res) | Less-or-equal (`<=`). |
| 0x40 | `IC_PUSH_CMP` | 0 (res) | Saves the previous comparison's boolean on an internal chain-slot for 3-arg chained comparisons (`a<b<c`); consumed and turned into `IC_NOP1` once the chain is fused by `OptBrZero` in `OptPass012` pass 2 (`OptLib.HC:307-311`). No-op if it survives to codegen. |
| 0x41 | `IC_AND_AND` | 2 (res) | Logical AND (`&&`) as a plain 0/1 value (`ICAndAnd`); rewritten to `IC_BR_AND_AND_ZERO`+split branches by `OptBrZero` when used as a branch condition. |
| 0x42 | `IC_OR_OR` | 2 (res) | Logical OR (`\|\|`) (`ICOrOr`), or split into `IC_BR_OR_OR_ZERO` when used as a condition. |
| 0x43 | `IC_XOR_XOR` | 2 (res) | Logical XOR (`^^`) of two boolean-normalized operands (`ICXorXor`). |
| 0x44 | `IC_ASSIGN` | 2 (res) | Plain assignment (`ICAssign`); `res` mirrors the stored value. |
| 0x45 | `IC_ASSIGN_PP` | 2 (res) | Assign-then-post-increment compound (`ICAssignPostIncDec`, `SLASH_OP_INC`). |
| 0x46 | `IC_ASSIGN_MM` | 2 (res) | Assign-then-post-decrement compound. |
| 0x47 | `IC_SHL_EQU` | 2 (res) | Compound `<<=` through a pointer/lvalue (`ICShiftEqu`). |
| 0x48 | `IC_SHR_EQU` | 2 (res) | Compound `>>=`. |
| 0x49 | `IC_MUL_EQU` | 2 (res) | Compound `*=` (`ICMulEqu`/`ICFOpEqu`). |
| 0x4A | `IC_DIV_EQU` | 2 (res) | Compound `/=` (`ICDivEqu`/`ICFOpEqu`). |
| 0x4B | `IC_MOD_EQU` | 2 (res) | Compound `%=` (`ICDivEqu` with `is_mod=TRUE` / `ICFModEqu`). |
| 0x4C | `IC_AND_EQU` | 2 (res) | Compound `&=` (`ICAndEqu`). |
| 0x4D | `IC_OR_EQU` | 2 (res) | Compound `\|=` (`ICOrEqu`). |
| 0x4E | `IC_XOR_EQU` | 2 (res) | Compound `^=` (`ICXorEqu`). |
| 0x4F | `IC_ADD_EQU` | 2 (res) | Compound `+=` (`ICAddSubEctEqu`/`ICFOpEqu`). |
| 0x50 | `IC_SUB_EQU` | 2 (res) | Compound `-=`. |
| 0x51 | `IC_JMP` | 0 | Unconditional jump to label `ic_data`; short (`EB`) vs. near (`E9`) encoding chosen by measured displacement, re-iterated across passes 7-9 until stable. |
| 0x52 | `IC_SUB_CALL` | 0 | Direct near `CALL` to a compiler-internal label (`ic_data`) — e.g. shared float-template code — distinct from a HolyC-level `IC_CALL`. |
| 0x53 | `IC_SWITCH` | 2 | Bounds-checked jump-table switch dispatch (`ICSwitch`, bound-checked). |
| 0x54 | `IC_NOBOUND_SWITCH` | 2 | Jump-table switch dispatch without a bounds check. |
| 0x55 | `IC_ADD_RSP` | V | Unconditional `RSP` adjustment by `ic_data` bytes (frame/stack cleanup), peephole-merges with adjacent adjustments (`ICAddRSP`). |
| 0x56 | `IC_ADD_RSP1` | V | Caller-side conditional stack-argument cleanup after a call (paired with `IC_ADD_RSP` per the callee's `ARGPOP` convention, `PrsExp.HC:575-582`); a no-op placeholder if still present by `OptPass789A`. |
| 0x57 | `IC_ENTER` | 0 | Function prologue: `push rbp; mov rbp,rsp`, reserves local-frame space, saves callee-used registers, materializes register-resident args from `reg_offsets`. |
| 0x58 | `IC_PUSH_REGS` | 0 | Pushes/saves the register set in `ic_data` (bitmask) (`ICPushRegs`); shared by `IC_CALL_START` and function-entry saves. |
| 0x59 | `IC_POP_REGS` | 0 | Restores the register set in `ic_data`; paired with `IC_PUSH_REGS`/`IC_CALL_END`. |
| 0x5A | `IC_LEAVE` | 0 | Function epilogue: restore callee-saved regs, pop `rbp` (or `LEAVE`), `RET`/`IRETQ`/`RET imm16` per `Ff_INTERRUPT`/`Ff_ARGPOP`/`Ff_HASERRCODE` flags. |
| 0x5B | `IC_RET` | 0 | Bare `RET` (`0xC3`). |
| 0x5C | `IC_CALL` | 0 | Direct call to a fixed runtime address (`ic_data`): near `CALL` if within `rel32` range, else `mov reg,imm64`+`call reg`. |
| 0x5D | `IC_CALL_INDIRECT` | 0 | Call through a stack-relative function-pointer slot (`CALL disp[RSP]`). |
| 0x5E | `IC_CALL_INDIRECT2` | 0 | Call through an absolute-address function pointer loaded via `mov reg,imm64` (AOT-relocatable). |
| 0x5F | `IC_CALL_IMPORT` | 0 | Call to an imported/external symbol — `imm64`-load+call or `rel32 CALL` depending on `OPTf_USE_IMM64`; patched via `CAOTImportExport`. |
| 0x60 | `IC_CALL_EXTERN` | 0 | Call to an extern symbol in a static/AOT module (`rel32 CALL`, patched via `CExternUsage`). |
| 0x61 | `IC_ASM` | 0 | Embeds a pre-assembled inline-asm byte blob (`CAOT` template) verbatim, with its own AOT/JIT relocation fixups (`CmpFixUpAOTAsm`/`CmpFixUpJITAsm`). |
| 0x62 | `IC_PUSH` | 1 | Pushes `arg1` onto the machine stack (`ICPush`). |
| 0x63 | `IC_POP` | 0 | Pops the top of the machine stack into `RAX` (`0x58`). |
| 0x64 | `IC_CLFLUSH` | 1 | `CLFLUSH` on the address in `arg1`. |
| 0x65 | `IC_INVLPG` | 1 | `INVLPG` (TLB invalidate) on the address in `arg1`. |
| 0x66 | `IC_IN_U8` | 1 | `IN AL,dx/imm8` — reads a byte from I/O port `arg1` into `res` via `RAX`. |
| 0x67 | `IC_IN_U16` | 1 | `IN AX,dx/imm8` — reads a word from an I/O port. |
| 0x68 | `IC_IN_U32` | 1 | `IN EAX,dx/imm8` — reads a dword from an I/O port. |
| 0x69 | `IC_OUT_U8` | 2 | `OUT dx/imm8,AL` — writes byte `arg2` to I/O port `arg1`. |
| 0x6A | `IC_OUT_U16` | 2 | `OUT dx/imm8,AX` — writes a word to an I/O port. |
| 0x6B | `IC_OUT_U32` | 2 | `OUT dx/imm8,EAX` — writes a dword to an I/O port. |
| 0x6C | `IC_GET_RFLAGS` | 0 | `PUSHF`+`POP` — reads `RFLAGS` into `res` via `RAX`. |
| 0x6D | `IC_CARRY` | 0 | Materializes the x86 carry flag as 0/1 in `RAX` (`SETC AL; AND EAX,1`). |
| 0x6E | `IC_SET_RFLAGS` | 1 | `PUSH arg1; POPF` — writes `RFLAGS`. |
| 0x6F | `IC_GET_RAX` | 0 | Yields the value already in `RAX` as `res`; emits nothing (pairs with `IC_RETURN_VAL2`). |
| 0x70 | `IC_SET_RAX` | 1 | Moves `arg1` into `RAX` (same path as `IC_RETURN_VAL`). |
| 0x71 | `IC_GET_RBP` | 0 | Reads `RBP` into `res`. |
| 0x72 | `IC_SET_RBP` | 1 | Writes `arg1` into `RBP`. |
| 0x73 | `IC_GET_RSP` | 0 | Reads `RSP` into `res`. |
| 0x74 | `IC_SET_RSP` | 1 | Writes `arg1` into `RSP`. |
| 0x75 | `IC_RIP` | 0 (res) | Loads the node's own runtime address (self-relative `RIP`) into `res`; AOT-relocatable. |
| 0x76 | `IC_RDTSC` | 0 | Executes `RDTSC` via a compiled template (`CMP_TEMPLATE_RDTSC`), result in `res`. |
| 0x77 | `IC_BT` | 2 | Bit-test (`BT`) of bit `arg2` in `arg1` (`ICBitOps`); result consumed via flags by a following `CALL_END`/`BR_BT`-style fusion. |
| 0x78 | `IC_BTS` | 2 | Bit-test-and-set (`BTS`). |
| 0x79 | `IC_BTR` | 2 | Bit-test-and-reset (`BTR`). |
| 0x7A | `IC_BTC` | 2 | Bit-test-and-complement (`BTC`). |
| 0x7B | `IC_LBTS` | 2 | `LOCK`-prefixed `BTS` (atomic). |
| 0x7C | `IC_LBTR` | 2 | `LOCK`-prefixed `BTR` (atomic). |
| 0x7D | `IC_LBTC` | 2 | `LOCK`-prefixed `BTC` (atomic). |
| 0x7E | `IC_BSF` | 1 | Bit-scan-forward of `arg1` into `res` (0 on zero input, via `CMOVE` guard). |
| 0x7F | `IC_BSR` | 1 | Bit-scan-reverse of `arg1` into `res`. |
| 0x80 | `IC_QUE_INIT` | 1 | Initializes a doubly-linked queue head at `arg1` (`ICQueInit`; same primitive style as the compiler's own `next`/`last` queues). |
| 0x81 | `IC_QUE_INS` | 2 | Inserts `arg1` into the queue after `arg2` (`ICQueIns`). |
| 0x82 | `IC_QUE_INS_REV` | 2 | Inserts `arg1` into the queue before `arg2` (`ICQueInsRev`). |
| 0x83 | `IC_QUE_REM` | 1 | Removes the node at `arg1` from its queue (`ICQueRem`). |
| 0x84 | `IC_STRLEN` | 1 | Length of the NUL-terminated string at `arg1` via a compiled template (`CMP_TEMPLATE_STRLEN`), result in `res`. |
| 0x85 | `IC_BR_ZERO` | 1 | Branch to `ic_data` if `arg1==0` (`TEST`+`JZ`, `ICTestAndBranch`); the canonical `if(!x)` branch, primary fusion target of `OptBrZero`. |
| 0x86 | `IC_BR_NOT_ZERO` | 1 | Branch to `ic_data` if `arg1!=0` (`TEST`+`JNZ`). |
| 0x87 | `IC_BR_CARRY` | 0 | Branch to `ic_data` if the carry flag is set (`JC`) — follows a flag-setting op such as `BT`. |
| 0x88 | `IC_BR_NOT_CARRY` | 0 | Branch to `ic_data` if carry flag is clear (`JNC`). |
| 0x89 | `IC_BR_EQU_EQU` | 2 | Fused compare-and-branch: branch if `arg1==arg2` (`ICCmpAndBranch`); produced by `OptBrZero` fusing a `CMP` into a following `BR_ZERO`. |
| 0x8A | `IC_BR_NOT_EQU` | 2 | Fused branch if `arg1!=arg2`. |
| 0x8B | `IC_BR_LESS` | 2 | Fused branch if `arg1<arg2` (int `ICCmpAndBranch` or float `ICFCmpAndBranch` per `ICF_USE_F64`). |
| 0x8C | `IC_BR_GREATER_EQU` | 2 | Fused branch if `arg1>=arg2`. |
| 0x8D | `IC_BR_GREATER` | 2 | Fused branch if `arg1>arg2`. |
| 0x8E | `IC_BR_LESS_EQU` | 2 | Fused branch if `arg1<=arg2`. |
| 0x8F | `IC_BR_EQU_EQU2` | 2 (res) | Chained-comparison variant of `BR_EQU_EQU`: ANDs this compare with the previously `IC_PUSH_CMP`'d flag and stores the combined boolean in `res` (for 3-arg chains like `a<b<c`) instead of branching itself. |
| 0x90 | `IC_BR_NOT_EQU2` | 2 (res) | Chained-comparison variant of `BR_NOT_EQU`. |
| 0x91 | `IC_BR_LESS2` | 2 (res) | Chained-comparison variant of `BR_LESS`. |
| 0x92 | `IC_BR_GREATER_EQU2` | 2 (res) | Chained-comparison variant of `BR_GREATER_EQU`. |
| 0x93 | `IC_BR_GREATER2` | 2 (res) | Chained-comparison variant of `BR_GREATER`. |
| 0x94 | `IC_BR_LESS_EQU2` | 2 (res) | Chained-comparison variant of `BR_LESS_EQU`. |
| 0x95 | `IC_BR_AND_ZERO` | 2 | Fused branch if `(arg1 & arg2)==0` (`TEST`+`JZ`, `ICAndBranch`). |
| 0x96 | `IC_BR_AND_NOT_ZERO` | 2 | Fused branch if `(arg1 & arg2)!=0`. |
| 0x97 | `IC_BR_MM_ZERO` | 1 | Fused pre-decrement-and-branch-if-zero: decrements `*arg1` then branches if the result is zero (`ICPreIncDec`+`ICFlagBranch`; counted-loop back-edge idiom). |
| 0x98 | `IC_BR_MM_NOT_ZERO` | 1 | Pre-decrement-and-branch-if-nonzero (classic `LOOP`-style back-edge). |
| 0x99 | `IC_BR_AND_AND_ZERO` | 2 | Transient fused `if(a&&b)` node; expanded by `OptBrZero`, same pass, into two chained `IC_BR_ZERO` branches over `t.arg1_tree`/`t.arg2_tree` — never reaches codegen (`OptLib.HC:315-328`). |
| 0x9A | `IC_BR_AND_AND_NOT_ZERO` | 2 | Transient `if(a&&b)` used as the true-branch condition (opposite polarity); expanded by `OptBrNotZero` the same way. |
| 0x9B | `IC_BR_OR_OR_ZERO` | 2 | Transient fused `if(a\|\|b)` node; expanded into a `BR_NOT_ZERO`/label/`BR_ZERO` pair by `OptBrZero` — never reaches codegen (`OptLib.HC:329-345`). |
| 0x9C | `IC_BR_OR_OR_NOT_ZERO` | 2 | Transient `if(a\|\|b)` true-branch condition; expanded by `OptBrNotZero`. |
| 0x9D | `IC_BR_BT` | 2 | Fused bit-test-and-branch: branch if bit `arg2` of `arg1` is set (`ICBrBitOps`). |
| 0x9E | `IC_BR_BTS` | 2 | Fused bit-test-and-set-and-branch (branches on the pre-test bit, then sets it). |
| 0x9F | `IC_BR_BTR` | 2 | Fused bit-test-and-reset-and-branch. |
| 0xA0 | `IC_BR_BTC` | 2 | Fused bit-test-and-complement-and-branch. |
| 0xA1 | `IC_BR_NOT_BT` | 2 | Fused branch if bit `arg2` of `arg1` is clear. |
| 0xA2 | `IC_BR_NOT_BTS` | 2 | Fused `BTS` + branch-if-bit-was-clear. |
| 0xA3 | `IC_BR_NOT_BTR` | 2 | Fused `BTR` + branch-if-bit-was-clear. |
| 0xA4 | `IC_BR_NOT_BTC` | 2 | Fused `BTC` + branch-if-bit-was-clear. |
| 0xA5 | `IC_SWAP_U8` | 2 | Swaps two `U8` lvalues (`ICSwap`). |
| 0xA6 | `IC_SWAP_U16` | 2 | Swaps two `U16` lvalues. |
| 0xA7 | `IC_SWAP_U32` | 2 | Swaps two `U32` lvalues. |
| 0xA8 | `IC_SWAP_I64` | 2 | Swaps two `I64`/`U64` lvalues. |
| 0xA9 | `IC_ABS_I64` | 1 | Integer absolute value of `arg1` into `res` (branchless `CDQ`+`XOR`+`SUB`). |
| 0xAA | `IC_SIGN_I64` | 1 | Sign function (-1/0/1) of `arg1` into `res` via a compiled template (`CMP_TEMPLATE_SIGN_I64`). |
| 0xAB | `IC_MIN_I64` | 2 | Signed minimum of `arg1,arg2` into `res` (`ICMinMax`, `CMOV`-based). |
| 0xAC | `IC_MIN_U64` | 2 | Unsigned minimum of `arg1,arg2`. |
| 0xAD | `IC_MAX_I64` | 2 | Signed maximum of `arg1,arg2`. |
| 0xAE | `IC_MAX_U64` | 2 | Unsigned maximum of `arg1,arg2`. |
| 0xAF | `IC_MOD_U64` | 2 | Unsigned 64-bit modulo (`ICModU64`; separate from signed `IC_MOD`'s `IDIV`). |
| 0xB0 | `IC_SQR_I64` | 1 | Signed integer square (`arg1*arg1`) via `IMUL` (`ICSqr`, `SLASH_OP_IMUL`). |
| 0xB1 | `IC_SQR_U64` | 1 | Unsigned integer square via `MUL`. |
| 0xB2 | `IC_SQR` | 1 (res) | Floating-point square (`ICFTemplateFun`, `CMP_TEMPLATE_SQR`, x87). |
| 0xB3 | `IC_ABS` | 1 (res) | Floating-point absolute value (`CMP_TEMPLATE_ABS`, x87 `FABS`). |
| 0xB4 | `IC_SQRT` | 1 (res) | Floating-point square root (x87 `FSQRT` via `CMP_TEMPLATE_SQRT`). |
| 0xB5 | `IC_SIN` | 1 (res) | Floating-point sine (x87 `FSIN`). |
| 0xB6 | `IC_COS` | 1 (res) | Floating-point cosine (x87 `FCOS`). |
| 0xB7 | `IC_TAN` | 1 (res) | Floating-point tangent (x87 `FPTAN`-based template). |
| 0xB8 | `IC_ATAN` | 1 (res) | Floating-point arctangent (x87 `FPATAN`-based template). |

**185 opcodes total** (`IC_END`=0x00 .. `IC_ATAN`=0xB8); `IC_ICS_NUM`=0xB9
(185) is the *count*, not itself an opcode.

---

## 3. How the x86-64 backend walks the IC list

The backend is **not** a single "BackA.HC entry point" — `BackA/B/C.HC` are
libraries of per-opcode x86-64 emitter helpers (`ICMul`, `ICAdd*`, `ICShift*`,
`ICAssign*`, `ICBitOps`, `ICMinMax`, etc.) called *from* a driver. The real
driver and entry point is `COCCompile()` in `PrsLib.HC:249-320`, invoked once
per function body after parsing. It runs a fixed pipeline of named passes,
each walking `cc->coc.coc_head.next → ... → IC_END` in list order:

1. **Pass 0** (`OptPass012`, called mid-parse from `PrsExp.HC` for call-arg
   typing) and **passes 1-2** (`OptPass012` called twice more from
   `COCCompile`) — constant folding, dead-code → `IC_NOP1`, type/class
   propagation by walking the `t.arg1_tree`/`arg2_tree` back-pointers, pointer-
   arithmetic scaling, and rewriting `&&`/`\|\|`/chained comparisons into the
   `IC_BR_AND_AND_ZERO`/`IC_BR_OR_OR_ZERO`/`IC_PUSH_CMP`+`*_2` fused forms
   (immediately expanded to concrete branches within the same pass, via
   `OptLib.HC:OptBrZero`/`OptBrNotZero`).
2. **Pass 3** (`OptPass3.HC`) — allocates registers to locals (`reg_offsets`),
   resolves `IC_REG`/`IC_RBP` placeholders into concrete `CICArg` operands,
   fixes up labels/jump tables.
3. **Pass 4** (`OptPass4.HC`) — peephole instruction-combining (e.g. fusing an
   `ADD_CONST`+`DEREF` pair into a single displaced `MOV`/`LEA`) via
   `OptLag1`-based lookback.
4. **Pass 5 / 6** (`OptPass5.HC`/`OptPass6.HC`) — further peephole cleanup and
   abstract-stack-pointer bookkeeping, driven by
   `intermediate_code_table[code].arg_cnt==IS_V_ARG` for opcodes with
   stack-depth side effects (`IC_ADD_RSP`/`IC_PUSH_REGS`/etc.).
5. **Passes 7, 8, 9, 9, 9...** (`OptPass789A()` in `OptPass789A.HC`, called
   repeatedly from `COCCompile`) — the actual x86-64 code generator: for each
   node, a giant `switch(tmpi->ic_code)` dispatches into the `BackA/B/C/BackFA/
   BackFB.HC` emitter helpers, which call `ICU8/16/24/32/64` (`BackLib.HC`) to
   append raw bytes into `tmpi->ic_body`, tracked by `tmpi->ic_cnt`. Passes 7-9
   re-run the whole walk to let jump/displacement sizes (short vs. near) and
   peephole lag-merges (`ic_last_start`) converge; `COCCompile` loops pass 9
   until `code_size` stops shrinking (`PrsLib.HC:287-296`).
6. **Pass 10** (final `OptPass789A()` call) — same walk, but this time `buf`
   is a real allocated code buffer: each node's `ic_body[0..ic_cnt)` is
   `MemCpy`'d into `buf` at the running `rip`, `CDbgInfo` line→address entries
   are recorded from `ic_line`, and `CMT_STR_CONST`/`CMT_JMP_TABLE`/
   `CMT_FLOAT_CONSTS` misc-list entries are appended after the code and
   AOT-relocation records (`CAOTAbsAddr`/`CAOTImportExport`/`CExternUsage`)
   are finalized.

---

## 4. Type / register notation used inside `CICArg`/`CICType`

From `shrine/Kernel/KernelA.HH:1562-1600` (section literally headed
`"Compiler/Intermediate Code"`):

```c
#define RT_I0   2
#define RT_U0   3
#define RT_I8   4
#define RT_U8   5
#define RT_I16  6
#define RT_U16  7
#define RT_I32  8
#define RT_U32  9
#define RT_I64  10
#define RT_PTR  10 //Signed to allow negative err codes.
#define RT_U64  11
#define RT_F32  12 //Not implemented
#define RT_UF32 13 //Not implemented, Fictitious
#define RT_F64  14
#define RT_UF64 15 //Fictitious
#define RT_RTS_NUM 16
#define RTF_UNSIGNED 1
#define RTG_MASK 0xFF

#define MDf_STK        8
#define MDf_IMM        9
#define MDf_REG        10
#define MDf_DISP       11
#define MDf_SIB        12
#define MDf_RIP_DISP32 13

#define MDF_NULL       0x0000
#define MDF_STK        0x0100
#define MDF_IMM        0x0200
#define MDF_REG        0x0400
#define MDF_DISP       0x0800
#define MDF_SIB        0x1000
#define MDF_RIP_DISP32 0x2000
#define MDG_MASK              0xFF00
#define MDG_REG_DISP_SIB      (MDF_REG|MDF_DISP|MDF_SIB)
#define MDG_DISP_SIB_RIP      (MDF_DISP|MDF_SIB|MDF_RIP_DISP32)
#define MDG_REG_DISP_SIB_RIP  (MDF_REG|MDG_DISP_SIB_RIP)
```

- **`RT_*`** ("raw type") is the operand's HolyC type as a small integer —
  this is what `CICType.raw_type` and `CHashClass.raw_type` carry. `RT_PTR`
  deliberately aliases `RT_I64`. `RTF_UNSIGNED` (bit 0) is OR'd in on top of
  the base `RT_*` value for unsigned variants; `RTG_MASK` masks it back off.
  This is the type system every backend arithmetic/compare helper switches
  on (e.g. `tmpi->ic_class->raw_type&RTF_UNSIGNED` chooses signed vs.
  unsigned `MUL`/`DIV`/branch encodings throughout `BackA.HC`).
- **`MDF_*`/`MDG_*`** ("mode flags") is the addressing-mode bitmask carried in
  `CICType.mode`, combined by simple OR: `MDF_REG` (plain register),
  `MDF_IMM` (immediate operand, value in `CICArg.disp`), `MDF_STK` (still on
  the abstract eval stack, not yet materialized), `MDF_DISP` (register +
  displacement), `MDF_SIB` (indexed, uses the high byte of `CICArg.reg` as
  `index_reg+scale<<6`), `MDF_RIP_DISP32` (RIP-relative). The `MDG_*` unions
  (`MDG_REG_DISP_SIB`, `MDG_DISP_SIB_RIP`, `MDG_REG_DISP_SIB_RIP`) are the
  actual "is this a real memory/register operand" tests used pervasively,
  e.g. `t2&MDG_REG_DISP_SIB_RIP` in `BackA.HC:ICAddEct`.
- Concrete x86-64 register numbers (`REG_RAX` etc., not shown here — declared
  elsewhere) go in `CICArg.reg`'s low byte when `MDF_REG` is set; `disp` is
  reused as the plain immediate value when `MDF_IMM` is set instead.

---

**Not committed to git** (per instructions).
