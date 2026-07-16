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

//Set by OptPass789A64 at each pass entry: output buffer base for
//IC_STR_CONST/IC_GET_LABEL absolute-address materialization.
I64 a64_str_buf = 0;

//aiwnios host ABI passes args in x0-x7 (arm_backend.c __ICFCallTOS) while
//Terry streams push args on the stack. IC_PUSH counts arg slots, IC_CALL
//marshals them into regs. Reset at each pass entry by OptPass789A64
//(pass-size stability); IC_CALL_START saves counts across nested calls.
I64 a64_arg_cnt = 0;
I64 a64_arg_sp = 0;
I64 a64_arg_stk[16];

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
        A64W(tmpi, ARM_strPreImmX(r, 31, -16));
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
        A64W(tmpi, ARM_ldrPostImmX(r, 31, 16));
        mask &= ~(1 << r);
      }
    }
    r--;
  }
}

U0 A64FrameEnter(CCmpCtrl *cc, CIntermediateCode *tmpi) {
  //Callee prologue. Save fp/lr, set x29, allocate locals + arg-home area, then
  //spill the incoming register args x0..x_{n-1} into their frame homes. With
  //reg-opt disabled (mkboot NoRegOpt), the Shrine front end reads args from
  //stack slots [RBP+16+8k]; our callers pass args in x0..x7, so the callee must
  //stash them where the front end expects. Homes sit at the frame bottom
  //([sp+16k]); A64FrameRef maps [RBP+16+8k] -> [x29 - argbase + 16k] to match.
  I64 nargs, argsz, locals, k;
  A64W(tmpi, ARM_strPreImmX(30, 31, -16));
  A64W(tmpi, ARM_strPreImmX(29, 31, -16));
  A64W(tmpi, ARM_addImmX(29, 31, 0));         //mov x29, sp
  locals = (A64FrameLocals(tmpi) + 15) & ~15; //ic_data=-fun->size = positive local bytes
  nargs = 0;
  if (cc->htc.fun)
    nargs = cc->htc.fun->arg_cnt;
  if (nargs > 8) nargs = 8;                   //only x0..x7 carry register args
  argsz = nargs * 16;                         //16B home per arg (bug#7c slot size)
  a64_frame_argbase = locals + argsz;         //A64FrameRef: [RBP+16+8k] -> [x29 - argbase + 16k]
  A64SubSP(tmpi, locals + argsz);             //16-aligned: locals rounded, argsz already 16*n
  for (k = 0; k < nargs; k++)                 //spill x_k -> [sp + 16k] (its frame home)
    A64W(tmpi, ARM_strRegImmX(k, 31, k * 16));
  A64PushMask(tmpi, A64FrameMask(tmpi));
}

U0 A64EmitCallAbs(CIntermediateCode *tmpi, I64 addr) {
  A64W(tmpi, ARM_movzImmX(16, addr & 0xFFFF, 0));
  A64W(tmpi, ARM_movkImmX(16, (addr >> 16) & 0xFFFF, 16));
  A64W(tmpi, ARM_movkImmX(16, (addr >> 32) & 0xFFFF, 32));
  A64W(tmpi, ARM_movkImmX(16, (addr >> 48) & 0xFFFF, 48));
  A64W(tmpi, ARM_blr(16));
}

//C4 IC_CALL_INDIRECT2: the callee address lives IN the slot at addr
//(Terry x86: mov rbx,imm64; call [rbx] — OptPass789A.HC:345). Deref at
//call time so JIT patching of exe_addr stays live. Fixed emission size
//per node (pass-stable while addresses settle), same rule as CallAbs.
U0 A64EmitCallSlot(CIntermediateCode *tmpi, I64 addr) {
  A64W(tmpi, ARM_movzImmX(16, addr & 0xFFFF, 0));
  A64W(tmpi, ARM_movkImmX(16, (addr >> 16) & 0xFFFF, 16));
  A64W(tmpi, ARM_movkImmX(16, (addr >> 32) & 0xFFFF, 32));
  A64W(tmpi, ARM_movkImmX(16, (addr >> 48) & 0xFFFF, 48));
  A64W(tmpi, ARM_ldrRegImmX(16, 16, 0));
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
  A64W(tmpi, ARM_ldrPostImmX(9, 31, 16));
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
  I64 rt, delta, argc, addr, i;
  CCodeMisc *lb;
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
      A64W(tmpi, ARM_strPreImmX(0, 31, -16));
      return TRUE;

    case IC_CALL_INDIRECT: //callee ptr in a stack slot; shares the marshal too
    case IC_CALL_INDIRECT2: //callee = *ic_data slot; only the callee emit differs
    case IC_CALL:
      //Terry OptPass789A.HC:323 — ic_data IS the absolute callee address.
      //aiwnios host ABI adapter (arm_backend.c __ICFCallTOS): callees take
      //args in x0-x7, but the Terry stream passed them on the stack. Load
      //the slots into regs — top of stack = first arg (observed order:
      //variadic argc pushed first, landing in x1 = aiwnios argc position).
      //bug#8 variadic: the stage-time PrsExp transform (mkboot.sh) stashes
      //F+1 in ic_flags high bits ((F+1)<<ICF_A64_VARG_SHIFT, ShrineHost.HH)
      //of DOT_DOT_DOT IC_CALLs — passes 0-6 scrub arg2 in place but touch
      //ic_flags only via low ICF bits (smoke76/77). Terry slot layout here:
      //slots 0..F-1 fixed args, slot F = vararg count (PrsExp argc imm),
      //slots F+1.. = varargs, first vararg lowest, 16B stride (bug#7c).
      //Aiwnios variadic callees (parser.c CLSF_VARGS, arm_backend.c
      //__IC_VARGS) take (fixed...,argc,argv) as plain reg args, argv an
      //8B-stride array: compact vararg slots to 8B in place (lower halves
      //are dead after this marshal), then x_{F+1} = &slot(F+1). Marker 0
      //(non-variadic: no high bits) takes the plain branch; range-guarded.
      argc = a64_arg_cnt;
      if (argc > 8) argc = 8;
      delta = (tmpi->ic_flags>>ICF_A64_VARG_SHIFT)&ICF_A64_VARG_MASK;
      if (0 < delta && delta <= 8 && a64_arg_cnt >= delta) {
        rt = a64_arg_cnt - delta; //vararg count
        for (i = 0; i < rt; i++) {
          A64W(tmpi, ARM_ldrRegImmX(9, 31, (delta + i) * 16));
          A64W(tmpi, ARM_strRegImmX(9, 31, delta * 16 + i * 8));
        }
        for (i = 0; i < delta; i++) //x0..x_F: fixed args + vararg count
          A64W(tmpi, ARM_ldrRegImmX(i, 31, i * 16));
        A64W(tmpi, ARM_addImmX(delta, 31, delta * 16)); //x_{F+1} = argv
      } else {
        for (i = 0; i < argc; i++)
          A64W(tmpi, ARM_ldrRegImmX(i, 31, i * 16)); //16B slots (bug#7c: Apple SP-SA)
      }
      //blr clobbers x30 — save/restore so IC_RET returns to OUR caller.
      //Fixed emission size per argc: pass-stable while addresses settle.
      A64W(tmpi, ARM_strPreImmX(30, 31, -16));
      if (tmpi->ic_code==IC_CALL)
        A64EmitCallAbs(tmpi, tmpi->ic_data);
      else if (tmpi->ic_code==IC_CALL_INDIRECT2) //deref the slot at call time
        A64EmitCallSlot(tmpi, tmpi->ic_data);
      else {
        //IC_CALL_INDIRECT: Terry CALL disp[RSP] (OptPass789A.HC:723). ic_data
        //= (total arg slots)<<3 from parse (PrsExp.HC:558). IC_PUSH_REGS put
        //the fn ptr BELOW the args and does not bump a64_arg_cnt, so slot
        //index ic_data>>3 IS the ptr: 8B->16B slots = *2 (bug#7c), +16 for
        //the x30 push just above. Variadic-through-ptr: no bug#8 marker yet.
        A64W(tmpi, ARM_ldrRegImmX(16, 31, tmpi->ic_data*2+16));
        A64W(tmpi, ARM_blr(16));
      }
      A64W(tmpi, ARM_ldrPostImmX(30, 31, 16));
      if (a64_arg_sp)
        a64_arg_cnt = a64_arg_stk[--a64_arg_sp];
      else
        a64_arg_cnt = 0;
      return TRUE;

    case IC_RET:
      argc = tmpi->ic_data * 16; //16B slots (bug#7c)
      A64AddSP(tmpi, argc);
      if (tmpi->ic_flags & Ff_INTERRUPT)
        A64W(tmpi, ARM_eret());
      else
        A64W(tmpi, ARM_ret());
      return TRUE;

    case IC_ENTER:
      A64FrameEnter(cc, tmpi); //prologue + spill reg args to frame homes (bug#10b)
      return TRUE;

    case IC_LEAVE:
      A64PopMask(tmpi, A64FrameMask(tmpi));
      A64W(tmpi, ARM_addImmX(31, 29, 0));
      A64W(tmpi, ARM_ldrPostImmX(29, 31, 16));
      A64W(tmpi, ARM_ldrPostImmX(30, 31, 16));
      //x86 LEAVE's RET/RET1 lives inside IC_LEAVE: PrsFun never ICAdds an
      //IC_RET, so the epilogue must emit the return itself or execution
      //falls off the end of the buffer (smoke83's exec5 fault). Terry's
      //RET1 convention: callee pops its own args (Ff_RET1 set by
      //PrsFunJoin when 0<arg_cnt<<3<=I16_MAX and not variadic). Our
      //IC_PUSH slots are 16B (bug#7c), so pop arg_cnt*16, not <<3.
  //bug#7d RESOLVED: callers clean args (IC_ADD_RSP1 emits a real A64AddSP at every call site), so the callee must NOT pop. x86 inverted this (RET1 popped, ADD_RSP1 was a no-op).
      A64W(tmpi, ARM_ret());
      return TRUE;

    case IC_PUSH:
      rt = tmpi->ic_class->raw_type;
      A64LoadArg(tmpi, 0, &tmpi->arg1, rt);
      A64W(tmpi, ARM_strPreImmX(0, 31, -16));
      a64_arg_cnt++; //arg slot for the next IC_CALL's register marshal
      return TRUE;

    case IC_POP:
      rt = tmpi->ic_class->raw_type;
      A64W(tmpi, ARM_ldrPostImmX(0, 31, 16));
      A64StoreRes(tmpi, 0, &tmpi->res, rt);
      return TRUE;

    case IC_CALL_START: //Terry OptPass789A.HC:1022 — push regs, open arg count
      if (a64_arg_sp < 16)
        a64_arg_stk[a64_arg_sp++] = a64_arg_cnt;
      a64_arg_cnt = 0;
      A64PushMask(tmpi, tmpi->ic_data);
      return TRUE;

    case IC_PUSH_REGS:
      A64PushMask(tmpi, tmpi->ic_data);
      return TRUE;

    case IC_POP_REGS:
      A64PopMask(tmpi, tmpi->ic_data);
      return TRUE;

    case IC_CALL_END: //Terry OptPass789A.HC:1026 — pop regs, res <- return reg
      A64PopMask(tmpi, tmpi->ic_data);
      if (tmpi->res.type.mode)
        A64StoreRes(tmpi, 0, &tmpi->res, RT_I64);
      return TRUE;

    case IC_CALL_END2:    //Terry OptPass789A.HC:1036 — no emission
    case IC_RETURN_VAL:   //Terry OptPass789A.HC:779 — MOV RAX,arg1: load the
      //return value into x0. The return-stmt jump to lb_leave is a separate
      //IC_JMP (proven green, smoke89 d61); LEAVE's epilogue then rets.
      rt = tmpi->ic_class->raw_type;
      A64LoadArg(tmpi, 0, &tmpi->arg1, rt);
      return TRUE;

    case IC_RETURN_VAL2:  //Terry OptPass789A.HC:784 — result already in return reg
      return TRUE;

    case IC_SET_RAX: //PrsExp indirect-call callee into the register save slot
      A64LoadArg(tmpi, 0, &tmpi->arg1, RT_I64);
      A64W(tmpi, ARM_ldrRegImmX(0, 0, 0));
      return TRUE;

    case IC_ADD_RSP1:
    //Terry PrsExp.HC:583 - emitted when callee is Ff_RET1/Ff_ARGPOP
    //(callee pops own args; x86 emits nothing, OptPass789A.HC:1038).
    //Our callees are aiwnios HOST fns which never pop the caller's
    //stack, so WE clean here. ic_data is already bytes ((cnt)<<3).
    //FUTURE bug#7d: a JIT'd callee whose IC_RET pops argc would
    //double-pop - unify convention when JIT->JIT calls exist.
    A64AddSP(tmpi, tmpi->ic_data*2); //ic_data counts 8B slots; ours are 16B (bug#7c: Apple hw-enforces 16-aligned SP on every SP-based access)
    return TRUE;
  case IC_ADD_RSP: //Terry ICAddRSP (BackLib.HC:59) — ic_data already in BYTES
      A64AddSP(tmpi, tmpi->ic_data*2); //ic_data counts 8B slots; ours are 16B (bug#7c: Apple hw-enforces 16-aligned SP on every SP-based access)
      return TRUE;

    case IC_STR_CONST: //Terry OptPass789A.HC:296 — res <- lb->addr + buf
    case IC_GET_LABEL:
      lb = tmpi->ic_data;
      addr = lb->addr + a64_str_buf;
      //Fixed 4-instruction materialization: lb->addr moves between passes,
      //emitted size must NOT change (pass-size stability).
      A64W(tmpi, ARM_movzImmX(0, addr & 0xFFFF, 0));
      A64W(tmpi, ARM_movkImmX(0, (addr >> 16) & 0xFFFF, 16));
      A64W(tmpi, ARM_movkImmX(0, (addr >> 32) & 0xFFFF, 32));
      A64W(tmpi, ARM_movkImmX(0, (addr >> 48) & 0xFFFF, 48));
      A64StoreRes(tmpi, 0, &tmpi->res, RT_I64);
      return TRUE;
  }
  return FALSE;
}
