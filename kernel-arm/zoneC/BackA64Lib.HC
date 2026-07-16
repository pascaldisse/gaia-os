/* BackA64Lib.HC — operand materialization for HolyC -> aarch64.
 * Contract: kernel-arm/zoneC/BACKA64-DESIGN.md section 4.
 * Arm64Asm.HC encoders return I64 words; A64W appends bytes. */

U0 A64W(CIntermediateCode *tmpi, I64 word)
{
  tmpi->ic_body[tmpi->ic_cnt++] = word.u8[0];
  tmpi->ic_body[tmpi->ic_cnt++] = word.u8[1];
  tmpi->ic_body[tmpi->ic_cnt++] = word.u8[2];
  tmpi->ic_body[tmpi->ic_cnt++] = word.u8[3];
}

U0 A64MovImm(CIntermediateCode *tmpi, I64 rx, I64 val)
{
  I64 i,sh,cnt,first,chunk,inv;
  cnt = 0;
  first = 0;
  for (i = 0; i < 4; i++) {
    chunk = (val >> (i * 16)) & 0xFFFF;
    if (chunk) {
      cnt++;
      first = i;
    }
  }
  if (!cnt) {
    A64W(tmpi, ARM_movzImmX(rx, 0, 0));
    return;
  }
  if (cnt == 1) {
    A64W(tmpi, ARM_movzImmX(rx, (val >> (first * 16)) & 0xFFFF,
          first * 16));
    return;
  }

  cnt = 0;
  first = 0;
  inv = ~val;
  for (i = 0; i < 4; i++) {
    chunk = (inv >> (i * 16)) & 0xFFFF;
    if (chunk) {
      cnt++;
      first = i;
    }
  }
  if (!cnt) {
    A64W(tmpi, ARM_movnImmX(rx, 0, 0));
    return;
  }
  if (cnt == 1) {
    A64W(tmpi, ARM_movnImmX(rx, (inv >> (first * 16)) & 0xFFFF,
          first * 16));
    return;
  }

  first = -1;
  for (i = 0; i < 4; i++) {
    chunk = (val >> (i * 16)) & 0xFFFF;
    if (chunk && first < 0) {
      A64W(tmpi, ARM_movzImmX(rx, chunk, i * 16));
      first = i;
    }
  }
  for (i = 0; i < 4; i++) {
    sh = i * 16;
    chunk = (val >> sh) & 0xFFFF;
    if (chunk && i != first)
      A64W(tmpi, ARM_movkImmX(rx, chunk, sh));
  }
}

I64 A64CC(I64 ic_code, Bool is_unsigned)
{
  switch (ic_code) {
  case IC_BR_ZERO:
  case IC_EQU_EQU:
  case IC_BR_EQU_EQU:
  case IC_BR_EQU_EQU2:
    return 0; // EQ
  case IC_BR_NOT_ZERO:
  case IC_NOT_EQU:
  case IC_BR_NOT_EQU:
  case IC_BR_NOT_EQU2:
    return 1; // NE
  case IC_BR_CARRY:
    return 2; // CS/HS
  case IC_BR_NOT_CARRY:
    return 3; // CC/LO
  case IC_LESS:
  case IC_BR_LESS:
  case IC_BR_LESS2:
    if (is_unsigned)
      return 3; // LO/CC
    else
      return 11; // LT
  case IC_GREATER_EQU:
  case IC_BR_GREATER_EQU:
  case IC_BR_GREATER_EQU2:
    if (is_unsigned)
      return 2; // HS/CS
    else
      return 10; // GE
  case IC_GREATER:
  case IC_BR_GREATER:
  case IC_BR_GREATER2:
    if (is_unsigned)
      return 8; // HI
    else
      return 12; // GT
  case IC_LESS_EQU:
  case IC_BR_LESS_EQU:
  case IC_BR_LESS_EQU2:
    if (is_unsigned)
      return 9; // LS
    else
      return 13; // LE
  default:
    throw('BackA64');
  }
}

//Set by A64FrameEnter (BackA64Br) at each IC_ENTER: rounded local bytes + the
//arg-home area. A64FrameRef maps stack args to [x29 - a64_frame_argbase + 16k].
//Reset to 0 at each OptPass789A64 pass entry; top-level code has no RBP refs.
I64 a64_frame_argbase = 0;

U0 A64FrameRef(I64 *base, I64 *disp)
{//x86 frame ref -> arm64 frame. REG_RBP(5) always means the frame ptr (x86 never
 //allocates RBP as a var reg). Locals (disp<0, PrsVar grows down) map 1:1 below x29.
 //Args [RBP+16+8k] map to their spilled homes at the frame bottom:
 //[x29 - argbase + 16k], where argbase = rounded-locals + arg-home area (set by
 //A64FrameEnter, which spills x0..x_{n-1} into exactly those homes).
  if (*base == REG_RBP) {
    *base = 29;
    if (*disp >= 16)
      *disp = 16 * ((*disp - 16) / 8) - a64_frame_argbase;
  }
}

U0 A64LoadArg(CIntermediateCode *tmpi, I64 rx, CICArg *a, I64 raw_type)
{
  I64 mode,rt,base,index,scale,disp,sz,log,scratch;
  mode = a->type & MDG_MASK;
  rt = raw_type & RTG_MASK;
  disp = a->disp;
  scratch = 9;
  switch (rt) {
  case RT_I8:
  case RT_U8:
    sz = 1; log = 0; break;
  case RT_I16:
  case RT_U16:
    sz = 2; log = 1; break;
  case RT_I32:
  case RT_U32:
    sz = 4; log = 2; break;
  default:
    sz = 8; log = 3;
  }

  switch (mode) {
  case MDF_REG:
    base = a->reg & 0xFF;
    if (rx != base)
      A64W(tmpi, ARM_movRegX(rx, base));
    break;
  case MDF_IMM:
    A64MovImm(tmpi, rx, disp);
    break;
  case MDF_STK:
    A64W(tmpi, ARM_ldrPostImmX(rx, 31, 16));
    break;
  case MDF_RIP_DISP32:
    A64MovImm(tmpi, scratch, disp);
    switch (rt) {
    case RT_I8:  A64W(tmpi, ARM_ldrsbX(rx, scratch, 0)); break;
    case RT_U8:  A64W(tmpi, ARM_ldrbRegImm(rx, scratch, 0)); break;
    case RT_I16: A64W(tmpi, ARM_ldrshX(rx, scratch, 0)); break;
    case RT_U16: A64W(tmpi, ARM_ldrhRegImm(rx, scratch, 0)); break;
    case RT_I32: A64W(tmpi, ARM_ldrswX(rx, scratch, 0)); break;
    case RT_U32: A64W(tmpi, ARM_ldrRegImm(rx, scratch, 0)); break;
    default:     A64W(tmpi, ARM_ldrRegImmX(rx, scratch, 0));
    }
    break;
  case MDF_DISP:
    base = a->reg & 0xFF;
    A64FrameRef(&base, &disp);
    if (0 <= disp && disp <= 4095 * sz && !(disp & (sz - 1))) {
      switch (rt) {
      case RT_I8:  A64W(tmpi, ARM_ldrsbX(rx, base, disp)); break;
      case RT_U8:  A64W(tmpi, ARM_ldrbRegImm(rx, base, disp)); break;
      case RT_I16: A64W(tmpi, ARM_ldrshX(rx, base, disp)); break;
      case RT_U16: A64W(tmpi, ARM_ldrhRegImm(rx, base, disp)); break;
      case RT_I32: A64W(tmpi, ARM_ldrswX(rx, base, disp)); break;
      case RT_U32: A64W(tmpi, ARM_ldrRegImm(rx, base, disp)); break;
      default:     A64W(tmpi, ARM_ldrRegImmX(rx, base, disp));
      }
    } else if (-256 <= disp && disp <= 255 &&
          (rt == RT_I8 || rt == RT_I16 || rt == RT_I32 || sz == 8)) {
      switch (rt) {
      case RT_I8:  A64W(tmpi, ARM_ldursbX(rx, base, disp)); break;
      case RT_I16: A64W(tmpi, ARM_ldurshX(rx, base, disp)); break;
      case RT_I32: A64W(tmpi, ARM_ldurswX(rx, base, disp)); break;
      default:     A64W(tmpi, ARM_ldur(rx, base, disp));
      }
    } else {
      A64MovImm(tmpi, scratch, disp);
      A64W(tmpi, ARM_addRegX(scratch, scratch, base));
      switch (rt) {
      case RT_I8:  A64W(tmpi, ARM_ldrsbX(rx, scratch, 0)); break;
      case RT_U8:  A64W(tmpi, ARM_ldrbRegImm(rx, scratch, 0)); break;
      case RT_I16: A64W(tmpi, ARM_ldrshX(rx, scratch, 0)); break;
      case RT_U16: A64W(tmpi, ARM_ldrhRegImm(rx, scratch, 0)); break;
      case RT_I32: A64W(tmpi, ARM_ldrswX(rx, scratch, 0)); break;
      case RT_U32: A64W(tmpi, ARM_ldrRegImm(rx, scratch, 0)); break;
      default:     A64W(tmpi, ARM_ldrRegImmX(rx, scratch, 0));
      }
    }
    break;
  case MDF_SIB:
    base = a->reg & 0xFF;
    A64FrameRef(&base, &disp);
    index = (a->reg >> 8) & 0x3F;
    scale = (a->reg >> 14) & 3;
    if (scale == 0 || scale == log) {
      switch (rt) {
      case RT_I8:
	if (scale)
	  A64W(tmpi, ARM_ldrsbRegRegShiftX(rx, base, index));
	else
	  A64W(tmpi, ARM_ldrsbRegRegX(rx, base, index));
	break;
      case RT_U8:
	A64W(tmpi, ARM_ldrbRegReg(rx, base, index)); //both ?: arms were identical
	break;
      case RT_I16:
	if (scale)
	  A64W(tmpi, ARM_ldrshRegRegShiftX(rx, base, index));
	else
	  A64W(tmpi, ARM_ldrshRegRegX(rx, base, index));
	break;
      case RT_U16:
	if (scale)
	  A64W(tmpi, ARM_ldrhRegRegShift(rx, base, index));
	else
	  A64W(tmpi, ARM_ldrhRegReg(rx, base, index));
	break;
      case RT_I32:
	if (scale)
	  A64W(tmpi, ARM_ldrswRegRegShiftX(rx, base, index));
	else
	  A64W(tmpi, ARM_ldrswRegRegX(rx, base, index));
	break;
      case RT_U32:
	if (scale)
	  A64W(tmpi, ARM_ldrRegRegShift(rx, base, index));
	else
	  A64W(tmpi, ARM_ldrRegReg(rx, base, index));
	break;
      default:
	if (scale)
	  A64W(tmpi, ARM_ldrRegRegShiftX(rx, base, index));
	else
	  A64W(tmpi, ARM_ldrRegRegX(rx, base, index));
      }
    } else {
      A64W(tmpi, ARM_addShiftRegX(scratch, base, index, scale));
      switch (rt) {
      case RT_I8:  A64W(tmpi, ARM_ldrsbX(rx, scratch, 0)); break;
      case RT_U8:  A64W(tmpi, ARM_ldrbRegImm(rx, scratch, 0)); break;
      case RT_I16: A64W(tmpi, ARM_ldrshX(rx, scratch, 0)); break;
      case RT_U16: A64W(tmpi, ARM_ldrhRegImm(rx, scratch, 0)); break;
      case RT_I32: A64W(tmpi, ARM_ldrswX(rx, scratch, 0)); break;
      case RT_U32: A64W(tmpi, ARM_ldrRegImm(rx, scratch, 0)); break;
      default:     A64W(tmpi, ARM_ldrRegImmX(rx, scratch, 0));
      }
    }
    break;
  default:
    throw('BackA64');
  }
}

// Round 10 — safe binary-op operand load: arg1->x0, arg2->x1 with NO clobber.
// Hazard (slice-4 finding): the naive `A64LoadArg(0,arg1); A64LoadArg(1,arg2)`
// pair loads arg1 into x0 FIRST; if arg2 is a live register temp sitting in x0
// (mem<op>temp, e.g. `off=off+memvar` inline where memvar is arg1), that load
// destroys arg2 before it reaches x1 (observed `ldur x0,[b]; mov x1,x0;
// add x0,x0,x0` -> address=base+base -> fault). Fix: when arg2 lives in x0,
// relocate it to a distinct scratch (x1, or x2 for the crossed dual-temp case)
// BEFORE arg1 overwrites x0. temp<op>mem (arg1 already in x0) stays on the
// straight path -> arg1->x0 is a no-op mov, arg2->x1 loads the memory operand.
U0 A64LoadBinArgs(CIntermediateCode *tmpi, CICArg *a1, CICArg *a2, I64 rt)
{
  I64 a1m,a2m;
  a1m = a1->type & MDG_MASK;
  a2m = a2->type & MDG_MASK;
  if (a2m == MDF_REG && (a2->reg & 0xFF) == 0) {
    if (a1m == MDF_REG && (a1->reg & 0xFF) == 1) {
      //crossed dual-temp (arg1 in x1, arg2 in x0): stage arg2 through x2
      A64LoadArg(tmpi, 2, a2, rt);   //mov x2,x0
      A64LoadArg(tmpi, 0, a1, rt);   //mov x0,x1
      A64W(tmpi, ARM_movRegX(1, 2)); //mov x1,x2
    } else {
      A64LoadArg(tmpi, 1, a2, rt);   //save arg2 to x1 first
      A64LoadArg(tmpi, 0, a1, rt);   //then arg1 -> x0 (arg2 no longer in x0)
    }
  } else {
    A64LoadArg(tmpi, 0, a1, rt);
    A64LoadArg(tmpi, 1, a2, rt);
  }
}

U0 A64StoreRes(CIntermediateCode *tmpi, I64 rx, CICArg *a, I64 raw_type)
{
  I64 mode,rt,base,index,scale,disp,sz,log,scratch;
  mode = a->type & MDG_MASK;
  rt = raw_type & RTG_MASK;
  disp = a->disp;
  scratch = 10;
  switch (rt) {
  case RT_I8:
  case RT_U8:
    sz = 1; log = 0; break;
  case RT_I16:
  case RT_U16:
    sz = 2; log = 1; break;
  case RT_I32:
  case RT_U32:
    sz = 4; log = 2; break;
  default:
    sz = 8; log = 3;
  }

  switch (mode) {
  case MDF_REG:
    base = a->reg & 0xFF;
    if (rx != base)
      A64W(tmpi, ARM_movRegX(base, rx));
    break;
  case MDF_STK:
    A64W(tmpi, ARM_strPreImmX(rx, 31, -16));
    break;
  case MDF_RIP_DISP32:
    A64MovImm(tmpi, scratch, disp);
    switch (sz) {
    case 1: A64W(tmpi, ARM_strbRegImm(rx, scratch, 0)); break;
    case 2: A64W(tmpi, ARM_strhRegImm(rx, scratch, 0)); break;
    case 4: A64W(tmpi, ARM_strRegImm(rx, scratch, 0)); break;
    default:A64W(tmpi, ARM_strRegImmX(rx, scratch, 0));
    }
    break;
  case MDF_DISP:
    base = a->reg & 0xFF;
    A64FrameRef(&base, &disp);
    if (0 <= disp && disp <= 4095 * sz && !(disp & (sz - 1))) {
      switch (sz) {
      case 1: A64W(tmpi, ARM_strbRegImm(rx, base, disp)); break;
      case 2: A64W(tmpi, ARM_strhRegImm(rx, base, disp)); break;
      case 4: A64W(tmpi, ARM_strRegImm(rx, base, disp)); break;
      default:A64W(tmpi, ARM_strRegImmX(rx, base, disp));
      }
    } else if (-256 <= disp && disp <= 255) {
      switch (sz) {
      case 1: A64W(tmpi, ARM_sturb(rx, base, disp)); break;
      case 2: A64W(tmpi, ARM_sturh(rx, base, disp)); break;
      case 4: A64W(tmpi, ARM_sturw(rx, base, disp)); break;
      default:A64W(tmpi, ARM_stur(rx, base, disp));
      }
    } else {
      A64MovImm(tmpi, scratch, disp);
      A64W(tmpi, ARM_addRegX(scratch, scratch, base));
      switch (sz) {
      case 1: A64W(tmpi, ARM_strbRegImm(rx, scratch, 0)); break;
      case 2: A64W(tmpi, ARM_strhRegImm(rx, scratch, 0)); break;
      case 4: A64W(tmpi, ARM_strRegImm(rx, scratch, 0)); break;
      default:A64W(tmpi, ARM_strRegImmX(rx, scratch, 0));
      }
    }
    break;
  case MDF_SIB:
    base = a->reg & 0xFF;
    A64FrameRef(&base, &disp);
    index = (a->reg >> 8) & 0x3F;
    scale = (a->reg >> 14) & 3;
    if (scale == 0 || scale == log) {
      switch (sz) {
      case 1: A64W(tmpi, ARM_strbRegReg(rx, base, index)); break;
      case 2:
	if (scale)
	  A64W(tmpi, ARM_strhRegRegShift(rx, base, index));
	else
	  A64W(tmpi, ARM_strhRegReg(rx, base, index));
	break;
      case 4:
	if (scale)
	  A64W(tmpi, ARM_strRegRegShift(rx, base, index));
	else
	  A64W(tmpi, ARM_strRegReg(rx, base, index));
	break;
      default:
	if (scale)
	  A64W(tmpi, ARM_strRegRegShiftX(rx, base, index));
	else
	  A64W(tmpi, ARM_strRegRegX(rx, base, index));
      }
    } else {
      A64W(tmpi, ARM_addShiftRegX(scratch, base, index, scale));
      switch (sz) {
      case 1: A64W(tmpi, ARM_strbRegImm(rx, scratch, 0)); break;
      case 2: A64W(tmpi, ARM_strhRegImm(rx, scratch, 0)); break;
      case 4: A64W(tmpi, ARM_strRegImm(rx, scratch, 0)); break;
      default:A64W(tmpi, ARM_strRegImmX(rx, scratch, 0));
      }
    }
    break;
  case MDF_NULL:
    break; //unused result (ICF_RES_NOT_USED): storing to no destination is a no-op
  default:
    throw('BackA64');
  }
}

U0 A64Deref(CIntermediateCode *tmpi, I64 rx, CICArg *a)
{
  I64 mode,base,index,scale,disp;
  mode = a->type & MDG_MASK;
  disp = a->disp;
  switch (mode) {
  case MDF_REG:
    base = a->reg & 0xFF;
    if (rx != base)
      A64W(tmpi, ARM_movRegX(rx, base));
    break;
  case MDF_DISP:
    base = a->reg & 0xFF;
    A64FrameRef(&base, &disp);
    if (0 <= disp && disp <= 4095)
      A64W(tmpi, ARM_addImmX(rx, base, disp));
    else {
      A64MovImm(tmpi, rx, disp);
      A64W(tmpi, ARM_addRegX(rx, rx, base));
    }
    break;
  case MDF_SIB:
    base = a->reg & 0xFF;
    A64FrameRef(&base, &disp);
    index = (a->reg >> 8) & 0x3F;
    scale = (a->reg >> 14) & 3;
    A64W(tmpi, ARM_addShiftRegX(rx, base, index, scale));
    break;
  case MDF_RIP_DISP32:
    A64MovImm(tmpi, rx, disp);
    break;
  default:
    throw('BackA64');
  }
}

U0 A64LoadArgF(CIntermediateCode *tmpi, I64 dreg, CICArg *a)
{
  I64 mode,base,index,scale,disp;
  mode = a->type & MDG_MASK;
  disp = a->disp;
  switch (mode) {
  case MDF_REG:
    A64W(tmpi, ARM_fmovF64I64(dreg, a->reg & 0xFF));
    break;
  case MDF_STK:
    A64W(tmpi, ARM_ldrPostImmX(9, 31, 16));
    A64W(tmpi, ARM_fmovF64I64(dreg, 9));
    break;
  case MDF_RIP_DISP32:
    A64MovImm(tmpi, 9, disp);
    A64W(tmpi, ARM_ldrRegImmF64(dreg, 9, 0));
    break;
  case MDF_DISP:
    base = a->reg & 0xFF;
    A64FrameRef(&base, &disp);
    if (0 <= disp && disp <= 32760 && !(disp & 7))
      A64W(tmpi, ARM_ldrRegImmF64(dreg, base, disp));
    else {
      A64MovImm(tmpi, 9, disp);
      A64W(tmpi, ARM_addRegX(9, 9, base));
      A64W(tmpi, ARM_ldrRegImmF64(dreg, 9, 0));
    }
    break;
  case MDF_SIB:
    base = a->reg & 0xFF;
    A64FrameRef(&base, &disp);
    index = (a->reg >> 8) & 0x3F;
    scale = (a->reg >> 14) & 3;
    if (scale == 0)
      A64W(tmpi, ARM_ldrRegRegF64(dreg, base, index));
    else if (scale == 3)
      A64W(tmpi, ARM_ldrRegRegShiftF64(dreg, base, index));
    else {
      A64W(tmpi, ARM_addShiftRegX(9, base, index, scale));
      A64W(tmpi, ARM_ldrRegImmF64(dreg, 9, 0));
    }
    break;
  default:
    throw('BackA64');
  }
}

U0 A64StoreResF(CIntermediateCode *tmpi, I64 dreg, CICArg *a)
{
  I64 mode,base,index,scale,disp;
  mode = a->type & MDG_MASK;
  disp = a->disp;
  switch (mode) {
  case MDF_REG:
    A64W(tmpi, ARM_fmovI64F64(a->reg & 0xFF, dreg));
    break;
  case MDF_STK:
    A64W(tmpi, ARM_fmovI64F64(9, dreg));
    A64W(tmpi, ARM_strPreImmX(9, 31, -16));
    break;
  case MDF_RIP_DISP32:
    A64MovImm(tmpi, 10, disp);
    A64W(tmpi, ARM_strRegImmF64(dreg, 10, 0));
    break;
  case MDF_DISP:
    base = a->reg & 0xFF;
    A64FrameRef(&base, &disp);
    if (0 <= disp && disp <= 32760 && !(disp & 7))
      A64W(tmpi, ARM_strRegImmF64(dreg, base, disp));
    else {
      A64MovImm(tmpi, 10, disp);
      A64W(tmpi, ARM_addRegX(10, 10, base));
      A64W(tmpi, ARM_strRegImmF64(dreg, 10, 0));
    }
    break;
  case MDF_SIB:
    base = a->reg & 0xFF;
    A64FrameRef(&base, &disp);
    index = (a->reg >> 8) & 0x3F;
    scale = (a->reg >> 14) & 3;
    if (scale == 0)
      A64W(tmpi, ARM_strRegRegF64(dreg, base, index));
    else if (scale == 3)
      A64W(tmpi, ARM_strRegRegShiftF64(dreg, base, index));
    else {
      A64W(tmpi, ARM_addShiftRegX(10, base, index, scale));
      A64W(tmpi, ARM_strRegImmF64(dreg, 10, 0));
    }
    break;
  default:
    throw('BackA64');
  }
}
