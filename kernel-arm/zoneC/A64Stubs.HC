// A64Stubs.HC -- shim satisfying x86-assembler symbols referenced by
// arch-neutral Shrine compiler files, so Asm.HC, AsmInit.HC, UAsm.HC,
// OptPass789A.HC, and Back*.HC (x86) can be excluded from the aarch64 build
// without editing any arch-neutral file.

// Replaces OptPass789A.HC's I64 OptPass789A(...): forwards to the aarch64
// optimizer/backend pass with the identical signature.
I64 OptPass789A(CCmpCtrl *cc,COptReg *reg_offsets,U8 *buf,CDbgInfo **_dbg)
{
  I64 fwd_res; // TEMP-PROBE bug#5
  fwd_res=OptPass789A64(cc,reg_offsets,buf,_dbg);
  "zoneC 789A fwd out res=%d\n",fwd_res;
  return fwd_res;
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
// BUT the tail of the original also registered the 17 internal types
// (I0..F64) into cmp.internal_types[] -- the compiler cannot type a single
// expression without them (IC_STR_CONST stores internal_types[RT_U8]+1;
// with a NULL table that is 0x88 = sizeof(CHashClass), a garbage class ptr).
// Keep the registration verbatim, drop only the x86 opcode-table parsing.
// (HashAdd into cmp.asm_hash omitted: that table is never created on
// aarch64; by-name lookup of internal types is a later concern.)
U0 AsmHashLoad()
{
  I64 i;
  CInternalType *tmpit;
  CHashClass *tmpc;
  for (i=0;i<INTERNAL_TYPES_NUM;i++) {
    tmpit=&internal_types_table[i];
    tmpc=PrsClassNew;
    tmpc->type=HTT_INTERNAL_TYPE;
    tmpc->raw_type=tmpit->type;
    Bts(&tmpc->flags,Cf_INTERNAL_TYPE);
    tmpc->size=tmpit->size;
    tmpc->str=AStrNew(tmpit->name);
    cmp.internal_types[tmpc->raw_type]=tmpc;
  }
}

// Replaces UAsm.HC's U0 UAsmHashLoad(): no x86 unassembler hash table on aarch64.
U0 UAsmHashLoad()
{
}

// Run the registration at include time: the Zone C boot may never reach a
// CInit call with this stub in scope. A second run (if CInit does fire)
// only leaks a few small allocs -- it cannot corrupt the table.
AsmHashLoad;
