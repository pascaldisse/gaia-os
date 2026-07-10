// Arm64Asm.ZC — mechanical HolyC translation of aiwnios/c/arm64_asm.c
// (aarch64 instruction encoders). See that file for the original C source
// and commentary; this port preserves structure, names, and comments 1:1.

//PORT: from aiwn_arm.h — error sentinel constants. In the original C these
//      were returned by ARM_FORCE_ALIGN/ARM_FORCE_REG and by explicit
//      error checks; every such error path below now throws directly
//      (throw('Asm')). These constants remain only where the ported code
//      still compares a helper's result against them (see LogicalImm /
//      LogicalImmX).
#define ARM_ERR_INV_OFF -2
#define ARM_ERR_INV_REG -3

//PORT: from aiwn_arm.h — shift-mode constant used as the default shift
//      operand to the register-shift encoders below.
#define LSL 0

#define MASKn(v, bits, off) (((v) & ((1 << (bits)) - 1)) << (off))

// https://math.stackexchange.com/questions/490813/range-twos-complement
#define ARM_FORCE_ALIGN(v, bits, chop)                                         \
  {                                                                            \
    if ((v) & ((1 << (chop)) - 1))                                             \
      throw('Asm');                                                  \
    if ((v) >= 0) {                                                            \
      if (((v) >> chop) > (1 << ((bits) - 1)) - 1)                            \
        throw('Asm');                                                \
    } else if ((-(v) >> chop) > (1 << ((bits) - 1)))                          \
      throw('Asm');                                                  \
  }
#define ARM_FORCE_REG(r)                                                       \
  {                                                                            \
    if ((r) < 0)                                                               \
      throw('Asm');                                                  \
    if ((r) > 32)                                                              \
      throw('Asm');                                                  \
  }
// https://github.com/fujitsu/xbyak_aarch64
typedef struct {
  I64 n, m;
  I64 type;
  I64 shift;
  I64 init_sh;
} CARMAdrRegReg;
typedef struct {
  I64 n;
  I64 off;
} CARMAdrRegImm;
typedef struct {
  I64 n;
  I64 off;
} CARMAdrPostImm;
typedef struct {
  I64 n;
  I64 off;
} CARMAdrPreImm;
typedef struct {
  I64 n;
  I64 off;
  I64 ext;
} CARMAdrScImm;

I64 LdStRegExt(I64 sz, I64 opc, I64 rt, I64 n,
                          I64 m, I64 mod) {
  I64 s = 1;
  I64 v = 0;
  return (sz << 30) | (7 << 27) | (v << 26) | (opc << 22) | (1 << 21) |
         (m << 16) | (mod << 13) | (s << 12) | (2 << 10) | (n << 5) | rt;
}

I64 MvWideImmX(U32 opc, I64 d, I64 imm, I64 shift) {
  if (imm & ~0xffff)
    throw('Asm');
  ARM_FORCE_REG(d);
  U32 hw = (shift >> 4) & 0b11;
  U32 imm16 = imm & 0xffff;
  return (1 << 31) | (opc << 29) | (0x25 << 23) | (hw << 21) | (imm16 << 5) | d;
}
I64 DataProc2Src(U32 opcode, I64 rd, I64 rn,
                            I64 rm) {
  ARM_FORCE_REG(rn);
  ARM_FORCE_REG(rm);
  ARM_FORCE_REG(rd);
  U32 sf = 1;
  U32 S = 0;
  return (sf << 31) | (S << 29) | (0xd6 << 21) | (rm << 16) | (opcode << 10) |
         (rn << 5) | rd;
}

I64 FpComp(U32 M, U32 S, U32 type, U32 op,
                      U32 opcode2, I64 vn, I64 vm) {
  ARM_FORCE_REG(vn);
  ARM_FORCE_REG(vm);
  return (M << 31) | (S << 29) | (0xf << 25) | (type << 22) | (1 << 21) |
         (vm << 16) | (op << 14) | (1 << 13) | (vn << 5) | opcode2;
}

I64 FpDataProc1Reg(U32 M, U32 S, U32 type,
                              U32 opcode, I64 vd, I64 vn) {
  ARM_FORCE_REG(vn);
  ARM_FORCE_REG(vd);
  return (M << 31) | (S << 29) | (0xf << 25) | (type << 22) | (1 << 21) |
         (opcode << 15) | (1 << 14) | (vn << 5) | vd;
}

I64 LdStRegPairPostImm(I64 opc, I64 l, I64 r, I64 r2,
                                  CARMAdrPostImm *pre) {
  I64 imm = pre->off;
  I64 times = (opc == 2) ? 2 : 1;
  ARM_FORCE_ALIGN(imm, 7, times + 1);
  I64 imm7 = MASKn(imm >> (times + 1), 7, 15);
  return (opc << 30) | (0x5 << 27) | (1 << 23) | (l << 22) | imm7 | (r2 << 10) |
         (pre->n << 5) | r;
}
I64 LdStRegPairPre(I64 opc, I64 L, I64 rt1, I64 rt2,
                              CARMAdrPreImm *adr) {
  I64 imm = adr->off;
  I64 times = (opc == 2) ? 2 : 1;
  ARM_FORCE_ALIGN(imm, 7, times + 1);
  U32 imm7 = MASKn(imm >> (times + 1), 7, 15);
  return (opc << 30) | (0x5 << 27) | (3 << 23) | (L << 22) | imm7 |
         (rt2 << 10) | (adr->n << 5) | rt1;
}

// For 64bit
I64 LdStSimdFpUnImm(I32 opc, I64 vt, I64 n,
                               I64 imm) {
  U32 imm12 = MASKn(imm >> 3, 12, 10);
  ARM_FORCE_ALIGN(imm, 12, 3);
  if (imm < 0)
    throw('Asm');
  return (3 << 30) | (0x7 << 27) | (1 << 26) | (1 << 24) | (opc << 22) | imm12 |
         (n << 5) | vt;
}

I64 LdRegSimdFpLiteralEnc(I64 vt, I64 imm) {
  U32 imm19 = MASKn(imm >> 2, 19, 5);
  ARM_FORCE_ALIGN(imm, 19, 2);
  return (1 << 30) | (0x3 << 27) | (1 << 26) | imm19 | vt;
}

I64 LdStRegPre(I64 sz, I64 opc, I64 r,
                          CARMAdrPreImm *adr) {
  ARM_FORCE_ALIGN(adr->off, 9, 0);
  return (sz << 30) | (0x7 << 27) | (0xf << 27) | (opc << 22) |
         MASKn(adr->off, 9, 12) | (3 << 10) | (adr->n << 5) | r;
}

I64 LdStRegPostImm(I64 sz, I64 opc, I64 r,
                              CARMAdrPostImm *adr) {
  ARM_FORCE_ALIGN(adr->off, 9, 0);
  return (sz << 30) | (0xf << 27) | (0xf << 27) | (opc << 22) |
         MASKn(adr->off, 9, 12) | (1 << 10) | (adr->n << 5) | r;
}

I64 FpDataProc2Reg(I64 M, I64 s, I64 type,
                              I64 opcode, I64 d, I64 n, I64 m) {
  return (M << 31) | (s << 29) | (0xf << 25) | (type << 22) | (1 << 21) |
         (m << 16) | (opcode << 12) | (2 << 10) | (n << 5) | d;
}

I64 LdRegLiteralEnc(I64 opc, I64 v, I64 r,
                               I64 label) {
  ARM_FORCE_ALIGN(label, 19, 2);
  return (opc << 30) | (0x3 << 27) | (v << 26) | MASKn(label >> 2, 19, 5) | r;
}
I64 ConversionFpIntIF(I64 sf, I64 s, I64 type,
                                 I64 rmode, I64 opcode, I64 d,
                                 I64 n) {
  return (sf << 31) | (s << 29) | (0xf << 25) | (type << 22) | (1 << 21) |
         (rmode << 19) | (opcode << 16) | (n << 5) | d;
}
I64 ConversionFpIntFI(U32 sf, U32 S, U32 type,
                                 U32 rmode, U32 opcode, I32 vd,
                                 I32 rn) {
  return (sf << 31) | (S << 29) | (0xf << 25) | (type << 22) | (1 << 21) |
         (rmode << 19) | (opcode << 16) | (rn << 5) | (vd);
}
I64 LdStRegUnImm(I64 sz, I64 opc, I64 r,
                            CARMAdrRegImm *adr) {
  ARM_FORCE_ALIGN(adr->off, 12, sz);
  return MASKn(adr->off >> sz, 12, 10) | (sz << 30) | (0x7 << 27) | (1 << 24) |
         (opc << 22) | (adr->n << 5) | (r);
}

I64 LdStRegReg(I64 size, I64 opc, I64 rt, I64 m,
                          I64 n, I64 shift) {
  //(1<<12) triggers shift
  I64 S = shift;
  return (size << 30) | (0x7 << 27) | (opc << 22) | (1 << 21) | (n << 16) |
         (3 << 13) | (S << 12) | (2 << 10) | (m << 5) | rt;
}

I64 CompareBr(I64 op, I64 r, I64 imm) {
  ARM_FORCE_ALIGN(imm, 19, 2);
  return (0x1a << 25) | (op << 24) | MASKn(imm >> 2, 19, 5) | r;
}

I64 CompareBrX(I64 op, I64 r, I64 imm) {
  ARM_FORCE_ALIGN(imm, 19, 2);
  return (1 << 31) | (0x1a << 25) | (op << 24) | MASKn(imm >> 2, 19, 5) | r;
}

I64 UncondBrNoReg(I64 opc, I64 op2, I64 op3, I64 rn,
                             I64 op4) {
  return (0x6b << 25) | (opc << 21) | (op2 << 16) | (op3 << 10) | (rn << 5) |
         op4;
}

I64 UncondBrImm(I64 op, I64 label) {
  ARM_FORCE_ALIGN(label, 26, 2);
  return (op << 31) | (5 << 26) | MASKn(label >> 2, 26, 0);
}

I64 UncondBr1Reg(I64 opc, I64 op2, I64 op3, I64 reg,
                            I64 op4) {
  return (0x6b << 25) | (opc << 21) | (op2 << 16) | (op3 << 10) | (reg << 5) |
         op4;
}

I64 CondBrImm(I64 cond, I64 imm) {
  ARM_FORCE_ALIGN(imm, 19, 2);
  return (0x2a << 25) | MASKn(imm >> 2, 19, 5) | cond;
}
I64 AddSubImm(I64 op, I64 s, I64 d, I64 n,
                         I64 imm, I64 sh) {
  ARM_FORCE_ALIGN(imm, 12, 0);
  I32 imm12 = MASKn(imm, 12, 10);
  I32 shift = MASKn(sh, 1, 22);
  return imm12 | shift | MASKn(d, 6, 0) | MASKn(n, 6, 5) | (0x11 << 24) |
         (s << 29) | (op << 30);
}

I64 TestBr(I64 op, I64 rt, I64 imm, I64 off) {
  I64 d5 = (imm >> 5) & 1;
  I64 d40 = MASKn(imm, 5, 0);
  I64 imm14 = MASKn(off >> 2, 14, 5);
  ARM_FORCE_ALIGN(off, 14, 2);
  return (d5 << 31) | (0x1b << 25) | (op << 24) | (d40 << 19) | imm14 | rt;
}

I64 Bitfield(I64 op, I64 d, I64 n, U64 immr,
                        U64 imms) {
  return (op << 29) | (0x26 << 23) | (0 << 22) | MASKn(immr, 6, 16) |
         MASKn(imms, 6, 10) | (n << 5) | d;
}

I64 BitfieldX(I64 op, I64 d, I64 n, U64 immr,
                         U64 imms) {
  return (1 << 31) | (op << 29) | (0x26 << 23) | (1 << 22) |
         MASKn(immr, 6, 16) | MASKn(imms, 6, 10) | (n << 5) | d;
}

I64 AddSubImmX(I64 op, I64 s, I64 d, I64 n,
                          I64 imm, I64 sh) {
  if (imm < 0)
    throw('Asm');
  ARM_FORCE_ALIGN(imm, 12, 0);
  I32 imm12 = MASKn(imm, 12, 10);
  I32 shift = MASKn(sh, 1, 22);
  return (1 << 31) | imm12 | shift | MASKn(d, 6, 0) | MASKn(n, 6, 5) |
         (0x11 << 24) | (s << 29) | (op << 30);
}

I64 AddSubExtReg(I64 opc, I64 s, I64 d, I64 n,
                            I64 m, I64 emod, I64 sh) {
  return (opc << 30) | (s << 29) | (0xb << 24) | (1 << 21) | (m << 16) |
         (emod << 13) | MASKn(sh, 3, 10) | (n << 5) | d;
}

I64 AddSubExtRegX(I64 opc, I64 s, I64 d, I64 n,
                             I64 m, I64 emod, I64 sh) {
  U32 option = (emod == 12) ? 3 : emod;
  return (1 << 31) | (opc << 30) | (s << 29) | (0xb << 24) | (1 << 21) |
         (m << 16) | (option << 13) | MASKn(sh, 3, 10) | (n << 5) | d;
}

I64 AddSubShiftReg(I64 opc, I64 s, I64 d, I64 n,
                              I64 m, I64 shmod, I64 sh,
                              I64 alias) {
  I64 rd_sp = d == 31;
  I64 rn_sp = d == 31;
  if (((rd_sp + rn_sp) >= 1 + alias) && shmod == LSL) {
    return AddSubExtReg(opc, s, d, n, m, 12, sh);
  }
  return (opc << 30) | (s << 29) | (0xb << 24) | (shmod << 22) | (m << 16) |
         MASKn(sh, 6, 10) | (n << 5) | d;
}

I64 LdStSimdFpRegPostImm(I64 opc, I64 d, I64 p,
                                    I64 off) {
  ARM_FORCE_ALIGN(off, 9, 0);
  return (3 << 30) | (0x7 << 27) | (1 << 26) | (opc << 22) | MASKn(off, 9, 12) |
         (1 << 10) | (p << 5) | d;
}

I64 LdStSimdFpRegPreImm(I64 opc, I64 d, I64 p,
                                   I64 off) {
  ARM_FORCE_ALIGN(off, 9, 0);
  return (3 << 30) | (0x7 << 27) | (1 << 26) | (opc << 22) | MASKn(off, 9, 12) |
         (3 << 10) | (p << 5) | d;
}

I64 AddSubShiftRegX(I64 opc, I64 s, I64 d, I64 n,
                               I64 m, I64 shmod, I64 sh,
                               I64 alias) {
  I64 rd_sp = d == 31;
  I64 rn_sp = n == 31;
  if (((rd_sp + rn_sp) >= 1 + alias) && shmod == LSL) {
    return AddSubExtRegX(opc, s, d, n, m, 12, sh);
  }
  return (1 << 31) | (opc << 30) | (s << 29) | (0xb << 24) | (shmod << 22) |
         (m << 16) | MASKn(sh, 6, 10) | (n << 5) | d;
}

I64 DataProc3RegX(I64 op54, I64 op31, I64 o0, I64 d,
                             I64 n, I64 m, I64 ra) {
  return (1 << 31) | (op54 << 29) | (0x1b << 24) | (op31 << 21) | (m << 16) |
         (o0 << 15) | (ra << 10) | (n << 5) | (d);
}

I64 LogicalShiftReg(I64 opc, I64 N, I64 rd, I64 rn,
                               I64 rm, I64 shmod, U32 sh) {
  return (opc << 29) | (0xa << 24) | (shmod << 22) | (N << 21) | (rm << 16) |
         MASKn(sh, 6, 10) | MASKn(rn, 6, 5) | rd;
}

I64 LogicalShiftRegX(I64 opc, I64 N, I64 rd, I64 rn,
                                I64 rm, I64 shmod, U32 sh) {
  return (1 << 31) | (opc << 29) | (0xa << 24) | (shmod << 22) | (N << 21) |
         (rm << 16) | MASKn(sh, 6, 10) | MASKn(rn, 6, 5) | rd;
}

I64 CondSel(I64 op, I64 s, I64 op2, I64 d, I64 n,
                       I64 m, I64 cond) {
  return (op << 30) | (s << 29) | (m << 16) | (cond << 12) | (op2 << 10) |
         (n << 5) | (d);
}

I64 CondSelX(I64 op, I64 s, I64 op2, I64 d,
                        I64 n, I64 m, I64 cond) {
  return (1 << 31) | (op << 30) | (s << 29) | (0xd4 << 21) | (m << 16) |
         (cond << 12) | (op2 << 10) | (n << 5) | (d);
}

I64 MvReg(I64 d, I64 n) {
  if (d == 31 || n == 31) {
    return AddSubImm(0, 0, d, n, 0, 0);
  } else
    return LogicalShiftReg(1, 0, d, 31, n, LSL, 0);
}

I64 MvRegX(I64 d, I64 n) {
  if (d == 31 || n == 31) {
    return AddSubImmX(0, 0, d, n, 0, 0);
  } else
    return LogicalShiftRegX(1, 0, d, 31, n, LSL, 0);
}

I64 LdStSimdFpRegRegShift(U32 opc, I64 vt, I64 rn,
                              I64 rm) {
  U32 size = 3;
  U32 option = 3;
  U32 S = 1;
  U32 V = 1;
  return (size << 30) | (0x7 << 27) | (V << 26) | (opc << 22) | (1 << 21) |
         (rm << 16) | (option << 13) | (S << 12) | (2 << 10) | (rn << 5) | vt;
}

// dst+=label
I64 ARM_adrX(I64 d, I64 pc_off) {
  I32 inst = 0x10 << 24;
  ARM_FORCE_ALIGN(pc_off, 21, 0);
  inst |= MASKn(d, 6, 0);
  inst |= MASKn(pc_off >> 2, 19, 5);
  inst |= MASKn(pc_off, 2, 29);
  return inst;
}

I64 ARM_addImmX(I64 d, I64 n, I64 imm) {
  return AddSubImmX(0, 0, d, n, imm, 0);
}

I64 ARM_subImmX(I64 d, I64 n, I64 imm) {
  return AddSubImmX(1, 0, d, n, imm, 0);
}

I64 ARM_subsImmX(I64 d, I64 n, I64 imm) {
  return AddSubImmX(1, 1, d, n, imm, 0);
}

I64 ARM_cmpImmX(I64 n, I64 imm) {
  return AddSubImmX(1, 1, 31, n, imm, 0);
}

I64 ARM_andRegX(I64 rd, I64 rn, I64 rm) {
  return LogicalShiftRegX(0, 0, rd, rn, rm, LSL, 0);
}

I64 ARM_orrRegX(I64 rd, I64 rn, I64 rm) {
  return LogicalShiftRegX(1, 0, rd, rn, rm, LSL, 0);
}

// MVN move negative
I64 ARM_mvnShift(I64 rd, I64 rm, I64 shmod, I64 sh) {
  return LogicalShiftReg(1, 1, rd, 31, rm, shmod, sh);
}

I64 ARM_mvnShiftX(I64 rd, I64 rm, I64 shmod, I64 sh) {
  return LogicalShiftRegX(1, 1, rd, 31, rm, shmod, sh);
}

// Psuedo inst i invented
I64 ARM_mvnReg(I64 rd, I64 rm) {
  return LogicalShiftReg(1, 1, rd, 31, rm, LSL, 0);
}

I64 ARM_mvnRegX(I64 rd, I64 rm) {
  return LogicalShiftRegX(1, 1, rd, 31, rm, LSL, 0);
}

// exclusive xor
I64 ARM_eorShiftX(I64 rd, I64 rn, I64 rm, I64 shmod,
                      I64 sh) {
  return LogicalShiftRegX(2, 0, rd, rn, rm, shmod, sh);
}

I64 ARM_eorRegX(I64 rd, I64 rn, I64 rm) {
  return ARM_eorShiftX(rd, rn, rm, LSL, 0);
}

I64 ARM_andsRegX(I64 rd, I64 rn, I64 rm) {
  return LogicalShiftRegX(3, 0, rd, rn, rm, LSL, 0);
}

I64 ARM_movReg(I64 d, I64 n) {
  return MvReg(d, n);
}

I64 ARM_movRegX(I64 d, I64 n) {
  return MvRegX(d, n);
}

I64 ARM_addShiftRegX(I64 d, I64 n, I64 m, I64 sh) {
  return AddSubShiftRegX(0, 0, d, n, m, LSL, sh, 0);
}

I64 ARM_subShiftRegX(I64 d, I64 n, I64 m, I64 sh) {
  return AddSubShiftRegX(1, 0, d, n, m, LSL, sh, 0);
}

I64 ARM_addShiftRegXs(I64 d, I64 n, I64 m, I64 sh) {
  return AddSubShiftRegX(0, 1, d, n, m, LSL, sh, 0);
}

I64 ARM_subShiftRegXs(I64 d, I64 n, I64 m, I64 sh) {
  return AddSubShiftRegX(1, 1, d, n, m, LSL, sh, 0);
}

I64 ARM_addRegX(I64 d, I64 n, I64 m) {
  return AddSubShiftRegX(0, 0, d, n, m, LSL, 0, 0);
}

I64 ARM_addsRegX(I64 d, I64 n, I64 m) {
  return AddSubShiftRegX(0, 1, d, n, m, LSL, 0, 0);
}

I64 ARM_addsImmX(I64 rd, I64 rn, I64 imm) {
  return AddSubImmX(0, 1, rd, rn, imm, 0);
}

I64 ARM_subRegX(I64 d, I64 n, I64 m) {
  return AddSubShiftRegX(1, 0, d, n, m, LSL, 0, 0);
}

I64 ARM_subsRegX(I64 d, I64 n, I64 m) {
  return AddSubShiftRegX(1, 1, d, n, m, LSL, 0, 0);
}

I64 ARM_negsRegX(I64 d, I64 m) {
  return AddSubShiftRegX(1, 1, d, 31, m, LSL, 0, 1);
}

I64 ARM_cmpRegX(I64 n, I64 m) {
  return AddSubShiftRegX(1, 1, 31, n, m, LSL, 0, 1);
}

I64 ARM_csel(I64 d, I64 n, I64 m, I64 cond) {
  return CondSel(0, 0, 0, d, n, m, cond);
}

I64 ARM_cselX(I64 d, I64 n, I64 m, I64 cond) {
  return CondSelX(0, 0, 0, d, n, m, cond);
}

I64 ARM_cset(I64 d, I64 cond) {
  return CondSel(0, 0, 1, d, 31, 31, cond ^ 1);
}

I64 ARM_csetX(I64 d, I64 cond) {
  return CondSelX(0, 0, 1, d, 31, 31, cond ^ 1);
}

I64 ARM_mulRegX(I64 d, I64 n, I64 m) {
  return DataProc3RegX(0, 0, 0, d, n, m, 31);
}

I64 ARM_uxtb(I64 d, I64 n) {
  return Bitfield(2, d, n, 0, 7);
}

I64 ARM_uxtbX(I64 d, I64 n) {
  return BitfieldX(2, d, n, 0, 7);
}

I64 ARM_uxth(I64 d, I64 n) {
  return Bitfield(2, d, n, 0, 15);
}

I64 ARM_uxthX(I64 d, I64 n) {
  return BitfieldX(2, d, n, 0, 15);
}

I64 ARM_uxtwX(I64 d, I64 n) {
  return BitfieldX(2, d, n, 0, 31);
}

I64 ARM_sxtb(I64 d, I64 n) {
  return BitfieldX(0, d, n, 0, 7);
}

I64 ARM_sxtbX(I64 d, I64 n) {
  return BitfieldX(0, d, n, 0, 7);
}

I64 ARM_sxth(I64 d, I64 n) {
  return Bitfield(0, d, n, 0, 15);
}

I64 ARM_sxthX(I64 d, I64 n) {
  return BitfieldX(0, d, n, 0, 15);
}

I64 ARM_sxtw(I64 d, I64 n) {
  return Bitfield(0, d, n, 0, 31);
}

I64 ARM_sxtwX(I64 d, I64 n) {
  return BitfieldX(0, d, n, 0, 31);
}

I64 ARM_asrImm(I64 d, I64 n, I64 imm) {
  return Bitfield(0, d, n, imm, 31);
}

I64 ARM_asrImmX(I64 d, I64 n, I64 imm) {
  return BitfieldX(0, d, n, imm, 63);
}

I64 ARM_lslImm(I64 d, I64 n, I64 imm) {
  return Bitfield(2, d, n, MASKn(-imm % 32, 6, 0), 31 - imm);
}

I64 ARM_lslImmX(I64 d, I64 n, I64 imm) {
  return BitfieldX(2, d, n, MASKn(-imm % 64, 6, 0), 63 - imm);
}

I64 ARM_lsrImm(I64 d, I64 n, I64 imm) {
  return Bitfield(2, d, n, imm, 31);
}

I64 ARM_lsrImmX(I64 d, I64 n, I64 imm) {
  return BitfieldX(2, d, n, imm, 63);
}
I64 ARM_ret() {
  return UncondBrNoReg(2, 31, 0, 30, 0);
}

I64 ARM_eret() {
  return UncondBrNoReg(4, 31, 0, 31, 0);
}

I64 ARM_br(I64 r) {
  return UncondBr1Reg(0, 31, 0, r, 0);
}

// Func call
I64 ARM_blr(I64 r) {
  return UncondBr1Reg(1, 31, 0, r, 0);
}

I64 ARM_retReg(I64 r) {
  return UncondBr1Reg(2, 31, 0, r, 0);
}

I64 ARM_bcc(I64 cond, I64 lab) {
  return CondBrImm(cond, lab);
}

I64 ARM_b(I64 lab) {
  return UncondBrImm(0, lab);
}

I64 ARM_bl(I64 lab) {
  return UncondBrImm(1, lab);
}

// Condition branch if not zero
I64 ARM_cbz(I64 r, I64 label) {
  return CompareBr(0, r, label);
}

I64 ARM_cbzX(I64 r, I64 label) {
  return CompareBrX(0, r, label);
}

// Vice versa
I64 ARM_cbnz(I64 r, I64 label) {
  return CompareBr(1, r, label);
}

I64 ARM_cbnzX(I64 r, I64 label) {
  return CompareBrX(1, r, label);
}

// This is one of Bungis's favorites,it tests if a bit is set and jumps
I64 ARM_tbz(I64 rt, I64 bit, I64 label) {
  return TestBr(0, rt, bit, label);
}

I64 ARM_tbnz(I64 rt, I64 bit, I64 label) {
  return TestBr(1, rt, bit, label);
}

I64 ARM_strbRegReg(I64 r, I64 a, I64 b) {
  return LdStRegReg(0, 0, r, a, b, 0);
}

I64 ARM_strbRegImm(I64 r, I64 n, I64 off) {
  CARMAdrRegImm adr;
  adr.n = n;
  adr.off = off;
  return LdStRegUnImm(0, 0, r, &adr);
}

I64 ARM_ldrbRegImm(I64 r, I64 n, I64 off) {
  CARMAdrRegImm adr;
  adr.n = n;
  adr.off = off;
  return LdStRegUnImm(0, 1, r, &adr);
}

I64 ARM_ldrbRegReg(I64 r, I64 a, I64 b) {
  return LdStRegReg(0, 1, r, a, b, 0);
}

I64 ARM_strhRegReg(I64 r, I64 a, I64 b) {
  return LdStRegReg(1, 0, r, a, b, 0);
}

I64 ARM_strhRegImm(I64 r, I64 n, I64 off) {
  CARMAdrRegImm adr;
  adr.n = n;
  adr.off = off;
  return LdStRegUnImm(1, 0, r, &adr);
}

I64 ARM_ldrhRegReg(I64 r, I64 a, I64 b) {
  return LdStRegReg(1, 1, r, a, b, 0);
}

I64 ARM_ldrhRegImm(I64 r, I64 n, I64 off) {
  CARMAdrRegImm adr;
  adr.n = n;
  adr.off = off;
  return LdStRegUnImm(1, 1, r, &adr);
}

I64 ARM_strRegImm(I64 r, I64 n, I64 off) {
  CARMAdrRegImm adr;
  adr.n = n;
  adr.off = off;
  return LdStRegUnImm(2, 0, r, &adr);
}

I64 ARM_strRegReg(I64 r, I64 a, I64 b) {
  return LdStRegReg(2, 0, r, a, b, 0);
}

I64 ARM_ldrRegReg(I64 r, I64 a, I64 b) {
  return LdStRegReg(2, 1, r, a, b, 0);
}

I64 ARM_ldrRegImm(I64 r, I64 n, I64 off) {
  CARMAdrRegImm adr;
  adr.n = n;
  adr.off = off;
  return LdStRegUnImm(2, 1, r, &adr);
}

I64 ARM_strRegImmX(I64 r, I64 n, I64 off) {
  CARMAdrRegImm adr;
  adr.n = n;
  adr.off = off;
  return LdStRegUnImm(3, 0, r, &adr);
}

I64 ARM_ldrRegImmX(I64 r, I64 n, I64 off) {
  CARMAdrRegImm adr;
  adr.n = n;
  adr.off = off;
  return LdStRegUnImm(3, 1, r, &adr);
}

I64 ARM_strRegRegX(I64 r, I64 a, I64 b) {
  return LdStRegReg(3, 0, r, a, b, 0);
}
I64 ARM_ldrRegRegX(I64 r, I64 a, I64 b) {
  return LdStRegReg(3, 1, r, a, b, 0);
}

I64 ARM_fmulReg(I64 d, I64 n, I64 m) {
  return FpDataProc2Reg(0, 0, 1, 0, d, n, m);
}

I64 ARM_fdivReg(I64 d, I64 n, I64 m) {
  return FpDataProc2Reg(0, 0, 1, 1, d, n, m);
}

I64 ARM_faddReg(I64 d, I64 n, I64 m) {
  return FpDataProc2Reg(0, 0, 1, 2, d, n, m);
}

I64 ARM_fsubReg(I64 d, I64 n, I64 m) {
  return FpDataProc2Reg(0, 0, 1, 3, d, n, m);
}

I64 ARM_scvtf(I64 d, I64 n) {
  return ConversionFpIntFI(1, 0, 1, 0, 2, d, n);
}

I64 ARM_ucvtf(I64 d, I64 n) {
  return ConversionFpIntFI(1, 0, 1, 0, 3, d, n);
}

I64 ARM_fcvtzs(I64 d, I64 n) {
  return ConversionFpIntIF(1, 0, 1, 3, 0, d, n);
}

I64 ARM_ldrLabel(I64 d, I64 label) {
  return LdRegLiteralEnc(0, 0, d, label);
}

I64 ARM_ldrLabelX(I64 d, I64 label) {
  return LdRegLiteralEnc(1, 0, d, label);
}

I64 ARM_ldrLabelF64(I64 d, I64 label) {
  return LdRegSimdFpLiteralEnc(d, label);
}

I64 ARM_strPostImm(I64 r, I64 b, I64 off) {
  CARMAdrPostImm adr = {b, off};
  return LdStRegPostImm(2, 0, r, &adr);
}

I64 ARM_ldrPostImm(I64 r, I64 b, I64 off) {
  CARMAdrPostImm adr = {b, off};
  return LdStRegPostImm(2, 1, r, &adr);
}

I64 ARM_strPreImm(I64 r, I64 b, I64 off) {
  CARMAdrPreImm adr = {b, off};
  return LdStRegPre(2, 0, r, &adr);
}

I64 ARM_ldrPreImm(I64 r, I64 b, I64 off) {
  CARMAdrPreImm adr = {b, off};
  return LdStRegPre(2, 1, r, &adr);
}

I64 ARM_strPostImmX(I64 r, I64 b, I64 off) {
  CARMAdrPostImm adr = {b, off};
  return LdStRegPostImm(3, 0, r, &adr);
}

I64 ARM_ldrPostImmX(I64 r, I64 b, I64 off) {
  CARMAdrPostImm adr = {b, off};
  return LdStRegPostImm(3, 1, r, &adr);
}

I64 ARM_strPreImmX(I64 r, I64 b, I64 off) {
  CARMAdrPreImm adr = {b, off};
  return LdStRegPre(3, 0, r, &adr);
}

I64 ARM_ldrPreImmX(I64 r, I64 b, I64 off) {
  CARMAdrPreImm adr = {b, off};
  return LdStRegPre(3, 1, r, &adr);
}

//
// STP store pair of registers
// LDP load a pair of registers
//

I64 ARM_stpPreImm(I64 r, I64 r2, I64 b, I64 off) {
  CARMAdrPreImm adr = {b, off};
  return LdStRegPairPre(0, 0, r, r2, &adr);
}

I64 ARM_stpPreImmX(I64 r, I64 r2, I64 b, I64 off) {
  CARMAdrPreImm adr = {b, off};
  return LdStRegPairPre(2, 0, r, r2, &adr);
}

I64 ARM_ldpPreImm(I64 r, I64 r2, I64 b, I64 off) {
  CARMAdrPreImm adr = {b, off};
  return LdStRegPairPre(0, 1, r, r2, &adr);
}

I64 ARM_ldpPreImmX(I64 r, I64 r2, I64 b, I64 off) {
  CARMAdrPreImm adr = {b, off};
  return LdStRegPairPre(2, 1, r, r2, &adr);
}

I64 ARM_stpPostImm(I64 r, I64 r2, I64 b, I64 off) {
  CARMAdrPostImm adr = {b, off};
  return LdStRegPairPostImm(0, 0, r, r2, &adr);
}

I64 ARM_stpPostImmX(I64 r, I64 r2, I64 b, I64 off) {
  CARMAdrPostImm adr = {b, off};
  return LdStRegPairPostImm(2, 0, r, r2, &adr);
}

I64 ARM_ldpPostImm(I64 r, I64 r2, I64 b, I64 off) {
  CARMAdrPostImm adr = {b, off};
  return LdStRegPairPostImm(0, 1, r, r2, &adr);
}

I64 ARM_ldpPostImmX(I64 r, I64 r2, I64 b, I64 off) {
  CARMAdrPostImm adr = {b, off};
  return LdStRegPairPostImm(2, 1, r, r2, &adr);
}

I64 ARM_ldrRegImmF64(I64 r, I64 n, U64 off) {
  return LdStSimdFpUnImm(1, r, n, off);
}

I64 ARM_strRegImmF64(I64 r, I64 n, U64 off) {
  return LdStSimdFpUnImm(0, r, n, off);
}

I64 ARM_fnegReg(I64 d, I64 s) {
  return FpDataProc1Reg(0, 0, 1, 2, d, s);
}

I64 ARM_fmovReg(I64 d, I64 s) {
  return FpDataProc1Reg(0, 0, 1, 0, d, s);
}

I64 ARM_fabsReg(I64 d, I64 s) {   //A1 amendment (BACKA64-DESIGN.md 8): IC_ABS
  return FpDataProc1Reg(0, 0, 1, 1, d, s);
}

I64 ARM_fsqrtReg(I64 d, I64 s) {  //A1 amendment (BACKA64-DESIGN.md 8): IC_SQRT
  return FpDataProc1Reg(0, 0, 1, 3, d, s);
}

I64 ARM_fcmp(I64 a, I64 b) {
  return FpComp(0, 0, 1, 0, 0, a, b);
}

I64 ARM_sdivRegX(I64 d, I64 n, I64 m) {
  return DataProc2Src(3, d, n, m);
}

I64 ARM_lslvRegX(I64 d, I64 n, I64 m) {
  return DataProc2Src(8, d, n, m);
}

I64 ARM_lsrvRegX(I64 d, I64 n, I64 m) {
  return DataProc2Src(9, d, n, m);
}

I64 ARM_asrvRegX(I64 d, I64 n, I64 m) {
  return DataProc2Src(10, d, n, m);
}

I64 ARM_movzImmX(I64 d, I64 i16, I64 sh) {
  switch (sh) {
  case 0:
  case 16:
  case 32:
  case 48:
    break;
  default:
    throw('Asm');
  }
  return MvWideImmX(2, d, i16, sh);
}
I64 ARM_movkImmX(I64 d, I64 i16, I64 sh) {
  switch (sh) {
  case 0:
  case 16:
  case 32:
  case 48:
    break;
  default:
    throw('Asm');
  }
  return MvWideImmX(3, d, i16, sh);
}

I64 ARM_movnImmX(I64 d, I64 i16, I64 sh) {
  switch (sh) {
  case 0:
  case 16:
  case 32:
  case 48:
    break;
  default:
    throw('Asm');
  }
  return MvWideImmX(0, d, i16, sh);
}

I64 ARM_strPostImmF64(I64 d, I64 p, I64 off) {
  return LdStSimdFpRegPostImm(0, d, p, off);
}
I64 ARM_ldrPostImmF64(I64 d, I64 p, I64 off) {
  return LdStSimdFpRegPostImm(1, d, p, off);
}

I64 ARM_strPreImmF64(I64 d, I64 p, I64 off) {
  return LdStSimdFpRegPreImm(0, d, p, off);
}
I64 ARM_ldrPreImmF64(I64 d, I64 p, I64 off) {
  return LdStSimdFpRegPreImm(1, d, p, off);
}
I64 LdStSimdFpReg(I64 opc, I64 d, I64 n, I64 m) {
  U32 size = 3;
  U32 option = 3;
  U32 S = 0;
  U32 V = 1;
  return (size << 30) | (0x7 << 27) | (V << 26) | (opc << 22) | (1 << 21) |
         (m << 16) | (option << 13) | (S << 12) | (2 << 10) | (n << 5) | d;
}

I64 ARM_strRegRegF64(I64 d, I64 n, I64 m) {
  return LdStSimdFpReg(0, d, n, m);
}

I64 ARM_ldrRegRegF64(I64 d, I64 n, I64 m) {
  return LdStSimdFpReg(1, d, n, m);
}

//
// The sexy section. You have been blessed by the divine energy,so send
// a prayer for the ARM developers. In HolyC,typecasts are "bit-for-bit"
// and ARM allows us to do "bit-for-bit" transfers easily with fmov and freinds
//
I64 ARM_fmovI64F64(I64 d, I64 s) {
  return ConversionFpIntIF(1, 0, 1, 0, 6, d, s);
}
I64 ARM_fmovF64I64(I64 d, I64 s) {
  return ConversionFpIntFI(1, 0, 1, 0, 7, d, s);
}

I64 ARM_strhRegRegShift(I64 a, I64 n, I64 m) {
  return LdStRegReg(1, 0, a, n, m, 1);
}

I64 ARM_ldrhRegRegShift(I64 a, I64 n, I64 m) {
  return LdStRegReg(1, 1, a, n, m, 1);
}

I64 ARM_strRegRegShift(I64 a, I64 n, I64 m) {
  return LdStRegReg(2, 0, a, n, m, 1);
}

I64 ARM_ldrRegRegShift(I64 a, I64 n, I64 m) {
  return LdStRegReg(2, 1, a, n, m, 1);
}

I64 ARM_strRegRegShiftX(I64 a, I64 n, I64 m) {
  return LdStRegReg(3, 0, a, n, m, 1);
}

I64 ARM_ldrRegRegShiftX(I64 a, I64 n, I64 m) {
  return LdStRegReg(3, 1, a, n, m, 1);
}

I64 ARM_ldrRegRegShiftF64(I64 a, I64 n, I64 m) {
  return LdStSimdFpRegRegShift(1, a, n, m);
}

I64 ARM_strRegRegShiftF64(I64 a, I64 n, I64 m) {
  return LdStSimdFpRegRegShift(0, a, n, m);
}

//
// https://dinfuehr.github.io/blog/encoding-of-immediate-values-on-aarch64/
//
// I would rather eat a pineapple than explain this code
//

// Return's -1 on poo poo unencodable
//PORT: GCC __builtin_ctzl/clzl mapped to HolyC bit-scan primitives:
//      ctzl(x) -> Bsf(x), clzl(x) -> 63-Bsr(x). Same UB-on-zero contract
//      as x86 BSF/BSR; all call sites guard nonzero as in the original.
I64 GenNImmSImmR(I64 imm) {
  // TODO write this stuff in assemblt
  if (imm == 0 || ~imm == 0)
    throw('Asm');
  I64 consec = 0, idx, zero_gap = 0, first_zeros, width, x;
  U64 pattern;
#define ROT(x, n) ((x) >> n) | ((x) << (64 - n))
  // Find first 1
  zero_gap = Bsf(imm);
  // Count consecitive bits for the pattern
  consec = Bsf((~imm) >> zero_gap);
  // If leading is zero,check the number of ones at the end
  if (zero_gap == 0) {
    // Zeros may be present at the other side of  the ones
    //"[10000111]"
    //    ^^^^
    if ((imm >> consec) == 0)
      zero_gap = 64 - consec;
    else
      zero_gap = Bsf(imm >> consec) - consec;
    consec += 63 - Bsr(~imm);
  } else if (zero_gap != 0) {
    // add leading 0s to the gap amount
    zero_gap += 63 - Bsr(imm);
  }
  // Our pattern size is zero_gap+consec
  width = zero_gap + consec;
  switch (width) {
#define MAKE_PATT                                                              \
  x = consec - 1;                                                              \
  pattern = 0;                                                                 \
  for (idx = 0; idx != 64 / width; idx++)                                      \
    if (!idx)                                                                  \
      pattern |= (1 << consec) - 1;                                          \
    else                                                                       \
      pattern |= ((1 << consec) - 1) << (width * idx);

#define CHECK_PATT(n)                                                          \
  for (idx = 0; idx != width; idx++)                                           \
    if (pattern == imm)                                                        \
      return (n << 12) | (idx << 6) | x;                                       \
    else                                                                       \
      pattern = ROT(pattern, 1);
    break;
  case 2:
    MAKE_PATT;
    CHECK_PATT(0);
    break;
  case 4:
    MAKE_PATT;
    CHECK_PATT(0);
    break;
  case 8:
    MAKE_PATT;
    CHECK_PATT(0);
    break;
  case 16:
    MAKE_PATT;
    CHECK_PATT(0);
    break;
  case 32:
    MAKE_PATT;
    CHECK_PATT(0);
    break;
  case 64:
    MAKE_PATT;
    CHECK_PATT(1);
    break;
  default:;
  }
  throw('Asm');
}

I64 LogicalImm(I64 opc, I64 d, I64 s, I64 i) {
  I64 poodle = GenNImmSImmR(i);
  if (i == ARM_ERR_INV_OFF)
    throw('Asm');
  return MASKn(opc, 8, 29) | MASKn(0x24, 8, 23) | MASKn(s, 14, 10) |
         MASKn(s, 6, 5) | MASKn(d, 6, 0);
}

I64 LogicalImmX(I64 opc, I64 d, I64 s, I64 i) {
  I64 poodle = GenNImmSImmR(i);
  if (poodle == ARM_ERR_INV_OFF)
    throw('Asm');
  return (1 << 31) | MASKn(opc, 8, 29) | MASKn(0x24, 8, 23) |
         MASKn(poodle, 14, 10) | MASKn(s, 6, 5) | MASKn(d, 6, 0);
}

I64 ARM_andImmX(I64 d, I64 s, I64 i) {
  return LogicalImmX(0, d, s, i);
}

I64 ARM_orrImmX(I64 d, I64 s, I64 i) {
  return LogicalImmX(1, d, s, i);
}

I64 ARM_eorImmX(I64 d, I64 s, I64 i) {
  return LogicalImmX(2, d, s, i);
}

I64 LdStRegPairImm(I64 opc, I64 L, I64 r1, I64 r2,
                              I64 ra, I64 off) {
  I64 times = 2;
  if (off % times)
    throw('Asm');
  if (off > times * 256)
    throw('Asm');
  if (off < times * -256)
    throw('Asm');
  return MASKn(opc, 32, 30) | MASKn(0x5, 32, 27) | MASKn(2, 32, 23) |
         MASKn(L, 32, 22) | MASKn(off / 8, 7, 15) | MASKn(r2, 5, 10) |
         MASKn(ra, 5, 5) | MASKn(r1, 5, 0);
}

I64 ARM_stpImmX(I64 r1, I64 r2, I64 ra, I64 off) {
  return LdStRegPairImm(2, 0, r1, r2, ra, off);
}
I64 ARM_ldpImmX(I64 r1, I64 r2, I64 ra, I64 off) {
  return LdStRegPairImm(2, 1, r1, r2, ra, off);
}

I64 LdStSimdFpPairImm(I64 opc, I64 L, I64 r1, I64 r2,
                                 I64 ra, I64 off) {
  I64 times = 2;
  if (off % times)
    throw('Asm');
  if (off > times * 256)
    throw('Asm');
  if (off < times * -256)
    throw('Asm');
  return MASKn(opc, 32, 30) | MASKn(0x5, 32, 27) | (1 << 26) |
         MASKn(2, 32, 23) | MASKn(L, 32, 22) | MASKn(off / 8, 7, 15) |
         MASKn(r2, 5, 10) | MASKn(ra, 5, 5) | MASKn(r1, 5, 0);
}

I64 ARM_stpImmF64(I64 r1, I64 r2, I64 ra, I64 off) {
  return LdStSimdFpPairImm(1, 0, r1, r2, ra, off);
}
I64 ARM_ldpImmF64(I64 r1, I64 r2, I64 ra, I64 off) {
  return LdStSimdFpPairImm(1, 1, r1, r2, ra, off);
}
I64 ARM_andsImmX(I64 d, I64 n, I64 imm) {
  return LogicalImmX(3, d, n, imm);
}

I64 ARM_udivX(I64 d, I64 n, I64 m) {
  return DataProc2Src(2, d, n, m);
}
I64 ARM_umullX(I64 d, I64 n, I64 m) {
  return DataProc3RegX(0, 5, 0, d, n, m, 31);
}

I64 LdStRegUnsImm(I64 size, I64 opc, I64 rt,
                             I64 adr_reg, I64 off) {
  if (-256 > off)
    throw('Asm');
  if (255 < off)
    throw('Asm');
  return (size << 30) | (7 << 27) | (opc << 22) | MASKn(off, 9, 12) |
         (adr_reg << 5) | rt;
}

I64 ARM_sturb(I64 r, I64 a, I64 off) {
  return LdStRegUnsImm(0, 0, r, a, off);
}

I64 ARM_sturh(I64 r, I64 a, I64 off) {
  return LdStRegUnsImm(1, 0, r, a, off);
}

I64 ARM_sturw(I64 r, I64 a, I64 off) {
  return LdStRegUnsImm(2, 0, r, a, off);
}

I64 ARM_ldursbX(I64 r, I64 a, I64 off) {
  return LdStRegUnsImm(0, 2, r, a, off);
}
I64 ARM_ldurshX(I64 r, I64 a, I64 off) {
  return LdStRegUnsImm(1, 2, r, a, off);
}
I64 ARM_ldurswX(I64 r, I64 a, I64 off) {
  return LdStRegUnsImm(2, 2, r, a, off);
}

I64 ARM_stur(I64 r, I64 a, I64 off) {
  return LdStRegUnsImm(3, 0, r, a, off);
}
I64 ARM_ldur(I64 r, I64 a, I64 off) {
  return LdStRegUnsImm(3, 1, r, a, off);
}

I64 ARM_ldrsbX(I64 r, I64 a, I64 off) {
  CARMAdrRegImm addr = {a, off};
  return LdStRegUnImm(0, 2, r, &addr);
}
I64 ARM_ldrshX(I64 r, I64 a, I64 off) {
  CARMAdrRegImm addr = {a, off};
  return LdStRegUnImm(1, 2, r, &addr);
}
I64 ARM_ldrswX(I64 r, I64 a, I64 off) {
  CARMAdrRegImm addr = {a, off};
  return LdStRegUnImm(2, 2, r, &addr);
}

I64 ARM_ldrsbRegRegX(I64 r, I64 a, I64 b) {
  return LdStRegReg(0, 2, r, a, b, 0);
}
I64 ARM_ldrshRegRegX(I64 r, I64 a, I64 b) {
  return LdStRegReg(1, 2, r, a, b, 0);
}
I64 ARM_ldrswRegRegX(I64 r, I64 a, I64 b) {
  return LdStRegReg(2, 2, r, a, b, 0);
}

I64 ARM_ldrsbRegRegShiftX(I64 r, I64 a, I64 b) {
  return LdStRegReg(0, 2, r, a, b, 1);
}
I64 ARM_ldrshRegRegShiftX(I64 r, I64 a, I64 b) {
  return LdStRegReg(1, 2, r, a, b, 1);
}
I64 ARM_ldrswRegRegShiftX(I64 r, I64 a, I64 b) {
  return LdStRegReg(2, 2, r, a, b, 1);
}

I64 LdExclusive(I64 sz, I64 o0, I64 d, I64 s) {
  return (sz << 30) | (0x8 << 24) | (1 << 22) | (0x1f << 16) | (0x1f << 10) |
         (s << 5) | d;
}

I64 ARM_ldaxrb(I64 d, I64 s) {
  return LdExclusive(0, 1, d, s);
}

I64 ARM_ldaxrh(I64 d, I64 s) {
  return LdExclusive(1, 1, d, s);
}
I64 ARM_ldaxr(I64 d, I64 s) {
  return LdExclusive(2, 1, d, s);
}
I64 ARM_ldaxrX(I64 d, I64 s) {
  return LdExclusive(3, 1, d, s);
}

I64 StExclusive(I64 sz, I64 o0, I64 pass, I64 d,
                           I64 s) {
  return (sz << 30) | (0x8 << 24) | (pass << 16) | (0x1f << 10) | (s << 5) | d |
         (o0 << 15);
}

I64 ARM_stlxrb(I64 p, I64 d, I64 s) {
  return StExclusive(0, 1, p, d, s);
}

I64 ARM_stlxrh(I64 p, I64 d, I64 s) {
  return StExclusive(1, 1, p, d, s);
}
I64 ARM_stlxr(I64 p, I64 d, I64 s) {
  return StExclusive(2, 1, p, d, s);
}
I64 ARM_stlxrX(I64 p, I64 d, I64 s) {
  return StExclusive(3, 1, p, d, s);
}

I64 FpCondSel(I64 m, I64 s, I64 t, I64 d, I64 n,
                         I64 M, I64 cond) {
  return (m << 31) | (s << 29) | (0xf << 25) | (t << 22) | (1 << 21) |
         (M << 16) | (cond << 12) | (3 << 10) | (n << 5) | (d);
}

I64 ARM_fcsel(I64 d, I64 a, I64 b, I64 c) {
  return FpCondSel(0, 0, 1, d, a, b, c);
}
