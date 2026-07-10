// A64Stubs.HC -- shim satisfying x86-assembler symbols referenced by
// arch-neutral Shrine compiler files, so Asm.HC, AsmInit.HC, UAsm.HC,
// OptPass789A.HC, and Back*.HC (x86) can be excluded from the aarch64 build
// without editing any arch-neutral file.

// Replaces OptPass789A.HC's I64 OptPass789A(...): forwards to the aarch64
// optimizer/backend pass with the identical signature.
I64 OptPass789A(CCmpCtrl *cc,COptReg *reg_offsets,U8 *buf,CDbgInfo **_dbg)
{
  return OptPass789A64(cc,reg_offsets,buf,_dbg);
}

// Replaces UAsm.HC's I64 Un(...): x86 unassembler is unavailable on aarch64.
I64 Un(U8 *rip,I64 cnt=0x80,I64 seg_size=64)
{
  "no aarch64 unassembler\n";
  return rip;
}

// Replaces Asm.HC's U0 PrsAsmBlk(...): asm{} blocks are unsupported on aarch64.
U0 PrsAsmBlk(CCmpCtrl *cc,I64 cmp_flags)
{
  throw('AsmErr');
}

// Replaces AsmInit.HC's U0 AsmHashLoad(): no x86 asm hash table on aarch64.
U0 AsmHashLoad()
{
}

// Replaces UAsm.HC's U0 UAsmHashLoad(): no x86 unassembler hash table on aarch64.
U0 UAsmHashLoad()
{
}
