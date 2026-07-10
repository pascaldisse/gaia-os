// BackA64F.HC — BackA64 family (g) float ops + family (h) x86-specific
// system-op rulings, lowering HolyC IC nodes to aarch64.
//
// Contract: kernel-arm/zoneC/BACKA64-DESIGN.md section 5, families (g) and
// (h) ONLY (int arith/branches/calls live in the sibling BackA64*.HC files,
// per the file plan in DESIGN.md section 7). Opcode names/args/semantics are
// IC-HolyC.md + shrine/Kernel/KernelA.HH:1657-1687 (CIntermediateCode) and
// shrine/Compiler/CompilerA.HH (the IC_* #defines) — the canonical 185-opcode
// set. Encoders are Arm64Asm.HC (incl. the A1 amendment ARM_fabsReg/
// ARM_fsqrtReg already present there). Operand plumbing (A64W, A64MovImm,
// A64CC, A64LoadArg/A64StoreRes, A64LoadArgF/A64StoreResF, A64Deref) is
// BackA64Lib.HC.
//
// Structure: one entry point, A64GenF(cc,tmpi) — a switch on tmpi->ic_code.
// A case that claims the node emits into tmpi->ic_body (via A64W) and
// returns TRUE; anything this family doesn't own returns FALSE so the
// driver (OptPass789A64.HC) can try the next family. Some opcodes (ADD/SUB/
// MUL/DIV, UNARY_MINUS, the six value-producing compares) are *shared* with
// the integer family (BackA64.HC) — exactly as Shrine's own x86-64 backend
// shares them (OptPass789A.HC checks `tmpi->ic_flags&ICF_USE_F64` inline in
// the same case); here each shared case checks that flag and returns FALSE
// immediately if the node isn't float, deferring to the int family file.
//
// Discrepancy note (read before extending this file): BACKA64-DESIGN.md
// section 5(h) lists two rows — `IC_INTERRUPT on/off` and `IC_HALT` — that
// do NOT correspond to any opcode in the canonical IC_* set (verified
// against shrine/Compiler/CompilerA.HH: the 185 opcodes run IC_END=0x00..
// IC_ATAN=0xB8, no HALT/INTERRUPT entry exists). In Shrine, HLT/CLI/STI are
// plain x86 mnemonics used only inside `asm{}` blocks (IC_ASM), which Zone
// C3 explicitly excludes from the initial ARM build. There is therefore no
// `tmpi->ic_code` value to switch on for them, and adding a case naming a
// nonexistent #define would not compile. The raw words the design doc (and
// the task spec) gave for these are kept below as inert reference constants
// for whenever C3 ports IC_ASM to aarch64 — no live case uses them.
// Similarly, `IC_GET/SET_CARRY` in that table is really just `IC_CARRY`
// (0x6D, read-only — no separate "set carry" opcode exists).

// Reference-only: raw encodings named in BACKA64-DESIGN.md section 5(h) that
// have no corresponding IC_* opcode today (see discrepancy note above). Not
// referenced by any case in A64GenF; kept so the values aren't lost.
#define A64F_REF_WFI			0xD503207F	// WFI — future IC_ASM(HLT)
#define A64F_REF_MSR_DAIFSET_2		0xD50342DF	// MSR DAIFSet,#2 — future IC_ASM(CLI)
#define A64F_REF_MSR_DAIFCLR_2		0xD50343DF	// MSR DAIFClr,#2 — future IC_ASM(STI)

// Placeholders for the runtime transcendental-function addresses. HolyC/
// aarch64 has no hardware SIN/COS/TAN/ATAN; the kernel math lib (Zone A/E,
// poly approximations) supplies them. C4 bootstrap patches these four
// globals with the real function addresses before any HolyC compilation
// through this backend runs; A64GenF bakes whatever value is here *now*
// into the MOVZ/MOVK veneer at each SIN/COS/TAN/ATAN call site.
I64 back_a64_sin_addr;
I64 back_a64_cos_addr;
I64 back_a64_tan_addr;
I64 back_a64_atan_addr;

// Shared tail for IC_SIN/COS/TAN/ATAN: arg1 already loaded into d0 by the
// caller. TempleOS/HolyC's calling convention is everything-in-the-
// accumulator (RAX on x86, x0 here), so the float argument crosses into the
// call as raw bits in x0, and the raw-bits result crosses back the same way
// — mirrors section 3's "F64 as raw bits via fmov" ruling for IC_LEAVE.
U0 A64FTransCall(CIntermediateCode *tmpi, I64 fn_addr)
{
  A64W(tmpi, ARM_fmovI64F64(0, 0));	// fmov x0, d0
  A64MovImm(tmpi, 16, fn_addr);	// x16 = runtime fn addr (C4-patched)
  A64W(tmpi, ARM_blr(16));		// blr  x16
  A64W(tmpi, ARM_fmovF64I64(0, 0));	// fmov d0, x0
}

Bool A64GenF(CCmpCtrl *cc, CIntermediateCode *tmpi)
{
  switch (tmpi->ic_code) {

  //=== (g) float — shared arithmetic (float half only; int half is
  //    BackA64.HC's job, gated the same way Shrine's own OptPass789A.HC
  //    gates ICAddEct/ICFAdd et al on ICF_USE_F64) ===================
  case IC_ADD:
  case IC_SUB:
  case IC_MUL:
  case IC_DIV:
    if (!(tmpi->ic_flags & ICF_USE_F64))
      return FALSE;
    A64LoadArgF(tmpi, 0, &tmpi->arg1);
    A64LoadArgF(tmpi, 1, &tmpi->arg2);
    switch (tmpi->ic_code) {
    case IC_ADD: A64W(tmpi, ARM_faddReg(0, 0, 1)); break;
    case IC_SUB: A64W(tmpi, ARM_fsubReg(0, 0, 1)); break;
    case IC_MUL: A64W(tmpi, ARM_fmulReg(0, 0, 1)); break;
    case IC_DIV: A64W(tmpi, ARM_fdivReg(0, 0, 1)); break;
    }
    if (tmpi->res.type.mode)
      A64StoreResF(tmpi, 0, &tmpi->res);
    return TRUE;

  case IC_UNARY_MINUS:	// FNEG — shared with int NEG (BackA64.HC)
    if (!(tmpi->ic_flags & ICF_USE_F64))
      return FALSE;
    A64LoadArgF(tmpi, 0, &tmpi->arg1);
    A64W(tmpi, ARM_fnegReg(0, 0));
    if (tmpi->res.type.mode)
      A64StoreResF(tmpi, 0, &tmpi->res);
    return TRUE;

  //=== (g) float — conversions (always float-typed opcodes, no gate) ==
  case IC_TO_F64:	// I64->F64, in-place (res_cnt==0: value stays in d0)
    A64LoadArg(tmpi, 0, &tmpi->arg1, tmpi->arg1.type.raw_type);
    if (tmpi->arg1.type.raw_type & RTF_UNSIGNED)
      A64W(tmpi, ARM_ucvtf(0, 0));
    else
      A64W(tmpi, ARM_scvtf(0, 0));
    return TRUE;

  case IC_TO_I64:	// F64->I64, in-place (res_cnt==0: value stays in x0)
    A64LoadArgF(tmpi, 0, &tmpi->arg1);
    A64W(tmpi, ARM_fcvtzs(0, 0));	// truncate toward zero (CVTTSD2SI-alike)
    return TRUE;

  //=== (g) float — compares (value-producing; shared with int cmpRegX
  //    path in BackA64Br.HC, gated the same way) ======================
  case IC_EQU_EQU:
  case IC_NOT_EQU:
  case IC_LESS:
  case IC_GREATER_EQU:
  case IC_GREATER:
  case IC_LESS_EQU:
    if (!(tmpi->ic_flags & ICF_USE_F64))
      return FALSE;
    A64LoadArgF(tmpi, 0, &tmpi->arg1);
    A64LoadArgF(tmpi, 1, &tmpi->arg2);
    A64W(tmpi, ARM_fcmp(0, 1));
    // Float compares are always the "signed"/ordered flavor (section 4's
    // condition map); NaN falls out unordered exactly as the x87 form did
    // — no separate NaN branch in v1.
    A64W(tmpi, ARM_csetX(0, A64CC(tmpi->ic_code, FALSE)));
    if (tmpi->res.type.mode)
      A64StoreRes(tmpi, 0, &tmpi->res, tmpi->res.type.raw_type);
    return TRUE;

  //=== (g) float — template funcs (always float-typed, no gate) =======
  case IC_SQR:		// float square: fmul d0,d0,d0 (distinct from
			// IC_SQR_I64/IC_SQR_U64, which are int-only opcodes
			// owned by BackA64.HC)
    A64LoadArgF(tmpi, 0, &tmpi->arg1);
    A64W(tmpi, ARM_fmulReg(0, 0, 0));
    if (tmpi->res.type.mode)
      A64StoreResF(tmpi, 0, &tmpi->res);
    return TRUE;

  case IC_ABS:		// float abs — amendment A1, ARM_fabsReg
    A64LoadArgF(tmpi, 0, &tmpi->arg1);
    A64W(tmpi, ARM_fabsReg(0, 0));
    if (tmpi->res.type.mode)
      A64StoreResF(tmpi, 0, &tmpi->res);
    return TRUE;

  case IC_SQRT:		// float sqrt — amendment A1, ARM_fsqrtReg
    A64LoadArgF(tmpi, 0, &tmpi->arg1);
    A64W(tmpi, ARM_fsqrtReg(0, 0));
    if (tmpi->res.type.mode)
      A64StoreResF(tmpi, 0, &tmpi->res);
    return TRUE;

  case IC_SIN:
    A64LoadArgF(tmpi, 0, &tmpi->arg1);
    A64FTransCall(tmpi, back_a64_sin_addr);
    if (tmpi->res.type.mode)
      A64StoreResF(tmpi, 0, &tmpi->res);
    return TRUE;

  case IC_COS:
    A64LoadArgF(tmpi, 0, &tmpi->arg1);
    A64FTransCall(tmpi, back_a64_cos_addr);
    if (tmpi->res.type.mode)
      A64StoreResF(tmpi, 0, &tmpi->res);
    return TRUE;

  case IC_TAN:
    A64LoadArgF(tmpi, 0, &tmpi->arg1);
    A64FTransCall(tmpi, back_a64_tan_addr);
    if (tmpi->res.type.mode)
      A64StoreResF(tmpi, 0, &tmpi->res);
    return TRUE;

  case IC_ATAN:
    A64LoadArgF(tmpi, 0, &tmpi->arg1);
    A64FTransCall(tmpi, back_a64_atan_addr);
    if (tmpi->res.type.mode)
      A64StoreResF(tmpi, 0, &tmpi->res);
    return TRUE;

  //=== (h) x86-specific — port I/O: no aarch64 equivalent, ever =======
  case IC_IN_U8:
  case IC_IN_U16:
  case IC_IN_U32:
  case IC_OUT_U8:
  case IC_OUT_U16:
  case IC_OUT_U32:
    throw('CmpErr');	// "no port I/O on aarch64 — use MMIO"; code that
			// reaches these must be ported, not humored.
    return TRUE;

  //=== (h) x86-specific — RDTSC -> CNTVCT_EL0 =========================
  case IC_RDTSC:
    A64W(tmpi, 0xD53BE040);	// MRS X0, CNTVCT_EL0
    if (tmpi->res.type.mode)
      A64StoreRes(tmpi, 0, &tmpi->res, tmpi->res.type.raw_type);
    return TRUE;

  //=== (h) x86-specific — CLFLUSH -> DC CIVAC + DSB SY ================
  case IC_CLFLUSH:
    A64LoadArg(tmpi, 0, &tmpi->arg1, tmpi->arg1.type.raw_type);
    A64W(tmpi, 0xD50B7E20);	// DC CIVAC, X0
    A64W(tmpi, 0xD5033F9F);	// DSB SY
    return TRUE;

  //=== (h) x86-specific — INVLPG -> TLBI + DSB ISH + ISB ==============
  case IC_INVLPG:
    A64LoadArg(tmpi, 0, &tmpi->arg1, tmpi->arg1.type.raw_type);
    // TLBI VAAE1, X0 — verified by hand-decode (nyari, 2026-07-10):
    // 0xD5088760 = SYS op0=1,op1=0,CRn=8,CRm=7,op2=3,Rt=0 = TLBI VAAE1
    // (all-ASID by-VA, EL1, non-shareable; the IS variant would be CRm=3).
    A64W(tmpi, 0xD5088760);	// TLBI VAAE1, X0 (op1=0,CRn=8,CRm=7,op2=3)
    A64W(tmpi, 0xD5033B9F);	// DSB ISH
    A64W(tmpi, 0xD5033FDF);	// ISB
    return TRUE;

  //=== (h) x86-specific — FS/GS base read -> TPIDR_EL0/EL1 ============
  // FS-role -> TPIDR_EL0, GS-role (kernel per-cpu/task ptr) -> TPIDR_EL1,
  // per section 5(h)'s ruling. Only the "get" direction exists as a real
  // IC opcode (IC_FS/IC_GS, 0x15/0x17); there is no IC_SET_FS/IC_SET_GS
  // in the canonical set. IC_MOV_FS/IC_MOV_GS (typed loads through the
  // segment base, 0x16/0x18) are data-movement family (b) territory, not
  // itemized in 5(g)/5(h) — left for whichever file picks up family (b).
  case IC_FS:
    A64W(tmpi, 0xD53BD040);	// MRS X0, TPIDR_EL0
    if (tmpi->res.type.mode)
      A64StoreRes(tmpi, 0, &tmpi->res, tmpi->res.type.raw_type);
    return TRUE;

  case IC_GS:
    A64W(tmpi, 0xD538D040);	// MRS X0, TPIDR_EL1
    if (tmpi->res.type.mode)
      A64StoreRes(tmpi, 0, &tmpi->res, tmpi->res.type.raw_type);
    return TRUE;

  //=== (h) x86-specific — RFLAGS/CARRY -> NZCV ========================
  // Only NZCV+DAIF are meaningful to HolyC code (section 5h); this v1
  // reads/writes NZCV only — DAIF bit-merging is left as a follow-up per
  // the design doc's own "documented in the impl" caveat.
  case IC_GET_RFLAGS:
    A64W(tmpi, 0xD53B4200);	// MRS X0, NZCV
    if (tmpi->res.type.mode)
      A64StoreRes(tmpi, 0, &tmpi->res, tmpi->res.type.raw_type);
    return TRUE;

  case IC_SET_RFLAGS:
    A64LoadArg(tmpi, 0, &tmpi->arg1, tmpi->arg1.type.raw_type);
    A64W(tmpi, 0xD51B4200);	// MSR NZCV, X0
    return TRUE;

  case IC_CARRY:
    // Materializes NZCV.C as 0/1. Assumes NZCV.C is still live from
    // whichever preceding op set it — same caveat section 5(d) documents
    // for IC_BR_CARRY (our BT lowering sets a value reg, not C; audited
    // at the call sites that actually produce this pairing).
    A64W(tmpi, ARM_csetX(0, 2));	// cset x0, cs  (CS/HS == carry set)
    if (tmpi->res.type.mode)
      A64StoreRes(tmpi, 0, &tmpi->res, tmpi->res.type.raw_type);
    return TRUE;

  default:
    return FALSE;
  }
}
