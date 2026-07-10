// BackA64.HC — aarch64 backend for Shrine HolyC, integer-op lowering.
//
// Design doc: BACKA64-DESIGN.md (this directory) — every lowering below
// implements section 5 families (b) data movement, (c) integer arithmetic
// and (d) bit ops, exactly as tabulated there. Register model is design
// section 2; operand materialization is design section 4 (the four
// BackA64Lib.HC helpers: A64W, A64MovImm, A64LoadArg, A64StoreRes,
// A64Deref, A64CC — assumed to exist, called per their documented
// signatures). Opcode contract (struct + full 185-opcode table) is
// IC-HolyC.md sections 1-2. Encoders are Arm64Asm.HC (each returns the
// instruction word; emitted via A64W).
//
// Scope: families (e) compares/branches, (f) calls, (g) float, (h)
// x86-specific system-op rulings are NOT handled here — those live in
// BackA64Br.HC / BackA64F.HC (design section 7). Every case below
// returns TRUE; anything this file doesn't recognize (including any
// node whose value type turns out to be floating point, dual-dispatch
// opcodes like IC_ADD/IC_MUL/etc.) falls through to `default: return
// FALSE`, per the driver contract, so a sibling family file gets a shot
// at it.
//
// Register convention used throughout (design section 2): x0/x1 are the
// two operand registers for the general binary-op idiom, x9/x10/x11/x12/
// x13 are backend-internal scratch (div temps, swap temps, bit masks,
// atomic-loop status). x2 holds a synthesized effective address when a
// lowering needs one (A64Deref output).

// (HolyC: parameterized macros are not supported — real function, same name)
Bool A64_FLOATY(I64 rt)
{
  return rt == RT_F64 || rt == RT_F32;
}

// --- Local plumbing: load/store a raw_type-sized value through an
// address already sitting in a register (offset 0). This is NOT one of
// BackA64Lib.HC's four helpers — A64LoadArg/A64StoreRes take a CICArg
// (an addressing-mode descriptor), not a bare address register. These
// two exist purely to implement the "A64Deref the lvalue once, then
// load-op-store through x9" idiom design section 5(c) mandates for the
// compound-assign (IST_ASSIGN) forms and the ++/-- family, and the
// atomic bit-RMW loop in 5(d). Size/sign switch mirrors the one A64LoadArg/
// A64StoreRes apply internally, per design section 4.
U0 A64LdAddr(CIntermediateCode *tmpi, I64 rx, I64 addr_reg, I64 raw_type) {
  switch (raw_type) {
    case RT_I8:  A64W(tmpi, ARM_ldrsbX(rx, addr_reg, 0));     break;
    case RT_U8:  A64W(tmpi, ARM_ldrbRegImm(rx, addr_reg, 0)); break;
    case RT_I16: A64W(tmpi, ARM_ldrshX(rx, addr_reg, 0));     break;
    case RT_U16: A64W(tmpi, ARM_ldrhRegImm(rx, addr_reg, 0)); break;
    case RT_I32: A64W(tmpi, ARM_ldrswX(rx, addr_reg, 0));     break;
    case RT_U32: A64W(tmpi, ARM_ldrRegImm(rx, addr_reg, 0));  break;
    default:     A64W(tmpi, ARM_ldrRegImmX(rx, addr_reg, 0)); break; //I64/U64/PTR
  }
}

U0 A64StAddr(CIntermediateCode *tmpi, I64 rx, I64 addr_reg, I64 raw_type) {
  switch (raw_type) {
    case RT_I8:  case RT_U8:  A64W(tmpi, ARM_strbRegImm(rx, addr_reg, 0)); break;
    case RT_I16: case RT_U16: A64W(tmpi, ARM_strhRegImm(rx, addr_reg, 0)); break;
    case RT_I32: case RT_U32: A64W(tmpi, ARM_strRegImm(rx, addr_reg, 0));  break;
    default:                  A64W(tmpi, ARM_strRegImmX(rx, addr_reg, 0)); break;
  }
}

// A64GenInt — family (b)(c)(d) driver. `cc` is unused here (no lowering
// below needs compiler-context state beyond `tmpi` itself); it is carried
// only to match the shared per-family entry-point signature the eventual
// OptPass789A64 driver calls through.
Bool A64GenInt(CCmpCtrl *cc, CIntermediateCode *tmpi) {
  I64 rt, pt;
  switch (tmpi->ic_code) {

    //=======================================================================
    // (b) Data movement — BACKA64-DESIGN.md 5(b)
    //=======================================================================

    // IC_MOV: "Generic move of arg1 into res" (IC-HolyC.md 0x1A).
    // IC_DEREF: "loads the pointee into res" (0x24) — the pointer's own
    // addressing mode is already folded into arg1's CICArg (MDF_DISP/SIB/
    // etc.), so this is byte-for-byte the same lowering as IC_MOV: design
    // row "IC_DEREF* | covered by LoadArg modes".
    case IC_MOV:
    case IC_DEREF: {
      rt = tmpi->ic_class->raw_type;
      if (A64_FLOATY(rt)) return FALSE; //float lvalues -> BackA64F.HC
      A64LoadArg(tmpi, 0, &tmpi->arg1, rt);
      A64StoreRes(tmpi, 0, &tmpi->res, rt);
      return TRUE;
    }

    // IC_ASSIGN: design row is explicit that the *source* is arg2, not
    // arg1: "A64LoadArg(x0, arg2) -> A64StoreRes(x0, res)".
    case IC_ASSIGN: {
      rt = tmpi->ic_class->raw_type;
      if (A64_FLOATY(rt)) return FALSE;
      A64LoadArg(tmpi, 0, &tmpi->arg2, rt);
      A64StoreRes(tmpi, 0, &tmpi->res, rt);
      return TRUE;
    }

    // IC_ADDR (&x): "IC_LEA-alikes (IC_ADDR) | A64Deref(x0, arg1) -> store".
    // Address-of never dereferences, so LoadArg (a load) is wrong here —
    // A64Deref synthesizes the effective address only. Result of &x is
    // always pointer-typed (RT_PTR==RT_I64), never float — no guard needed.
    case IC_ADDR: {
      A64Deref(tmpi, 0, &tmpi->arg1);
      A64StoreRes(tmpi, 0, &tmpi->res, tmpi->ic_class->raw_type);
      return TRUE;
    }

    // IC_SWAP_U8/U16/U32/I64: "load a->x9, load b->x10, store x10->a,
    // x9->b" (design row); each opcode names its own exact width, so the
    // raw_type is hardcoded per case rather than read from ic_class — a
    // swap is a pure byte-exchange, no interpretation of the bits happens,
    // so this is correct even for an F64 swap (which reuses IC_SWAP_I64;
    // IC-HolyC.md: "Swaps two I64/U64 lvalues").
    case IC_SWAP_U8: {
      A64LoadArg(tmpi, 9, &tmpi->arg1, RT_U8);
      A64LoadArg(tmpi, 10, &tmpi->arg2, RT_U8);
      A64StoreRes(tmpi, 10, &tmpi->arg1, RT_U8);
      A64StoreRes(tmpi, 9, &tmpi->arg2, RT_U8);
      return TRUE;
    }
    case IC_SWAP_U16: {
      A64LoadArg(tmpi, 9, &tmpi->arg1, RT_U16);
      A64LoadArg(tmpi, 10, &tmpi->arg2, RT_U16);
      A64StoreRes(tmpi, 10, &tmpi->arg1, RT_U16);
      A64StoreRes(tmpi, 9, &tmpi->arg2, RT_U16);
      return TRUE;
    }
    case IC_SWAP_U32: {
      A64LoadArg(tmpi, 9, &tmpi->arg1, RT_U32);
      A64LoadArg(tmpi, 10, &tmpi->arg2, RT_U32);
      A64StoreRes(tmpi, 10, &tmpi->arg1, RT_U32);
      A64StoreRes(tmpi, 9, &tmpi->arg2, RT_U32);
      return TRUE;
    }
    case IC_SWAP_I64: {
      A64LoadArg(tmpi, 9, &tmpi->arg1, RT_I64);
      A64LoadArg(tmpi, 10, &tmpi->arg2, RT_I64);
      A64StoreRes(tmpi, 10, &tmpi->arg1, RT_I64);
      A64StoreRes(tmpi, 9, &tmpi->arg2, RT_I64);
      return TRUE;
    }

    //=======================================================================
    // (c) Integer arithmetic — BACKA64-DESIGN.md 5(c)
    //=======================================================================

    // ADD/SUB dual-dispatch with float per IC-HolyC.md ("ICAddEct int /
    // ICFAdd float", per ICF_USE_INT). Binary-op idiom exactly as given:
    // load both operands to x0/x1, rg-rg op, store res.
    case IC_ADD: {
      if (!(tmpi->ic_flags & ICF_USE_INT)) return FALSE;
      rt = tmpi->ic_class->raw_type;
      A64LoadArg(tmpi, 0, &tmpi->arg1, rt);
      A64LoadArg(tmpi, 1, &tmpi->arg2, rt);
      A64W(tmpi, ARM_addRegX(0, 0, 1));
      A64StoreRes(tmpi, 0, &tmpi->res, rt);
      return TRUE;
    }
    case IC_SUB: {
      if (!(tmpi->ic_flags & ICF_USE_INT)) return FALSE;
      rt = tmpi->ic_class->raw_type;
      A64LoadArg(tmpi, 0, &tmpi->arg1, rt);
      A64LoadArg(tmpi, 1, &tmpi->arg2, rt);
      A64W(tmpi, ARM_subRegX(0, 0, 1));
      A64StoreRes(tmpi, 0, &tmpi->res, rt);
      return TRUE;
    }

    // IC_ADD_CONST/IC_SUB_CONST: folded single-arg form, constant lives in
    // ic_data (no arg2 CICArg to load) — materialize via A64MovImm, then
    // the same rg-rg op.
    case IC_ADD_CONST: {
      rt = tmpi->ic_class->raw_type;
      A64LoadArg(tmpi, 0, &tmpi->arg1, rt);
      A64MovImm(tmpi, 1, tmpi->ic_data);
      A64W(tmpi, ARM_addRegX(0, 0, 1));
      A64StoreRes(tmpi, 0, &tmpi->res, rt);
      return TRUE;
    }
    case IC_SUB_CONST: {
      rt = tmpi->ic_class->raw_type;
      A64LoadArg(tmpi, 0, &tmpi->arg1, rt);
      A64MovImm(tmpi, 1, tmpi->ic_data);
      A64W(tmpi, ARM_subRegX(0, 0, 1));
      A64StoreRes(tmpi, 0, &tmpi->res, rt);
      return TRUE;
    }

    // IC_MUL: dual-dispatch ("ICMul int / ICFMul float").
    case IC_MUL: {
      if (!(tmpi->ic_flags & ICF_USE_INT)) return FALSE;
      rt = tmpi->ic_class->raw_type;
      A64LoadArg(tmpi, 0, &tmpi->arg1, rt);
      A64LoadArg(tmpi, 1, &tmpi->arg2, rt);
      A64W(tmpi, ARM_mulRegX(0, 0, 1));
      A64StoreRes(tmpi, 0, &tmpi->res, rt);
      return TRUE;
    }

    // IC_SQR_I64/IC_SQR_U64: "MUL / SQR_I64 / SQR_U64 | mulRegX (low 64
    // bits — Shrine semantics)". Single-arg (arg1*arg1); low-64-bit result
    // is identical whether the multiply is interpreted signed or unsigned,
    // so both opcodes share one lowering.
    case IC_SQR_I64:
    case IC_SQR_U64: {
      rt = tmpi->ic_class->raw_type;
      A64LoadArg(tmpi, 0, &tmpi->arg1, rt);
      A64W(tmpi, ARM_mulRegX(0, 0, 0));
      A64StoreRes(tmpi, 0, &tmpi->res, rt);
      return TRUE;
    }

    // IC_DIV: dual-dispatch ("ICDiv/ICFDiv"); signed/unsigned chosen by
    // RTF_UNSIGNED on raw_type (there is only one DIV opcode, unlike MOD).
    case IC_DIV: {
      if (!(tmpi->ic_flags & ICF_USE_INT)) return FALSE;
      rt = tmpi->ic_class->raw_type;
      A64LoadArg(tmpi, 0, &tmpi->arg1, rt);
      A64LoadArg(tmpi, 1, &tmpi->arg2, rt);
      if (rt & RTF_UNSIGNED) A64W(tmpi, ARM_udivX(0, 0, 1));
      else                   A64W(tmpi, ARM_sdivRegX(0, 0, 1));
      A64StoreRes(tmpi, 0, &tmpi->res, rt);
      return TRUE;
    }

    // IC_MOD (always signed, IC_MOD_U64 always unsigned — the front end
    // already picked the opcode by type, IC-HolyC.md: "separate from
    // signed IC_MOD's IDIV"). MUST-follow spec: "sdiv/udiv x9,x0,x1 then
    // mul x9,x9,x1 then sub x0,x0,x9 (no msub)".
    case IC_MOD: {
      rt = tmpi->ic_class->raw_type;
      A64LoadArg(tmpi, 0, &tmpi->arg1, rt);
      A64LoadArg(tmpi, 1, &tmpi->arg2, rt);
      A64W(tmpi, ARM_sdivRegX(9, 0, 1));
      A64W(tmpi, ARM_mulRegX(9, 9, 1));
      A64W(tmpi, ARM_subRegX(0, 0, 9));
      A64StoreRes(tmpi, 0, &tmpi->res, rt);
      return TRUE;
    }
    case IC_MOD_U64: {
      rt = tmpi->ic_class->raw_type;
      A64LoadArg(tmpi, 0, &tmpi->arg1, rt);
      A64LoadArg(tmpi, 1, &tmpi->arg2, rt);
      A64W(tmpi, ARM_udivX(9, 0, 1));
      A64W(tmpi, ARM_mulRegX(9, 9, 1));
      A64W(tmpi, ARM_subRegX(0, 0, 9));
      A64StoreRes(tmpi, 0, &tmpi->res, rt);
      return TRUE;
    }

    // AND/OR/XOR: no float form exists for bitwise ops, no ICF_USE_INT
    // guard needed. A64LoadArg already materializes an MDF_IMM arg2 via
    // the shortest encoding internally (design section 4), so the plain
    // binary-op idiom covers the "imm forms" case too.
    case IC_AND: {
      rt = tmpi->ic_class->raw_type;
      A64LoadArg(tmpi, 0, &tmpi->arg1, rt);
      A64LoadArg(tmpi, 1, &tmpi->arg2, rt);
      A64W(tmpi, ARM_andRegX(0, 0, 1));
      A64StoreRes(tmpi, 0, &tmpi->res, rt);
      return TRUE;
    }
    case IC_OR: {
      rt = tmpi->ic_class->raw_type;
      A64LoadArg(tmpi, 0, &tmpi->arg1, rt);
      A64LoadArg(tmpi, 1, &tmpi->arg2, rt);
      A64W(tmpi, ARM_orrRegX(0, 0, 1));
      A64StoreRes(tmpi, 0, &tmpi->res, rt);
      return TRUE;
    }
    case IC_XOR: {
      rt = tmpi->ic_class->raw_type;
      A64LoadArg(tmpi, 0, &tmpi->arg1, rt);
      A64LoadArg(tmpi, 1, &tmpi->arg2, rt);
      A64W(tmpi, ARM_eorRegX(0, 0, 1));
      A64StoreRes(tmpi, 0, &tmpi->res, rt);
      return TRUE;
    }

    // IC_COM (~x): "NOT (~) | mvnRegX". Single-arg, bitwise, no float form.
    case IC_COM: {
      rt = tmpi->ic_class->raw_type;
      A64LoadArg(tmpi, 0, &tmpi->arg1, rt);
      A64W(tmpi, ARM_mvnRegX(0, 0));
      A64StoreRes(tmpi, 0, &tmpi->res, rt);
      return TRUE;
    }

    // IC_UNARY_MINUS (-x): dual-dispatch ("integer NEG or float path per
    // res type", IC-HolyC.md 0x23). Design row rejected a plain
    // subRegX-from-XZR trick in favor of the dedicated negsRegX encoder.
    case IC_UNARY_MINUS: {
      if (!(tmpi->ic_flags & ICF_USE_INT)) return FALSE;
      rt = tmpi->ic_class->raw_type;
      A64LoadArg(tmpi, 0, &tmpi->arg1, rt);
      A64W(tmpi, ARM_negsRegX(0, 0));
      A64StoreRes(tmpi, 0, &tmpi->res, rt);
      return TRUE;
    }

    // IC_SHL/IC_SHR: "lslvRegX/lsrvRegX/asrvRegX; imm counts ->
    // lslImmX/lsrImmX/asrImmX". The variable-shift-register form covers
    // the general binary-op idiom (shift count loaded like any operand);
    // the *_CONST folded forms below use the immediate encoders directly.
    case IC_SHL: {
      rt = tmpi->ic_class->raw_type;
      A64LoadArg(tmpi, 0, &tmpi->arg1, rt);
      A64LoadArg(tmpi, 1, &tmpi->arg2, rt);
      A64W(tmpi, ARM_lslvRegX(0, 0, 1));
      A64StoreRes(tmpi, 0, &tmpi->res, rt);
      return TRUE;
    }
    case IC_SHR: {
      rt = tmpi->ic_class->raw_type;
      A64LoadArg(tmpi, 0, &tmpi->arg1, rt);
      A64LoadArg(tmpi, 1, &tmpi->arg2, rt);
      if (rt & RTF_UNSIGNED) A64W(tmpi, ARM_lsrvRegX(0, 0, 1));
      else                   A64W(tmpi, ARM_asrvRegX(0, 0, 1));
      A64StoreRes(tmpi, 0, &tmpi->res, rt);
      return TRUE;
    }
    case IC_SHL_CONST: {
      rt = tmpi->ic_class->raw_type;
      A64LoadArg(tmpi, 0, &tmpi->arg1, rt);
      A64W(tmpi, ARM_lslImmX(0, 0, tmpi->ic_data & 63));
      A64StoreRes(tmpi, 0, &tmpi->res, rt);
      return TRUE;
    }
    case IC_SHR_CONST: {
      rt = tmpi->ic_class->raw_type;
      A64LoadArg(tmpi, 0, &tmpi->arg1, rt);
      if (rt & RTF_UNSIGNED) A64W(tmpi, ARM_lsrImmX(0, 0, tmpi->ic_data & 63));
      else                   A64W(tmpi, ARM_asrImmX(0, 0, tmpi->ic_data & 63));
      A64StoreRes(tmpi, 0, &tmpi->res, rt);
      return TRUE;
    }

    // IC_ABS_I64 — MUST-follow spec, exact order: negsRegX FIRST (so its
    // flags are the ones cselX reads), then cmpImmX, then cselX(...,GE).
    case IC_ABS_I64: {
      A64LoadArg(tmpi, 0, &tmpi->arg1, RT_I64);
      A64W(tmpi, ARM_negsRegX(9, 0));
      A64W(tmpi, ARM_cmpImmX(0, 0));
      A64W(tmpi, ARM_cselX(0, 0, 9, A64CC(IC_GREATER_EQU, FALSE)));
      A64StoreRes(tmpi, 0, &tmpi->res, RT_I64);
      return TRUE;
    }

    // IC_SIGN_I64 — MUST-follow spec exactly: cmp x0,#0; cset x9,GT;
    // cset x10,LT; sub x0,x9,x10.
    case IC_SIGN_I64: {
      A64LoadArg(tmpi, 0, &tmpi->arg1, RT_I64);
      A64W(tmpi, ARM_cmpImmX(0, 0));
      A64W(tmpi, ARM_csetX(9, A64CC(IC_GREATER, FALSE)));
      A64W(tmpi, ARM_csetX(10, A64CC(IC_LESS, FALSE)));
      A64W(tmpi, ARM_subRegX(0, 9, 10));
      A64StoreRes(tmpi, 0, &tmpi->res, RT_I64);
      return TRUE;
    }

    // IC_MIN_I64/U64, IC_MAX_I64/U64 — "cmp+csel with LT/GT (signed)
    // LO/HI (unsigned)"; A64CC's condition map (design section 4) turns
    // IC_LESS/IC_GREATER + the unsigned flag into exactly those codes.
    case IC_MIN_I64: {
      A64LoadArg(tmpi, 0, &tmpi->arg1, RT_I64);
      A64LoadArg(tmpi, 1, &tmpi->arg2, RT_I64);
      A64W(tmpi, ARM_cmpRegX(0, 1));
      A64W(tmpi, ARM_cselX(0, 0, 1, A64CC(IC_LESS, FALSE)));
      A64StoreRes(tmpi, 0, &tmpi->res, RT_I64);
      return TRUE;
    }
    case IC_MIN_U64: {
      A64LoadArg(tmpi, 0, &tmpi->arg1, RT_U64);
      A64LoadArg(tmpi, 1, &tmpi->arg2, RT_U64);
      A64W(tmpi, ARM_cmpRegX(0, 1));
      A64W(tmpi, ARM_cselX(0, 0, 1, A64CC(IC_LESS, TRUE)));
      A64StoreRes(tmpi, 0, &tmpi->res, RT_U64);
      return TRUE;
    }
    case IC_MAX_I64: {
      A64LoadArg(tmpi, 0, &tmpi->arg1, RT_I64);
      A64LoadArg(tmpi, 1, &tmpi->arg2, RT_I64);
      A64W(tmpi, ARM_cmpRegX(0, 1));
      A64W(tmpi, ARM_cselX(0, 0, 1, A64CC(IC_GREATER, FALSE)));
      A64StoreRes(tmpi, 0, &tmpi->res, RT_I64);
      return TRUE;
    }
    case IC_MAX_U64: {
      A64LoadArg(tmpi, 0, &tmpi->arg1, RT_U64);
      A64LoadArg(tmpi, 1, &tmpi->arg2, RT_U64);
      A64W(tmpi, ARM_cmpRegX(0, 1));
      A64W(tmpi, ARM_cselX(0, 0, 1, A64CC(IC_GREATER, TRUE)));
      A64StoreRes(tmpi, 0, &tmpi->res, RT_U64);
      return TRUE;
    }

    // ++/-- family — design row: "Deref lvalue, ldr, addImmX/subImmX #1
    // (scaled by ptr size from ic_class2 — pass 0-2 already scaled), str".
    // IC__PP/IC_DEREF_PP (post-inc of a plain lvalue / through a deref'd
    // pointer) and IC__MM/IC_DEREF_MM (post-dec) share one lowering each —
    // the addressing-mode abstraction in arg1 already captures whichever
    // syntactic form produced the lvalue. Dual-dispatch with float
    // (ICPostIncDec/ICFPostIncDec etc.) is guarded via arg1_type_pointed_to,
    // the field the struct docs say carries the pointee's actual raw_type
    // for these IST_ASSIGN-classified nodes.
    case IC__PP:
    case IC_DEREF_PP: {
      pt = tmpi->arg1_type_pointed_to;
      if (A64_FLOATY(pt)) return FALSE;
      A64Deref(tmpi, 2, &tmpi->arg1);
      A64LdAddr(tmpi, 9, 2, pt);
      A64StoreRes(tmpi, 9, &tmpi->res, pt); //old value is the expression's value
      A64W(tmpi, ARM_addImmX(9, 9, 1));
      A64StAddr(tmpi, 9, 2, pt);
      return TRUE;
    }
    case IC__MM:
    case IC_DEREF_MM: {
      pt = tmpi->arg1_type_pointed_to;
      if (A64_FLOATY(pt)) return FALSE;
      A64Deref(tmpi, 2, &tmpi->arg1);
      A64LdAddr(tmpi, 9, 2, pt);
      A64StoreRes(tmpi, 9, &tmpi->res, pt);
      A64W(tmpi, ARM_subImmX(9, 9, 1));
      A64StAddr(tmpi, 9, 2, pt);
      return TRUE;
    }
    case IC_PP_: {
      pt = tmpi->arg1_type_pointed_to;
      if (A64_FLOATY(pt)) return FALSE;
      A64Deref(tmpi, 2, &tmpi->arg1);
      A64LdAddr(tmpi, 9, 2, pt);
      A64W(tmpi, ARM_addImmX(9, 9, 1));
      A64StAddr(tmpi, 9, 2, pt);
      A64StoreRes(tmpi, 9, &tmpi->res, pt); //new value is the expression's value
      return TRUE;
    }
    case IC_MM_: {
      pt = tmpi->arg1_type_pointed_to;
      if (A64_FLOATY(pt)) return FALSE;
      A64Deref(tmpi, 2, &tmpi->arg1);
      A64LdAddr(tmpi, 9, 2, pt);
      A64W(tmpi, ARM_subImmX(9, 9, 1));
      A64StAddr(tmpi, 9, 2, pt);
      A64StoreRes(tmpi, 9, &tmpi->res, pt);
      return TRUE;
    }

    // Compound assigns (IST_ASSIGN forms) — design row: "A64Deref the
    // lvalue once, load-op-store through x9". arg1 is the lvalue's
    // addressing mode (its pointer, per the struct docs — arg1 itself
    // doesn't carry the pointee's type, arg1_type_pointed_to does); arg2
    // is the already-materialized RHS. res mirrors the new value
    // (IC-HolyC.md: every one of these is "(res)").
    case IC_SHL_EQU: {
      pt = tmpi->arg1_type_pointed_to;
      A64Deref(tmpi, 2, &tmpi->arg1);
      A64LdAddr(tmpi, 9, 2, pt);
      A64LoadArg(tmpi, 1, &tmpi->arg2, pt);
      A64W(tmpi, ARM_lslvRegX(9, 9, 1));
      A64StAddr(tmpi, 9, 2, pt);
      A64StoreRes(tmpi, 9, &tmpi->res, pt);
      return TRUE;
    }
    case IC_SHR_EQU: {
      pt = tmpi->arg1_type_pointed_to;
      A64Deref(tmpi, 2, &tmpi->arg1);
      A64LdAddr(tmpi, 9, 2, pt);
      A64LoadArg(tmpi, 1, &tmpi->arg2, pt);
      if (pt & RTF_UNSIGNED) A64W(tmpi, ARM_lsrvRegX(9, 9, 1));
      else                   A64W(tmpi, ARM_asrvRegX(9, 9, 1));
      A64StAddr(tmpi, 9, 2, pt);
      A64StoreRes(tmpi, 9, &tmpi->res, pt);
      return TRUE;
    }
    case IC_MUL_EQU: {
      pt = tmpi->arg1_type_pointed_to;
      if (A64_FLOATY(pt)) return FALSE; //ICFOpEqu handles the float form
      A64Deref(tmpi, 2, &tmpi->arg1);
      A64LdAddr(tmpi, 9, 2, pt);
      A64LoadArg(tmpi, 1, &tmpi->arg2, pt);
      A64W(tmpi, ARM_mulRegX(9, 9, 1));
      A64StAddr(tmpi, 9, 2, pt);
      A64StoreRes(tmpi, 9, &tmpi->res, pt);
      return TRUE;
    }
    case IC_DIV_EQU: {
      pt = tmpi->arg1_type_pointed_to;
      if (A64_FLOATY(pt)) return FALSE;
      A64Deref(tmpi, 2, &tmpi->arg1);
      A64LdAddr(tmpi, 9, 2, pt);
      A64LoadArg(tmpi, 1, &tmpi->arg2, pt);
      if (pt & RTF_UNSIGNED) A64W(tmpi, ARM_udivX(9, 9, 1));
      else                   A64W(tmpi, ARM_sdivRegX(9, 9, 1));
      A64StAddr(tmpi, 9, 2, pt);
      A64StoreRes(tmpi, 9, &tmpi->res, pt);
      return TRUE;
    }
    case IC_MOD_EQU: {
      pt = tmpi->arg1_type_pointed_to;
      if (A64_FLOATY(pt)) return FALSE; //ICFModEqu handles the float form
      A64Deref(tmpi, 2, &tmpi->arg1);
      A64LdAddr(tmpi, 9, 2, pt);
      A64LoadArg(tmpi, 1, &tmpi->arg2, pt);
      if (pt & RTF_UNSIGNED) A64W(tmpi, ARM_udivX(12, 9, 1));
      else                   A64W(tmpi, ARM_sdivRegX(12, 9, 1));
      A64W(tmpi, ARM_mulRegX(12, 12, 1));
      A64W(tmpi, ARM_subRegX(9, 9, 12));
      A64StAddr(tmpi, 9, 2, pt);
      A64StoreRes(tmpi, 9, &tmpi->res, pt);
      return TRUE;
    }
    case IC_AND_EQU: {
      pt = tmpi->arg1_type_pointed_to;
      A64Deref(tmpi, 2, &tmpi->arg1);
      A64LdAddr(tmpi, 9, 2, pt);
      A64LoadArg(tmpi, 1, &tmpi->arg2, pt);
      A64W(tmpi, ARM_andRegX(9, 9, 1));
      A64StAddr(tmpi, 9, 2, pt);
      A64StoreRes(tmpi, 9, &tmpi->res, pt);
      return TRUE;
    }
    case IC_OR_EQU: {
      pt = tmpi->arg1_type_pointed_to;
      A64Deref(tmpi, 2, &tmpi->arg1);
      A64LdAddr(tmpi, 9, 2, pt);
      A64LoadArg(tmpi, 1, &tmpi->arg2, pt);
      A64W(tmpi, ARM_orrRegX(9, 9, 1));
      A64StAddr(tmpi, 9, 2, pt);
      A64StoreRes(tmpi, 9, &tmpi->res, pt);
      return TRUE;
    }
    case IC_XOR_EQU: {
      pt = tmpi->arg1_type_pointed_to;
      A64Deref(tmpi, 2, &tmpi->arg1);
      A64LdAddr(tmpi, 9, 2, pt);
      A64LoadArg(tmpi, 1, &tmpi->arg2, pt);
      A64W(tmpi, ARM_eorRegX(9, 9, 1));
      A64StAddr(tmpi, 9, 2, pt);
      A64StoreRes(tmpi, 9, &tmpi->res, pt);
      return TRUE;
    }
    case IC_ADD_EQU: {
      pt = tmpi->arg1_type_pointed_to;
      if (A64_FLOATY(pt)) return FALSE; //ICFOpEqu handles the float form
      A64Deref(tmpi, 2, &tmpi->arg1);
      A64LdAddr(tmpi, 9, 2, pt);
      A64LoadArg(tmpi, 1, &tmpi->arg2, pt);
      A64W(tmpi, ARM_addRegX(9, 9, 1));
      A64StAddr(tmpi, 9, 2, pt);
      A64StoreRes(tmpi, 9, &tmpi->res, pt);
      return TRUE;
    }
    case IC_SUB_EQU: {
      pt = tmpi->arg1_type_pointed_to;
      if (A64_FLOATY(pt)) return FALSE;
      A64Deref(tmpi, 2, &tmpi->arg1);
      A64LdAddr(tmpi, 9, 2, pt);
      A64LoadArg(tmpi, 1, &tmpi->arg2, pt);
      A64W(tmpi, ARM_subRegX(9, 9, 1));
      A64StAddr(tmpi, 9, 2, pt);
      A64StoreRes(tmpi, 9, &tmpi->res, pt);
      return TRUE;
    }

    //=======================================================================
    // (d) Bit ops — BACKA64-DESIGN.md 5(d). arg1 = value/lvalue, arg2 =
    // bit index. Bit index is always masked to 0-63 (andImmX, encodable
    // logical imm per design section 5(d)) before use.
    //=======================================================================

    // IC_BT (test only, no memory write): "lsrvRegX(x9, x0, x1);
    // andImmX(x0, x9, 1)". Per IC-HolyC.md, the x86 backend consumes this
    // via FLAGS; our backend has no such cross-node flag-liveness contract
    // (design note: "our BT lowering sets x0 not C"), so the extracted
    // 0/1 bit is written to `res` like every other value-producing node
    // in this file.
    case IC_BT: {
      rt = tmpi->ic_class->raw_type;
      A64LoadArg(tmpi, 0, &tmpi->arg1, rt);
      A64LoadArg(tmpi, 1, &tmpi->arg2, rt);
      A64W(tmpi, ARM_andImmX(1, 1, 63));
      A64W(tmpi, ARM_lsrvRegX(9, 0, 1));
      A64W(tmpi, ARM_andImmX(0, 9, 1));
      A64StoreRes(tmpi, 0, &tmpi->res, rt);
      return TRUE;
    }

    // IC_BTS/BTR/BTC (non-atomic RMW): "A64MovImm(x9,1); lslvRegX(x9,x9,x1);
    // old-bit via lsrv+and ...; then orrRegX/bic(=andRegX of
    // mvnRegX)/eorRegX and store back." x0 holds the value being
    // read-modify-written (consistent with every other lowering in this
    // file); x10 holds the extracted old bit (this file's own register
    // choice — the design names no specific scratch register for it);
    // x9 holds the 1<<bit mask; x11 is BTR's extra complement scratch.
    // The mutated value is written back to arg1's own location, and the
    // pre-mutation bit becomes the node's res.
    case IC_BTS: {
      rt = tmpi->ic_class->raw_type;
      A64LoadArg(tmpi, 0, &tmpi->arg1, rt);
      A64LoadArg(tmpi, 1, &tmpi->arg2, rt);
      A64W(tmpi, ARM_andImmX(1, 1, 63));
      A64MovImm(tmpi, 9, 1);
      A64W(tmpi, ARM_lslvRegX(9, 9, 1));
      A64W(tmpi, ARM_lsrvRegX(10, 0, 1));
      A64W(tmpi, ARM_andImmX(10, 10, 1));
      A64W(tmpi, ARM_orrRegX(0, 0, 9));
      A64StoreRes(tmpi, 0, &tmpi->arg1, rt);
      A64StoreRes(tmpi, 10, &tmpi->res, rt);
      return TRUE;
    }
    case IC_BTR: {
      rt = tmpi->ic_class->raw_type;
      A64LoadArg(tmpi, 0, &tmpi->arg1, rt);
      A64LoadArg(tmpi, 1, &tmpi->arg2, rt);
      A64W(tmpi, ARM_andImmX(1, 1, 63));
      A64MovImm(tmpi, 9, 1);
      A64W(tmpi, ARM_lslvRegX(9, 9, 1));
      A64W(tmpi, ARM_lsrvRegX(10, 0, 1));
      A64W(tmpi, ARM_andImmX(10, 10, 1));
      A64W(tmpi, ARM_mvnRegX(11, 9));
      A64W(tmpi, ARM_andRegX(0, 0, 11));
      A64StoreRes(tmpi, 0, &tmpi->arg1, rt);
      A64StoreRes(tmpi, 10, &tmpi->res, rt);
      return TRUE;
    }
    case IC_BTC: {
      rt = tmpi->ic_class->raw_type;
      A64LoadArg(tmpi, 0, &tmpi->arg1, rt);
      A64LoadArg(tmpi, 1, &tmpi->arg2, rt);
      A64W(tmpi, ARM_andImmX(1, 1, 63));
      A64MovImm(tmpi, 9, 1);
      A64W(tmpi, ARM_lslvRegX(9, 9, 1));
      A64W(tmpi, ARM_lsrvRegX(10, 0, 1));
      A64W(tmpi, ARM_andImmX(10, 10, 1));
      A64W(tmpi, ARM_eorRegX(0, 0, 9));
      A64StoreRes(tmpi, 0, &tmpi->arg1, rt);
      A64StoreRes(tmpi, 10, &tmpi->res, rt);
      return TRUE;
    }

    // IC_LBTS/LBTR/LBTC ("LOCK"-prefixed, atomic): design + task spec,
    // literally: "loop: label; ldaxrX x9,[addr]; modify; stlxrX
    // w11,x9,[addr]; cbnz w11 back". The mask (1<<bit) is computed once
    // in x10 outside the loop (the bit index doesn't change across
    // retries); the old bit is re-extracted from the freshly-reloaded x9
    // on every iteration into x13, so a retry can never report a bit
    // value from a read that didn't win the exclusive store. The
    // backward-branch displacement is computed from `ic_cnt` deltas
    // rather than a hardcoded instruction count, so it stays correct
    // regardless of which op's modify-step (BTR needs one more
    // instruction than BTS/BTC) is in the loop body.
    case IC_LBTS: {
      I64 loop_start;
      rt = tmpi->ic_class->raw_type;
      A64Deref(tmpi, 2, &tmpi->arg1);            //x2 = &lvalue
      A64LoadArg(tmpi, 1, &tmpi->arg2, rt);       //x1 = bit index
      A64W(tmpi, ARM_andImmX(1, 1, 63));
      A64MovImm(tmpi, 10, 1);
      A64W(tmpi, ARM_lslvRegX(10, 10, 1));        //x10 = 1<<bit (loop-invariant)
      loop_start = tmpi->ic_cnt;
      A64W(tmpi, ARM_ldaxrX(9, 2));               //x9 = *addr (exclusive)
      A64W(tmpi, ARM_lsrvRegX(13, 9, 1));
      A64W(tmpi, ARM_andImmX(13, 13, 1));         //x13 = old bit, this attempt
      A64W(tmpi, ARM_orrRegX(9, 9, 10));          //x9 |= mask
      A64W(tmpi, ARM_stlxrX(11, 9, 2));           //w11 = store status
      A64W(tmpi, ARM_cbnz(11, loop_start - tmpi->ic_cnt));
      A64StoreRes(tmpi, 13, &tmpi->res, rt);
      return TRUE;
    }
    case IC_LBTR: {
      rt = tmpi->ic_class->raw_type;
      A64Deref(tmpi, 2, &tmpi->arg1);
      A64LoadArg(tmpi, 1, &tmpi->arg2, rt);
      A64W(tmpi, ARM_andImmX(1, 1, 63));
      A64MovImm(tmpi, 10, 1);
      A64W(tmpi, ARM_lslvRegX(10, 10, 1));
      loop_start = tmpi->ic_cnt;
      A64W(tmpi, ARM_ldaxrX(9, 2));
      A64W(tmpi, ARM_lsrvRegX(13, 9, 1));
      A64W(tmpi, ARM_andImmX(13, 13, 1));
      A64W(tmpi, ARM_mvnRegX(12, 10));            //x12 = ~mask
      A64W(tmpi, ARM_andRegX(9, 9, 12));           //x9 &= ~mask
      A64W(tmpi, ARM_stlxrX(11, 9, 2));
      A64W(tmpi, ARM_cbnz(11, loop_start - tmpi->ic_cnt));
      A64StoreRes(tmpi, 13, &tmpi->res, rt);
      return TRUE;
    }
    case IC_LBTC: {
      rt = tmpi->ic_class->raw_type;
      A64Deref(tmpi, 2, &tmpi->arg1);
      A64LoadArg(tmpi, 1, &tmpi->arg2, rt);
      A64W(tmpi, ARM_andImmX(1, 1, 63));
      A64MovImm(tmpi, 10, 1);
      A64W(tmpi, ARM_lslvRegX(10, 10, 1));
      loop_start = tmpi->ic_cnt;
      A64W(tmpi, ARM_ldaxrX(9, 2));
      A64W(tmpi, ARM_lsrvRegX(13, 9, 1));
      A64W(tmpi, ARM_andImmX(13, 13, 1));
      A64W(tmpi, ARM_eorRegX(9, 9, 10));           //x9 ^= mask
      A64W(tmpi, ARM_stlxrX(11, 9, 2));
      A64W(tmpi, ARM_cbnz(11, loop_start - tmpi->ic_cnt));
      A64StoreRes(tmpi, 13, &tmpi->res, rt);
      return TRUE;
    }

    default:
      return FALSE; //not ours — another family file (or the driver) handles it
  }
}
