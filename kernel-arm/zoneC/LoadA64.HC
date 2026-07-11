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
  U8 *jbuf,*xbuf;
  U32 *jp;
  I64 jtype,ji,jn;
  I64 *jsl;
  "zoneC: addrs Dbg=%X Lex=%X CmpCtrlNew=%X LexGetChar=%X LexStmt2Bin=%X bmp=%X\n",
	&Dbg,&Lex,&CmpCtrlNew,&LexGetChar,&LexStmt2Bin,char_bmp_alpha_numeric;
  // (SwTest(300) removed: out-of-range on a no-bounds switch is expected UB,
  // not evidence — *src++ in LexGetChar is a U8 deref, always 0..255.)
  // (SwTest/LexGetChar dumps removed — lexer proven good since 1863d98.)
  jcc=CmpCtrlNew("ABC",CCF_DONT_FREE_BUF);
  CmpCtrlDel(jcc);
  jcc=CmpCtrlNew("PutS(\"Hello GAIA\\n\");",CCF_DONT_FREE_BUF);
  Lex(jcc);
  // Inline LexStmt2Bin (CMain.HC:1) with phase prints — smoke29 faulted
  // between Lex and LexStmt2Bin's return (wild jump, frame symbolized to
  // host primitive &__Gs+4). First-ever execution of the parse pipeline
  // and the BackA64 passes; this pins which phase dies. Non-AOT path only.
  jtype=0;
  Btr(&jcc->flags,CCf_PASS_TRACE_PRESENT);
  COCInit(jcc);
  // smoke30 pinned the fault inside PrsStmt; for input `"...";` PrsStmt's
  // entire work is PrsFunCall(cc,NULL,FALSE,NULL) (PrsStmt.HC:1202, the
  // naked-string -> Print conversion). Dump its emitted code BEFORE calling,
  // then call it directly — PrsStmt afterwards degenerates to the ';'
  // branch and returns 0, so the IC stream is identical to LexStmt2Bin's.
  // Symbol table for cross-referencing blr targets in the code dumps: the
  // wild jump lands at a STABLE &__Gs+4 every run, and PrsFunCall's call
  // sites are movz/movk-built absolute constants — if one decoded target
  // equals __Gs+4, the front end mis-resolved that callee at emit time.
  // TEMP-PROBE bug#5: dump every import slot in COCCompile's code. Pattern =
  // movz/movk/movk x2 building the slot address, then ldr x17,[x2]. Slots for
  // OptPass3..6 should hold their SYM addresses (patched); an OptPass789A
  // slot still holding 0 is the k=7 NULL call.
  jp=&COCCompile;
  for (ji=0;ji<4096;ji++) {
    if ((jp[ji]&0xFFE0001F)==0xD2800002 &&
        (jp[ji+1]&0xFFE0001F)==0xF2A00002 &&
        (jp[ji+2]&0xFFE0001F)==0xF2C00002 &&
        jp[ji+3]==0xF9400051) {
      jn=((jp[ji]>>5)&0xFFFF)+(((jp[ji+1]>>5)&0xFFFF)<<16)+(((jp[ji+2]>>5)&0xFFFF)<<32);
      jsl=jn;
    }
  }
  jp=&PrsFunCall;
  for (ji=0;ji<2032;ji++) {
    if ((jp[ji]&0xFFE0001F)==0xD2800002 &&
        (jp[ji+1]&0xFFE0001F)==0xF2A00002 &&
        (jp[ji+2]&0xFFE0001F)==0xF2C00002 &&
        jp[ji+3]==0xF9400051) {
      jn=((jp[ji]>>5)&0xFFFF)+(((jp[ji+1]>>5)&0xFFFF)<<16)+(((jp[ji+2]>>5)&0xFFFF)<<32);
      jsl=jn;
    }
  }
  //bug#7e decisive probe: &PutS resolved by LIVE aiwnios = ground truth.
  //Compare against the movz/movk-decoded x16 in this run's hex dump: match
  //=> callee resolution good; mismatch => CHashFun layout drift in the
  //staged shrine headers misreads exe_addr.
  // (PrsFunCall(NULL) scaffold removed: it was the naked-string->Print
  // wrapper, which turned the input into Print(PutS(...)) = Print(garbage
  // U0 return) -> runtime fault. PrsStmt below owns the whole statement.)
  ji=PrsStmt(jcc,,,0);
  jbuf=NULL;
  if (!ji && jcc->coc.coc_head.next!=&jcc->coc.coc_head) {
    jcc->coc.coc_head.last->ic_flags&=~ICF_RES_NOT_USED;
    ICAdd(jcc,IC_RETURN_VAL2,0,0);
    ICAdd(jcc,IC_RET,0,0);
    jbuf=COCCompile(jcc,&jn,NULL,&jtype);
  }
  if (jbuf && jbuf!=INVALID_PTR) {
    jn=MSize(jbuf);
    "zoneC: JIT buf=%X size=%d type=%d\n",jbuf,jn,jtype;
    if (jn>256) jn=256;
    jp=jbuf;
    for (ji=0;ji<(jn+3)>>2;ji++)
      "zoneC HEX %08X\n",jp[ji];
    //icache flush: the staged HolyC backend writes code with plain stores
    //and never invalidates; Apple Silicon then executes stale bytes (wild
    //PC in heap, smoke59). aiwnios's C backend flushes at arm_backend.c:5312
    //- our staged path bypasses it. WriteProtectMemCpy = memcpy +
    //sys_icache_invalidate (main.c:1514), exported at main.c:1672.
    //bug#7e: hosted W^X. Data-heap pages (where the passes write) are
    //PROT_READ|PROT_WRITE - never executable (mem.c:679); code-heap pages
    //are execute-mode per thread (MAP_JIT) - plain stores into them fault
    //(smoke63: fault in A64W writing buf). aiwnios's own pattern (mem.c:452):
    //compile into data memory, then WriteProtectMemCpy into code memory -
    //it toggles pthread_jit_write_protect_np around the copy + icache flush.
    //Baked STR_CONST addresses still point into jbuf (readable data) - fine.
    //bug#7f: the HolyC MAlloc wrapper stores m[-1].caller1/2 AFTER
    //__MAlloc restores exec-mode on the MAP_JIT page (FULL_PACKAGE.HC:46)
    //- a plain store into an exec-mode code-heap page faults on Apple
    //Silicon. __MAlloc writes its header inside the SetWriteNP(0) window,
    //so call the C export directly and skip the caller-trace writeback.
    xbuf=__MAlloc(MSize(jbuf),Fs->code_heap);
    WriteProtectMemCpy(xbuf,jbuf,MSize(jbuf));
    "zoneC: exec begins\n";
    ji=Call(xbuf);
    "zoneC: exec done res=%X\n",ji;
  } else
    "zoneC: LexStmt2Bin failed (%X)\n",jbuf;
  CmpCtrlDel(jcc);
}

"zoneC: JIT hello begins\n";
ZoneCJitSmoke;
"zoneC: JIT hello returned\n";
