// BackA64Br.HC — aarch64 branch/call/frame lowering for GAIA-OS HolyC.
// Implements BACKA64-DESIGN.md sections 3, 5(e), 5(f), and 6.

#define A64_CC_EQ 0
#define A64_CC_NE 1
#define A64_CC_CS 2
#define A64_CC_CC 3
#define A64_CC_HI 8
#define A64_CC_LS 9
#define A64_CC_GE 10
#define A64_CC_LT 11
#define A64_CC_GT 12
#define A64_CC_LE 13

#define A64_BR_AND_ZERO_CODE 0x95
#define A64_BR_AND_NOT_ZERO_CODE 0x96
#define A64_BR_MM_ZERO_CODE 0x97
#define A64_BR_MM_NOT_ZERO_CODE 0x98
#define A64_BR_BT_ZERO_CODE 0x9D
#define A64_BR_BT_NOT_ZERO_CODE 0x9E
#define A64_BR_BTS_ZERO_CODE 0x9F
#define A64_BR_BTS_NOT_ZERO_CODE 0xA0
#define A64_BR_BTR_ZERO_CODE 0xA1
#define A64_BR_BTR_NOT_ZERO_CODE 0xA2
#define A64_BR_BTC_ZERO_CODE 0xA3
#define A64_BR_BTC_NOT_ZERO_CODE 0xA4

Bool A64IsUnsignedRaw(I64 rt) {
  return rt == RT_U8 || rt == RT_U16 || rt == RT_U32 || rt == RT_U64;
}

Bool A64BrFits(I64 delta, I64 bits) {
  I64 q, lo, hi;
  if (delta & 3) return FALSE;
  q = delta >> 2;
  lo = -(1 << (bits - 1));
  hi = (1 << (bits - 1)) - 1;
  return lo <= q && q <= hi;
}

I64 A64InvertCond(I64 cond) {
  return cond ^ 1;
}

U0 A64EmitB(CIntermediateCode *tmpi, I64 delta) {
  A64W(tmpi, ARM_b(delta));
}

U0 A64EmitBcc(CIntermediateCode *tmpi, I64 cond, I64 delta) {
  if (A64BrFits(delta, 19)) {
    A64W(tmpi, ARM_bcc(cond, delta));
  } else {
    A64W(tmpi, ARM_bcc(A64InvertCond(cond), 8));
    A64W(tmpi, ARM_b(delta - 4));
  }
}

U0 A64EmitCbz(CIntermediateCode *tmpi, I64 r, Bool not_zero, I64 delta) {
  if (A64BrFits(delta, 19)) {
    if (not_zero)
      A64W(tmpi, ARM_cbnzX(r, delta));
    else
      A64W(tmpi, ARM_cbzX(r, delta));
  } else {
    if (not_zero)
      A64W(tmpi, ARM_cbzX(r, 8));
    else
      A64W(tmpi, ARM_cbnzX(r, 8));
    A64W(tmpi, ARM_b(delta - 4));
  }
}

U0 A64EmitTbz(CIntermediateCode *tmpi, I64 r, I64 bit, Bool not_zero, I64 delta) {
  if (A64BrFits(delta, 14)) {
    if (not_zero)
      A64W(tmpi, ARM_tbnz(r, bit, delta));
    else
      A64W(tmpi, ARM_tbz(r, bit, delta));
  } else {
    if (not_zero)
      A64W(tmpi, ARM_tbz(r, bit, 8));
    else
      A64W(tmpi, ARM_tbnz(r, bit, 8));
    A64W(tmpi, ARM_b(delta - 4));
  }
}

I64 A64CmpCodeForBr2(I64 code) {
  return IC_BR_EQU_EQU + (code - IC_BR_EQU_EQU2); //2-form (0x8F-0x94) -> base (0x89-0x8E)
}

I64 A64FrameLocals(CIntermediateCode *tmpi) {
  return tmpi->ic_data & 0xFFFFFFFF;
}

I64 A64FrameMask(CIntermediateCode *tmpi) {
  return tmpi->ic_data >> 32;
}

U0 A64SubSP(CIntermediateCode *tmpi, I64 bytes) {
  //Register-form add/sub treats rg 31 as XZR, not SP —
  //only the immediate form addresses SP. Chunk large frames.
  if (!bytes) return;
  while (bytes > 4095) {
    A64W(tmpi, ARM_subImmX(31, 31, 4095));
    bytes -= 4095;
  }
  if (bytes)
    A64W(tmpi, ARM_subImmX(31, 31, bytes));
}

U0 A64AddSP(CIntermediateCode *tmpi, I64 bytes) {
  if (!bytes) return;
  while (bytes > 4095) {
    A64W(tmpi, ARM_addImmX(31, 31, 4095));
    bytes -= 4095;
  }
  if (bytes)
    A64W(tmpi, ARM_addImmX(31, 31, bytes));
}

U0 A64PushMask(CIntermediateCode *tmpi, I64 mask) {
  I64 r, r2;
  r = 0;
  while (r < 31) {
    if (mask & (1 << r)) {
      r2 = r + 1;
      while (r2 < 31 && !(mask & (1 << r2))) r2++;
      if (r2 < 31) {
        A64W(tmpi, ARM_stpPreImmX(r, r2, 31, -16));
        mask &= ~(1 << r);
        mask &= ~(1 << r2);
      } else {
        A64W(tmpi, ARM_strPreImmX(r, 31, -8));
        mask &= ~(1 << r);
      }
    }
    r++;
  }
}

U0 A64PopMask(CIntermediateCode *tmpi, I64 mask) {
  I64 r, r2;
  r = 30;
  while (r >= 0) {
    if (mask & (1 << r)) {
      r2 = r - 1;
      while (r2 >= 0 && !(mask & (1 << r2))) r2--;
      if (r2 >= 0) {
        A64W(tmpi, ARM_ldpPostImmX(r2, r, 31, 16));
        mask &= ~(1 << r);
        mask &= ~(1 << r2);
      } else {
        A64W(tmpi, ARM_ldrPostImmX(r, 31, 8));
        mask &= ~(1 << r);
      }
    }
    r--;
  }
}

U0 A64EmitCallAbs(CIntermediateCode *tmpi, I64 addr) {
  A64W(tmpi, ARM_movzImmX(16, addr & 0xFFFF, 0));
  A64W(tmpi, ARM_movkImmX(16, (addr >> 16) & 0xFFFF, 16));
  A64W(tmpi, ARM_movkImmX(16, (addr >> 32) & 0xFFFF, 32));
  A64W(tmpi, ARM_movkImmX(16, (addr >> 48) & 0xFFFF, 48));
  A64W(tmpi, ARM_blr(16));
}

U0 A64LoadCmpArgs(CIntermediateCode *tmpi, I64 rt) {
  A64LoadArg(tmpi, 0, &tmpi->arg1, rt);
  A64LoadArg(tmpi, 1, &tmpi->arg2, rt);
  A64W(tmpi, ARM_cmpRegX(0, 1));
}

U0 A64EmitCmpBr(CIntermediateCode *tmpi, I64 cmp_code, I64 delta) {
  I64 rt;
  rt = tmpi->ic_class->raw_type;
  A64LoadCmpArgs(tmpi, rt);
  A64EmitBcc(tmpi, A64CC(cmp_code, A64IsUnsignedRaw(rt)), delta);
}

U0 A64EmitCmpChain(CIntermediateCode *tmpi, I64 cmp_code) {
  I64 rt;
  rt = tmpi->ic_class->raw_type;
  A64LoadCmpArgs(tmpi, rt);
  A64W(tmpi, ARM_csetX(0, A64CC(cmp_code, A64IsUnsignedRaw(rt))));
  A64W(tmpi, ARM_ldrPostImmX(9, 31, 8));
  A64W(tmpi, ARM_andRegX(0, 0, 9));
  A64StoreRes(tmpi, 0, &tmpi->res, rt);
}

U0 A64EmitBitBranch(CIntermediateCode *tmpi, Bool rmw, I64 op, Bool not_zero, I64 delta) {
  A64LoadArg(tmpi, 0, &tmpi->arg1, RT_U64);
  if (!rmw && (tmpi->arg2.type & MDF_IMM)) {
    A64EmitTbz(tmpi, 0, tmpi->arg2.disp & 63, not_zero, delta);
    return;
  }
  A64LoadArg(tmpi, 1, &tmpi->arg2, RT_U64);
  A64W(tmpi, ARM_andImmX(1, 1, 63));
  A64W(tmpi, ARM_lsrvRegX(2, 0, 1));
  A64W(tmpi, ARM_andImmX(2, 2, 1));
  if (rmw) {
    A64Deref(tmpi, 9, &tmpi->arg1);
    A64W(tmpi, ARM_ldrRegImmX(0, 9, 0));
    A64MovImm(tmpi, 10, 1);
    A64W(tmpi, ARM_lslvRegX(10, 10, 1));
    if (op == 1)
      A64W(tmpi, ARM_orrRegX(0, 0, 10));
    else if (op == 2) {
      A64W(tmpi, ARM_mvnRegX(10, 10));
      A64W(tmpi, ARM_andRegX(0, 0, 10));
    } else if (op == 3)
      A64W(tmpi, ARM_eorRegX(0, 0, 10));
    A64W(tmpi, ARM_strRegImmX(0, 9, 0));
  }
  A64EmitCbz(tmpi, 2, not_zero, delta);
}

Bool A64GenBr(CCmpCtrl *cc, CIntermediateCode *tmpi) {
  I64 rt, delta, argc, addr;
  delta = tmpi->ic_data;
  switch (tmpi->ic_code) {
    case IC_JMP: //Shrine 0x51, unconditional
      A64EmitB(tmpi, delta);
      return TRUE;

    case IC_BR_ZERO:
      rt = tmpi->ic_class->raw_type;
      A64LoadArg(tmpi, 0, &tmpi->arg1, rt);
      A64EmitCbz(tmpi, 0, FALSE, delta);
      return TRUE;

    case IC_BR_NOT_ZERO:
      rt = tmpi->ic_class->raw_type;
      A64LoadArg(tmpi, 0, &tmpi->arg1, rt);
      A64EmitCbz(tmpi, 0, TRUE, delta);
      return TRUE;

    case IC_BR_EQU_EQU:
    case IC_BR_NOT_EQU:
    case IC_BR_LESS:
    case IC_BR_GREATER_EQU:
    case IC_BR_GREATER:
    case IC_BR_LESS_EQU:
      A64EmitCmpBr(tmpi, tmpi->ic_code, delta);
      return TRUE;

    case IC_BR_EQU_EQU2:
    case IC_BR_NOT_EQU2:
    case IC_BR_LESS2:
    case IC_BR_GREATER_EQU2:
    case IC_BR_GREATER2:
    case IC_BR_LESS_EQU2:
      A64EmitCmpChain(tmpi, A64CmpCodeForBr2(tmpi->ic_code));
      return TRUE;

    case IC_BR_AND_ZERO:
      rt = tmpi->ic_class->raw_type;
      A64LoadArg(tmpi, 0, &tmpi->arg1, rt);
      A64LoadArg(tmpi, 1, &tmpi->arg2, rt);
      A64W(tmpi, ARM_andsRegX(9, 0, 1));
      A64EmitBcc(tmpi, A64_CC_EQ, delta);
      return TRUE;

    case IC_BR_AND_NOT_ZERO:
      rt = tmpi->ic_class->raw_type;
      A64LoadArg(tmpi, 0, &tmpi->arg1, rt);
      A64LoadArg(tmpi, 1, &tmpi->arg2, rt);
      A64W(tmpi, ARM_andsRegX(9, 0, 1));
      A64EmitBcc(tmpi, A64_CC_NE, delta);
      return TRUE;

    case A64_BR_MM_ZERO_CODE:
      rt = tmpi->ic_class->raw_type;
      A64Deref(tmpi, 9, &tmpi->arg1);
      A64W(tmpi, ARM_ldrRegImmX(0, 9, 0));
      A64W(tmpi, ARM_subImmX(0, 0, 1));
      A64W(tmpi, ARM_strRegImmX(0, 9, 0));
      A64EmitCbz(tmpi, 0, FALSE, delta);
      return TRUE;

    case A64_BR_MM_NOT_ZERO_CODE:
      rt = tmpi->ic_class->raw_type;
      A64Deref(tmpi, 9, &tmpi->arg1);
      A64W(tmpi, ARM_ldrRegImmX(0, 9, 0));
      A64W(tmpi, ARM_subImmX(0, 0, 1));
      A64W(tmpi, ARM_strRegImmX(0, 9, 0));
      A64EmitCbz(tmpi, 0, TRUE, delta);
      return TRUE;

    case A64_BR_BT_ZERO_CODE:
      A64EmitBitBranch(tmpi, FALSE, 0, FALSE, delta);
      return TRUE;
    case A64_BR_BT_NOT_ZERO_CODE:
      A64EmitBitBranch(tmpi, FALSE, 0, TRUE, delta);
      return TRUE;
    case A64_BR_BTS_ZERO_CODE:
      A64EmitBitBranch(tmpi, TRUE, 1, FALSE, delta);
      return TRUE;
    case A64_BR_BTS_NOT_ZERO_CODE:
      A64EmitBitBranch(tmpi, TRUE, 1, TRUE, delta);
      return TRUE;
    case A64_BR_BTR_ZERO_CODE:
      A64EmitBitBranch(tmpi, TRUE, 2, FALSE, delta);
      return TRUE;
    case A64_BR_BTR_NOT_ZERO_CODE:
      A64EmitBitBranch(tmpi, TRUE, 2, TRUE, delta);
      return TRUE;
    case A64_BR_BTC_ZERO_CODE:
      A64EmitBitBranch(tmpi, TRUE, 3, FALSE, delta);
      return TRUE;
    case A64_BR_BTC_NOT_ZERO_CODE:
      A64EmitBitBranch(tmpi, TRUE, 3, TRUE, delta);
      return TRUE;

    case IC_PUSH_CMP:
      A64W(tmpi, ARM_strPreImmX(0, 31, -8));
      return TRUE;

    case IC_CALL:
      addr = tmpi->arg1.disp;
      if (delta && A64BrFits(delta, 26))
        A64W(tmpi, ARM_bl(delta));
      else
        A64EmitCallAbs(tmpi, addr);
      return TRUE;

    case IC_RET:
      argc = tmpi->ic_data * 8;
      A64AddSP(tmpi, argc);
      if (tmpi->ic_flags & Ff_INTERRUPT)
        A64W(tmpi, ARM_eret());
      else
        A64W(tmpi, ARM_ret());
      return TRUE;

    case IC_ENTER:
      A64W(tmpi, ARM_strPreImmX(30, 31, -8));
      A64W(tmpi, ARM_strPreImmX(29, 31, -8));
      A64W(tmpi, ARM_addImmX(29, 31, 0));
      A64SubSP(tmpi, A64FrameLocals(tmpi));
      A64PushMask(tmpi, A64FrameMask(tmpi));
      return TRUE;

    case IC_LEAVE:
      A64PopMask(tmpi, A64FrameMask(tmpi));
      A64W(tmpi, ARM_addImmX(31, 29, 0));
      A64W(tmpi, ARM_ldrPostImmX(29, 31, 8));
      A64W(tmpi, ARM_ldrPostImmX(30, 31, 8));
      return TRUE;

    case IC_PUSH:
      rt = tmpi->ic_class->raw_type;
      A64LoadArg(tmpi, 0, &tmpi->arg1, rt);
      A64W(tmpi, ARM_strPreImmX(0, 31, -8));
      return TRUE;

    case IC_POP:
      rt = tmpi->ic_class->raw_type;
      A64W(tmpi, ARM_ldrPostImmX(0, 31, 8));
      A64StoreRes(tmpi, 0, &tmpi->res, rt);
      return TRUE;

    case IC_PUSH_REGS:
      A64PushMask(tmpi, tmpi->ic_data);
      return TRUE;

    case IC_POP_REGS:
      A64PopMask(tmpi, tmpi->ic_data);
      return TRUE;
  }
  return FALSE;
}
