// zoneC C4 loader — Shrine HolyC compiler + BackA64 back end, hosted in a
// booted Aiwnios (run: aiwnios -c -t <bootdir> LoadA64.HC, all files flat
// in <bootdir>). Order follows shrine/Compiler/Compiler.PRJ (Terry's own),
// with three substitutions per C3-MANIFEST.md / C4-DESIGN.md:
//   1. Kernel headers (KernelA/B/C.HH) -> ShrineHost.HH hosted extract.
//   2. A64Defs.HH loads AFTER the extract: the host parser's silent-shadow
//      redefinition (newest-wins) makes it REPLACE any x86 REG_* values
//      that rode in with the extract.
//   3. x86 back end + assembler cut (Asm, AsmInit, UAsm, BackLib, BackFA,
//      BackFB, BackA, BackB, BackC, OptPass789A) -> zoneC aarch64 files.
// The PRJ's #exe Option() blocks (OPTf_WARN_PAREN, OPTf_KEEP_PRIVATE,
// OPTf_EXTERNS_TO_IMPORTS) are intentionally omitted in hosted mode;
// revisit at smoke time if symbol visibility misbehaves.

#include "ShrineHost.HH"	// Shrine kernel contract (hosted extract)
#include "A64Defs.HH"		// aarch64 REG_* — shadows x86 values
#include "CompilerA.HH"		// Shrine's compiler header, verbatim

// Hosted shims — Shrine kernel names absent from the Aiwnios runtime hash
// (probed 2026-07-10 against aiwnios/Src FULL_PACKAGE set):
//   sys_run_level: global in Shrine, class member in Aiwnios. Adam-server
//     bit SET = "system up", so LexExcept skips Raw(ON) boot-phase paths.
//   sys_var_init_flag/val: compiler-owned globals, absent (comments only).
//   BEqu: absent (Bt/Bts/Btr resolve C-side; BEqu was never ported).
//   Dbg: no hosted debugger (0 hits in HCRT2.BIN symbol strings) — report
//     and return; only reached on LexExcept's boot-phase branch anyway.
U32 sys_run_level = 1<<RLf_ADAM_SERVER;
U8  sys_var_init_flag = 0;
U8  sys_var_init_val  = 0;
Bool BEqu(U8 *bit_field,I64 bit_num,Bool val)
{
  Bool old=Bt(bit_field,bit_num);
  if (val)
    Bts(bit_field,bit_num);
  else
    Btr(bit_field,bit_num);
  return old;
}
U0 Dbg(U8 *msg=NULL,...)
{
  if (msg)
    PrintErr("Dbg(hosted no-op): %s\n",msg);
}
// mem_boot_base: Shrine kernel image base. Sole KEEP-set reader is
// HashEntrySize's KERNEL_MODULE_NAME branch (CHash.HC:24), which subtracts
// sizeof(CBinFile) and reads ->file_size. Hosted there is no kernel bin
// image, so back it with a zeroed CBinFile: an accidental hit reports 0.
U8 *mem_boot_base;
mem_boot_base=CAlloc(sizeof(CBinFile))+sizeof(CBinFile);
// MHeapCtrl: TempleOS returns the owning CHeapCtrl of an allocation; absent
// hosted (HCRT2.BIN count 0) and Aiwnios's allocator header is C-side,
// opaque to HolyC. Sole KEEP-set caller is PrsStmt.HC:381's same-heap
// alias check on unresolved globals; hosted there IS only one heap, so
// report one shared heap ctrl — equality holds, matching TempleOS's
// common path (both allocs from the same task heap).
U8 *MHeapCtrl(U8 *ptr)
{
  no_warn ptr;
  return NULL;
}

// Templates.HC CUT (x86 F64 op templates in inline asm; sole reader is
// BackFA.HC, itself cut in C3 — verified by grep). CExts.HC's five
// _extern CMP_TEMPLATES* binds are stripped at stage time by mkboot.sh:
// Aiwnios's _extern requires a system sym, and nothing reads cmp_templates*.
#include "CExts.HC"
CCmpGlbls	cmp;
#include "CInit.HC"
#include "CExcept.HC"
#include "LexLib.HC"
#include "Lex.HC"
CUAsmGlbls	uasm;		// only cut files use it; kept for decl parity
#include "CHash.HC"
#include "PrsLib.HC"
#include "AsmLib.HC"
#include "AsmResolve.HC"
#include "OptLib.HC"
#include "PrsExp.HC"
#include "PrsVar.HC"
#include "CMisc.HC"
#include "CMain.HC"
#include "PrsStmt.HC"
#include "OptPass012.HC"
#include "OptPass3.HC"
#include "OptPass4.HC"
#include "OptPass5.HC"
#include "OptPass6.HC"

// aarch64 back end (zoneC) — replaces BackLib/BackFA/BackFB/BackA/B/C +
// OptPass789A; A64Stubs must load after OptPass789A64 (forwards to it).
#include "Arm64Asm.HC"
#include "BackA64Lib.HC"
#include "BackA64.HC"
#include "BackA64Br.HC"
#include "BackA64F.HC"
#include "OptPass789A64.HC"
#include "A64Stubs.HC"

"zoneC: Shrine compiler + BackA64 loaded\n";

// C4 JIT smoke (bisect 2): smoke25 pinned the fault at Lex(jcc) — step 2
// printed, step 3 never did, fault PC in ANONYMOUS code (&Dbg+0x9000,
// shifting with our edits = a wild jump, not a data fault inside a named
// fn). This form: (a) prints the bound address of every symbol in the
// fault path — which world each resolved to; (b) probes LexGetChar on a
// throwaway cc BEFORE running Lex; (c) runs inside a NAMED function so
// fault frames symbolize.
// Unbounded-switch probe: same construct as Lex.HC:119 (`switch [x]` with
// a case 0xFF table-span idiom). 'A'=65 is a GAP value — it must fall to
// the end of the switch and return 3. A fault or wrong value here confirms
// the host front end mis-emits unbounded-switch jump tables.
I64 ZoneCSwTest(I64 ch)
{
  switch [ch] {
    case 0:    return 1;
    case '\n': return 2;
    case 0xFF: break; //nobound switch
  }
  return 3;
}

// Chained-comparison probe: PrsFunCall's first fresh construct class
// (PrsExp.HC:415 `IC_SQR<=tmpf->exe_addr<=IC_ATAN`). Expect 0 1 0.
I64 ZoneCChain(I64 x)
{
  if (5<=x<=10) return 1;
  return 0;
}

U0 ZoneCJitSmoke()
{
  CCmpCtrl *jcc;
  U8 *jbuf;
  U32 *jp;
  I64 jtype,ji,jn;
  I64 *jsl;
  "zoneC: addrs Dbg=%X Lex=%X CmpCtrlNew=%X LexGetChar=%X LexStmt2Bin=%X bmp=%X\n",
	&Dbg,&Lex,&CmpCtrlNew,&LexGetChar,&LexStmt2Bin,char_bmp_alpha_numeric;
  "zoneC: step 1b SwTest('A')=%d\n",ZoneCSwTest('A');
  // (SwTest(300) removed: out-of-range on a no-bounds switch is expected UB,
  // not evidence — *src++ in LexGetChar is a U8 deref, always 0..255.)
  "zoneC: step 1c SwTest(0)=%d SwTest(10)=%d\n",
	ZoneCSwTest(0),ZoneCSwTest(10);
  // (SwTest/LexGetChar dumps removed — lexer proven good since 1863d98.)
  "zoneC: step 1d Chain(4)=%d Chain(7)=%d Chain(11)=%d\n",
	ZoneCChain(4),ZoneCChain(7),ZoneCChain(11);
  jcc=CmpCtrlNew("ABC",CCF_DONT_FREE_BUF);
  "zoneC: step 2 probe jcc=%X buf=%s\n",jcc,jcc->cur_buf_ptr;
  "zoneC: step 3 LexGetChar=%d\n",LexGetChar(jcc);
  "zoneC: step 4 LexGetChar=%d\n",LexGetChar(jcc);
  CmpCtrlDel(jcc);
  "zoneC: step 5 probe cc deleted\n";
  jcc=CmpCtrlNew("\"Hello GAIA\\n\";",CCF_DONT_FREE_BUF);
  Lex(jcc);
  "zoneC: step 6 Lex ok token=%d\n",jcc->token;
  // Inline LexStmt2Bin (CMain.HC:1) with phase prints — smoke29 faulted
  // between Lex and LexStmt2Bin's return (wild jump, frame symbolized to
  // host primitive &__Gs+4). First-ever execution of the parse pipeline
  // and the BackA64 passes; this pins which phase dies. Non-AOT path only.
  jtype=0;
  Btr(&jcc->flags,CCf_PASS_TRACE_PRESENT);
  COCInit(jcc);
  "zoneC: step 6a COCInit ok\n";
  // smoke30 pinned the fault inside PrsStmt; for input `"...";` PrsStmt's
  // entire work is PrsFunCall(cc,NULL,FALSE,NULL) (PrsStmt.HC:1202, the
  // naked-string -> Print conversion). Dump its emitted code BEFORE calling,
  // then call it directly — PrsStmt afterwards degenerates to the ';'
  // branch and returns 0, so the IC stream is identical to LexStmt2Bin's.
  // Symbol table for cross-referencing blr targets in the code dumps: the
  // wild jump lands at a STABLE &__Gs+4 every run, and PrsFunCall's call
  // sites are movz/movk-built absolute constants — if one decoded target
  // equals __Gs+4, the front end mis-resolved that callee at emit time.
  "zoneC SYM __Gs=%X\n",&__Gs;
  "zoneC SYM HashFind=%X\n",&HashFind;
  "zoneC SYM Lex=%X\n",&Lex;
  "zoneC SYM LexExcept=%X\n",&LexExcept;
  "zoneC SYM ICAdd=%X\n",&ICAdd;
  "zoneC SYM COCPush=%X\n",&COCPush;
  "zoneC SYM COCPop=%X\n",&COCPop;
  "zoneC SYM COCInit=%X\n",&COCInit;
  "zoneC SYM COCMiscNew=%X\n",&COCMiscNew;
  "zoneC SYM PrsExpression=%X\n",&PrsExpression;
  "zoneC SYM OptPass012=%X\n",&OptPass012;
  "zoneC SYM OptClassFwd=%X\n",&OptClassFwd;
  "zoneC SYM StrNew=%X\n",&StrNew;
  "zoneC SYM StrLen=%X\n",&StrLen;
  "zoneC SYM PrsFunCall=%X\n",&PrsFunCall;
  jp=&PrsFunCall;
  for (ji=0;ji<2032;ji++) {
    if ((jp[ji]&0xFFE0001F)==0xD2800002 &&
        (jp[ji+1]&0xFFE0001F)==0xF2A00002 &&
        (jp[ji+2]&0xFFE0001F)==0xF2C00002 &&
        jp[ji+3]==0xF9400051) {
      jn=((jp[ji]>>5)&0xFFFF)+(((jp[ji+1]>>5)&0xFFFF)<<16)+(((jp[ji+2]>>5)&0xFFFF)<<32);
      "zoneC SLOT off=%X addr=%X\n",ji*4,jn;
      jsl=jn;
      "zoneC SLOTVAL %X\n",*jsl;
    }
  }
  "zoneC: step 6p HashFind Print=%X\n",
	HashFind("Print",jcc->htc.hash_table_lst,HTT_FUN);
  "zoneC: step 6q PrsFunCall begins\n";
  PrsFunCall(jcc,NULL,FALSE,NULL);
  "zoneC: step 6r PrsFunCall ok token=%d\n",jcc->token;
  ji=PrsStmt(jcc,,,0);
  "zoneC: step 6b PrsStmt=%d\n",ji;
  jbuf=NULL;
  if (!ji && jcc->coc.coc_head.next!=&jcc->coc.coc_head) {
    jcc->coc.coc_head.last->ic_flags&=~ICF_RES_NOT_USED;
    ICAdd(jcc,IC_RETURN_VAL2,0,0);
    ICAdd(jcc,IC_RET,0,0);
    "zoneC: step 6c ICAdd ok\n";
    jbuf=COCCompile(jcc,&jn,NULL,&jtype);
    "zoneC: step 7 COCCompile=%X size=%d type=%d\n",jbuf,jn,jtype;
  }
  if (jbuf && jbuf!=INVALID_PTR) {
    jn=MSize(jbuf);
    "zoneC: JIT buf=%X size=%d type=%d\n",jbuf,jn,jtype;
    if (jn>256) jn=256;
    jp=jbuf;
    for (ji=0;ji<(jn+3)>>2;ji++)
      "zoneC HEX %08X\n",jp[ji];
  } else
    "zoneC: LexStmt2Bin failed (%X)\n",jbuf;
  CmpCtrlDel(jcc);
}

"zoneC: JIT hello begins\n";
ZoneCJitSmoke;
"zoneC: JIT hello returned\n";
