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

//C4 IC_CALL_INDIRECT smoke rig: a REAL fn-ptr global in the live hash table,
//host-compiled and host-assigned by aiwnios. The staged compiler then only
//compiles the CALL through it — pure indirect path, no decl machinery.
U0 (*zc_fp)(U8 *st);
zc_fp=&PutS;

//C4 slice 4: ramfb GaiaPlot module hosted stubs. GaiaPlot imports FbBase()
///FbWidth() (proven slice-3 import-by-name). The stubs are DELIBERATELY named
//differently from the imports: if a hosted symbol matched the import name,
//Shrine would bind the call to its absolute address (movz/movk+blr) and the
//module would NOT be relocatable. Undefined import names stay named literal
//islands (ldr x16), which we patch here (hosted proof) and the kernel patches
//by name at load. See the abs-vs-island finding in the commit.
U32 *zc_hosted_fb=NULL;
U32 *ZcFbBaseStub() {return zc_hosted_fb;}
I64  ZcFbWidthStub() {return 640;}

U0 ZoneCJitSmoke()
{
  CCmpCtrl *jcc;
  U8 *jbuf,*xbuf,*b;
  U32 *jp;
  CHashFun *jfun;
  I64 jtype,ji,jn,sz,retend,zeroes;
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
    //bug#8 smoke: variadic Print through the staged pipeline. Print
    //resolves to the LIVE aiwnios Print (extern U0 Print(U8*,...),
    //FULL_PACKAGE.HC:121) from the shared task hash - the same binding
    //mechanism that carried PutS for smoke68.
    jcc=CmpCtrlNew("Print(\"Val=%d\\n\",42);",CCF_DONT_FREE_BUF);
    Lex(jcc);
    jtype=0;
    Btr(&jcc->flags,CCf_PASS_TRACE_PRESENT);
    COCInit(jcc);
    ji=PrsStmt(jcc,,,0);
    jbuf=NULL;
    if (!ji && jcc->coc.coc_head.next!=&jcc->coc.coc_head) {
      jcc->coc.coc_head.last->ic_flags&=~ICF_RES_NOT_USED;
      ICAdd(jcc,IC_RETURN_VAL2,0,0);
      ICAdd(jcc,IC_RET,0,0);
      jbuf=COCCompile(jcc,&jn,NULL,&jtype);
    }
    if (jbuf) {
      xbuf=__MAlloc(MSize(jbuf),Fs->code_heap);
      WriteProtectMemCpy(xbuf,jbuf,MSize(jbuf));
      "zoneC: exec2 begins\n";
      ji=Call(xbuf);
      "zoneC: exec2 done res=%X\n",ji;
    } else
      "zoneC: exec2 compile failed\n";
    //C4 IC_CALL_INDIRECT2 smoke: try/catch — PrsStmt compiles the try body
    //as a sub-fun and calls it through IC_CALL_INDIRECT2 (&tmp_try->exe_addr,
    //PrsStmt.HC:866). No exception is thrown, so the catch body must not run.
    jcc=CmpCtrlNew("try {\"Try OK\\n\";} catch {\"NOCATCH\\n\";}",CCF_DONT_FREE_BUF);
    Lex(jcc);
    jtype=0;
    Btr(&jcc->flags,CCf_PASS_TRACE_PRESENT);
    COCInit(jcc);
    ji=PrsStmt(jcc,,,0);
    jbuf=NULL;
    if (!ji && jcc->coc.coc_head.next!=&jcc->coc.coc_head) {
      jcc->coc.coc_head.last->ic_flags&=~ICF_RES_NOT_USED;
      ICAdd(jcc,IC_RETURN_VAL2,0,0);
      ICAdd(jcc,IC_RET,0,0);
      jbuf=COCCompile(jcc,&jn,NULL,&jtype);
    }
    if (jbuf && jbuf!=INVALID_PTR) {
      xbuf=__MAlloc(MSize(jbuf),Fs->code_heap);
      WriteProtectMemCpy(xbuf,jbuf,MSize(jbuf));
      "zoneC: exec3 begins\n";
      ji=Call(xbuf);
      "zoneC: exec3 done res=%X\n",ji;
    } else
      "zoneC: exec3 compile failed (%X)\n",jbuf;
    //C4 IC_CALL_INDIRECT smoke: call through the zc_fp global. PrsExp's
    //indirect path: IC_PUSH_REGS(1<<REG_RAX) stashes the ptr below the args,
    //then IC_CALL_INDIRECT with ic_data=(slots)<<3 (PrsExp.HC:558).
    jcc=CmpCtrlNew("zc_fp(\"Ind OK\\n\");",CCF_DONT_FREE_BUF);
    Lex(jcc);
    jtype=0;
    Btr(&jcc->flags,CCf_PASS_TRACE_PRESENT);
    COCInit(jcc);
    ji=PrsStmt(jcc,,,0);
    jbuf=NULL;
    if (!ji && jcc->coc.coc_head.next!=&jcc->coc.coc_head) {
      jcc->coc.coc_head.last->ic_flags&=~ICF_RES_NOT_USED;
      ICAdd(jcc,IC_RETURN_VAL2,0,0);
      ICAdd(jcc,IC_RET,0,0);
      jbuf=COCCompile(jcc,&jn,NULL,&jtype);
    }
    if (jbuf && jbuf!=INVALID_PTR) {
      xbuf=__MAlloc(MSize(jbuf),Fs->code_heap);
      WriteProtectMemCpy(xbuf,jbuf,MSize(jbuf));
      "zoneC: exec4 begins\n";
      ji=Call(xbuf);
      "zoneC: exec4 done res=%X\n",ji;
    } else
      "zoneC: exec4 compile failed (%X)\n",jbuf;
    //C5 fn-def smoke A: PrsStmt hits the U0 decl, PrsFun compiles ZC5's
    //body through the FULL backend (IC_ENTER/IC_LEAVE) and stores exe_addr
    //in the live hash table. Fs-heap buffers execute under aiwnios hosting
    //(proven: round 3 ran the try-body sub-fun straight from COCCompile).
    //The def leaves the top-level COC empty - no exec block for this one.
    jcc=CmpCtrlNew("U0 ZC5() {PutS(\"Def OK\\n\");}",CCF_DONT_FREE_BUF);
    Lex(jcc);
    jtype=0;
    Btr(&jcc->flags,CCf_PASS_TRACE_PRESENT);
    COCInit(jcc);
    ji=PrsStmt(jcc,,,0);
    "zoneC: def5 done err=%X\n",ji;
    //C5 fn-def smoke B: call it - IC_CALL through ZC5's fresh exe_addr:
    //the first JIT-compiled HolyC fn called from JIT-compiled code.
    jcc=CmpCtrlNew("ZC5;",CCF_DONT_FREE_BUF);
    Lex(jcc);
    jtype=0;
    Btr(&jcc->flags,CCf_PASS_TRACE_PRESENT);
    COCInit(jcc);
    ji=PrsStmt(jcc,,,0);
    jbuf=NULL;
    if (!ji && jcc->coc.coc_head.next!=&jcc->coc.coc_head) {
      jcc->coc.coc_head.last->ic_flags&=~ICF_RES_NOT_USED;
      ICAdd(jcc,IC_RETURN_VAL2,0,0);
      ICAdd(jcc,IC_RET,0,0);
      jbuf=COCCompile(jcc,&jn,NULL,&jtype);
    }
    if (jbuf && jbuf!=INVALID_PTR) {
      xbuf=__MAlloc(MSize(jbuf),Fs->code_heap);
      WriteProtectMemCpy(xbuf,jbuf,MSize(jbuf));
      "zoneC: exec5 begins\n";
      ji=Call(xbuf);
      "zoneC: exec5 done res=%X\n",ji;
    } else
      "zoneC: exec5 compile failed (%X)\n",jbuf;
    //C6 frame-var smoke: args + locals through the full frame path.
    //ZC6 a,b arrive as MDF_DISP RBP+16/+24 (A64FrameRef remaps to x29+32/+48,
    //16B push slots); local c lives below x29 (1:1). NoRegOpt mkboot transform
    //keeps all three frame-resident (x86 reg-var pool collides on arm64).
    jcc=CmpCtrlNew("I64 ZC6(I64 a,I64 b) {I64 c; c=a*2; c=c+b; return c;}",CCF_DONT_FREE_BUF);
    Lex(jcc);
    jtype=0;
    Btr(&jcc->flags,CCf_PASS_TRACE_PRESENT);
    COCInit(jcc);
    ji=PrsStmt(jcc,,,0);
    "zoneC: def6 done err=%X\n",ji;
    jcc=CmpCtrlNew("ZC6(20,1);",CCF_DONT_FREE_BUF);
    Lex(jcc);
    jtype=0;
    Btr(&jcc->flags,CCf_PASS_TRACE_PRESENT);
    COCInit(jcc);
    ji=PrsStmt(jcc,,,0);
    jbuf=NULL;
    if (!ji && jcc->coc.coc_head.next!=&jcc->coc.coc_head) {
      jcc->coc.coc_head.last->ic_flags&=~ICF_RES_NOT_USED;
      ICAdd(jcc,IC_RETURN_VAL2,0,0);
      ICAdd(jcc,IC_RET,0,0);
      jbuf=COCCompile(jcc,&jn,NULL,&jtype);
    }
    if (jbuf && jbuf!=INVALID_PTR) {
      xbuf=__MAlloc(MSize(jbuf),Fs->code_heap);
      WriteProtectMemCpy(xbuf,jbuf,MSize(jbuf));
      "zoneC: exec6 begins\n";
      ji=Call(xbuf);
      "zoneC: exec6 done Res6=%d\n",ji;
    } else
      "zoneC: exec6 compile failed (%X)\n",jbuf;
    jcc=CmpCtrlNew("I64 ZC7(I64 n) {I64 s,i; s=0; i=0; while (i<n) {s=s+i; i=i+1;} if (s>9) s=s+100; return s;}",CCF_DONT_FREE_BUF);
    Lex(jcc);
    jtype=0;
    Btr(&jcc->flags,CCf_PASS_TRACE_PRESENT);
    COCInit(jcc);
    ji=PrsStmt(jcc,,,0);
    "zoneC: def7 done err=%X\n",ji;
    jcc=CmpCtrlNew("ZC7(5);",CCF_DONT_FREE_BUF);
    Lex(jcc);
    jtype=0;
    Btr(&jcc->flags,CCf_PASS_TRACE_PRESENT);
    COCInit(jcc);
    ji=PrsStmt(jcc,,,0);
    jbuf=NULL;
    if (!ji && jcc->coc.coc_head.next!=&jcc->coc.coc_head) {
      jcc->coc.coc_head.last->ic_flags&=~ICF_RES_NOT_USED;
      ICAdd(jcc,IC_RETURN_VAL2,0,0);
      ICAdd(jcc,IC_RET,0,0);
      jbuf=COCCompile(jcc,&jn,NULL,&jtype);
    }
    if (jbuf && jbuf!=INVALID_PTR) {
      xbuf=__MAlloc(MSize(jbuf),Fs->code_heap);
      WriteProtectMemCpy(xbuf,jbuf,MSize(jbuf));
      "zoneC: exec7 begins\n";
      ji=Call(xbuf);
      "zoneC: exec7 done Res7=%d\n",ji;
    } else
      "zoneC: exec7 compile failed (%X)\n",jbuf;
    //TEMP-PROBE spec99: fn-ptr argument — fixed-arg call through a frame home.
    jcc=CmpCtrlNew("U0 Z90(U0 (*fp)(U8 *s)) {fp(\"d90 via arg\\n\");}",CCF_DONT_FREE_BUF);
    Lex(jcc);
    jtype=0;
    Btr(&jcc->flags,CCf_PASS_TRACE_PRESENT);
    COCInit(jcc);
    ji=PrsStmt(jcc,,,0);
    "zoneC: def90 done err=%X\n",ji;
    jcc=CmpCtrlNew("Z90(&PutS);",CCF_DONT_FREE_BUF);
    Lex(jcc);
    jtype=0;
    Btr(&jcc->flags,CCf_PASS_TRACE_PRESENT);
    COCInit(jcc);
    ji=PrsStmt(jcc,,,0);
    jbuf=NULL;
    if (!ji && jcc->coc.coc_head.next!=&jcc->coc.coc_head) {
      jcc->coc.coc_head.last->ic_flags&=~ICF_RES_NOT_USED;
      ICAdd(jcc,IC_RETURN_VAL2,0,0);
      ICAdd(jcc,IC_RET,0,0);
      jbuf=COCCompile(jcc,&jn,NULL,&jtype);
    }
    if (jbuf && jbuf!=INVALID_PTR) {
      xbuf=__MAlloc(MSize(jbuf),Fs->code_heap);
      WriteProtectMemCpy(xbuf,jbuf,MSize(jbuf));
      "zoneC: exec90 begins\n";
      ji=Call(xbuf);
      "zoneC: exec90 done res=%X\n",ji;
    } else
      "zoneC: exec90 compile failed (%X)\n",jbuf;
    jcc=CmpCtrlNew("U0 Z91(U0 (*fp)(U8 *s),I64 x) {I64 i; i=0; while (i<x) {fp(\"d91 tick\\n\"); i=i+1;}}",CCF_DONT_FREE_BUF);
    Lex(jcc);
    jtype=0;
    Btr(&jcc->flags,CCf_PASS_TRACE_PRESENT);
    COCInit(jcc);
    ji=PrsStmt(jcc,,,0);
    "zoneC: def91 done err=%X\n",ji;
    jcc=CmpCtrlNew("Z91(&PutS,2);",CCF_DONT_FREE_BUF);
    Lex(jcc);
    jtype=0;
    Btr(&jcc->flags,CCf_PASS_TRACE_PRESENT);
    COCInit(jcc);
    ji=PrsStmt(jcc,,,0);
    jbuf=NULL;
    if (!ji && jcc->coc.coc_head.next!=&jcc->coc.coc_head) {
      jcc->coc.coc_head.last->ic_flags&=~ICF_RES_NOT_USED;
      ICAdd(jcc,IC_RETURN_VAL2,0,0);
      ICAdd(jcc,IC_RET,0,0);
      jbuf=COCCompile(jcc,&jn,NULL,&jtype);
    }
    if (jbuf && jbuf!=INVALID_PTR) {
      xbuf=__MAlloc(MSize(jbuf),Fs->code_heap);
      WriteProtectMemCpy(xbuf,jbuf,MSize(jbuf));
      "zoneC: exec91 begins\n";
      ji=Call(xbuf);
      "zoneC: exec91 done res=%X\n",ji;
    } else
      "zoneC: exec91 compile failed (%X)\n",jbuf;
    //C4 slice 1: module export — pure compute, no calls/strings = position-independent
    jcc=CmpCtrlNew("I64 GaiaMod() {I64 s,i; s=0; i=0; while (i<10) {s=s+i; i=i+1;} if (s>40) s=s+55; return s;}",CCF_DONT_FREE_BUF);
    Lex(jcc);
    jtype=0;
    Btr(&jcc->flags,CCf_PASS_TRACE_PRESENT);
    COCInit(jcc);
    ji=PrsStmt(jcc,,,0);
    "zoneC: def GaiaMod done err=%X\n",ji;
    jcc=CmpCtrlNew("GaiaMod;",CCF_DONT_FREE_BUF);
    Lex(jcc);
    jtype=0;
    Btr(&jcc->flags,CCf_PASS_TRACE_PRESENT);
    COCInit(jcc);
    ji=PrsStmt(jcc,,,0);
    jbuf=NULL;
    if (!ji && jcc->coc.coc_head.next!=&jcc->coc.coc_head) {
      jcc->coc.coc_head.last->ic_flags&=~ICF_RES_NOT_USED;
      ICAdd(jcc,IC_RETURN_VAL2,0,0);
      ICAdd(jcc,IC_RET,0,0);
      jbuf=COCCompile(jcc,&jn,NULL,&jtype);
    }
    if (jbuf && jbuf!=INVALID_PTR) {
      xbuf=__MAlloc(MSize(jbuf),Fs->code_heap);
      WriteProtectMemCpy(xbuf,jbuf,MSize(jbuf));
      ji=Call(xbuf);
      "zoneC: GaiaModHosted=%d\n",ji;
      jfun=HashFind("GaiaMod",Fs->hash_table,HTT_FUN);
      b=jfun->exe_addr;
      jp=b;
      sz=0;
      for (ji=0;ji<64;ji++)
        if (jp[ji]==0xD65F03C0)
          sz=(ji+1)<<2;
      if (!sz)
        "zoneC: GaiaMod export FAIL\n";
      else {
        "zoneC: GaiaMod bytes begin sz=%d\n",sz;
        for (ji=0;ji<sz;ji++) {
          "0x%02X,",b[ji];
          if ((ji+1)%12==0)
            "\n";
        }
        "\nzoneC: GaiaMod bytes end\n";
      }
    } else
      "zoneC: GaiaMod compile failed (%X)\n",jbuf;
    //C4 slice 2: GaiaSay export — fn-ptr voice + PC-relative string
    jcc=CmpCtrlNew("U0 GaiaSay(U0 (*kputs)(U8 *s)) {kputs(\"GAIA-OS: love=1 — spoken by Shrine/zoneC\\n\");}",CCF_DONT_FREE_BUF);
    Lex(jcc);
    jtype=0;
    Btr(&jcc->flags,CCf_PASS_TRACE_PRESENT);
    COCInit(jcc);
    ji=PrsStmt(jcc,,,0);
    "zoneC: def GaiaSay done err=%X\n",ji;
    jcc=CmpCtrlNew("GaiaSay(&PutS);",CCF_DONT_FREE_BUF);
    Lex(jcc);
    jtype=0;
    Btr(&jcc->flags,CCf_PASS_TRACE_PRESENT);
    COCInit(jcc);
    ji=PrsStmt(jcc,,,0);
    jbuf=NULL;
    if (!ji && jcc->coc.coc_head.next!=&jcc->coc.coc_head) {
      jcc->coc.coc_head.last->ic_flags&=~ICF_RES_NOT_USED;
      ICAdd(jcc,IC_RETURN_VAL2,0,0);
      ICAdd(jcc,IC_RET,0,0);
      jbuf=COCCompile(jcc,&jn,NULL,&jtype);
    }
    if (jbuf && jbuf!=INVALID_PTR) {
      xbuf=__MAlloc(MSize(jbuf),Fs->code_heap);
      WriteProtectMemCpy(xbuf,jbuf,MSize(jbuf));
      ji=Call(xbuf);
      "zoneC: GaiaSayHosted ok\n";
      jfun=HashFind("GaiaSay",Fs->hash_table,HTT_FUN);
      b=jfun->exe_addr;
      jn=MSize(b);
      retend=0;
      jp=b;
      for (ji=0;ji+3<jn;ji+=4)
        if (jp[ji>>2]==0xD65F03C0)
          retend=ji+4;
      if (!retend)
        "zoneC: GaiaSay export FAIL\n";
      else {
        // Ship through the first 8-byte zero run after the final RET. This
        // retains the ADR-referenced NUL string emitted after the code.
        sz=retend;
        zeroes=0;
        for (ji=retend;ji<jn;ji++) {
          if (b[ji])
            zeroes=0;
          else if (++zeroes>=8) {
            sz=ji+1;
            break;
          }
          sz=ji+1;
        }
        "zoneC: GaiaSay bytes begin sz=%d\n",sz;
        for (ji=0;ji<sz;ji++) {
          "0x%02X,",b[ji];
          if ((ji+1)%12==0)
            "\n";
        }
        "\nzoneC: GaiaSay bytes end\n";
      }
    } else
      "zoneC: GaiaSay compile failed (%X)\n",jbuf;
    // C4 slice 3a: shrine/Compiler/AsmInit.HC:67 creates cmp.asm_hash and
    // :208 chains adam_task->hash_table to it; CMain.HC:68 chains AOT globals
    // to cmp.asm_hash. Asm layer excluded by C3, so host symbols are its tail.
    if (!cmp.asm_hash) cmp.asm_hash=Fs->hash_table; //asm layer excluded (C3): AOT chain bottom = host syms, mirroring donor tail
    "zoneC: asm_hash=%X next=%X FsHash=%X\n",cmp.asm_hash,cmp.asm_hash->next,Fs->hash_table;

    // C4 slice 3a permanent AOT gates: CmpBuf fn defs, import/export queue,
    // emitted A64 words, and the existing GaiaMod source shape.
    I64 aot_err,aot_warn,aot_i,aot_dump,aot_bytes,aot_rip,aot_main_rip;
    I64 aot_import_rip,aot_island,aot_imports;
    Bool aot_ret;
    U8 *aot_buf;
    U32 *aot_words;
    CAOT *aot;
    CAOTImportExport *aot_ie;
    aot=NULL;
    aot_err=0;
    aot_warn=0;
    try {
      aot=CmpBuf("I64 AotProbe() {return 42;}",NULL,&aot_err,&aot_warn,0);
      aot_buf=NULL;
      aot_rip=0;
      if (aot) {
        aot_buf=aot->buf;
        aot_rip=aot->rip;
      }
      "zoneC: AOT gate a aot=%X err=%d warn=%d buf=%X rip=%d\n",
        aot,aot_err,aot_warn,aot_buf,aot_rip;
    } catch {
      "zoneC: AOT gate a CAUGHT %X\n",Fs->except_ch;
      Fs->catch_except=TRUE;
    }
    if (aot) {
      aot_ie=aot->next_ie;
      aot_i=0;
      aot_main_rip=INVALID_PTR;
      while (aot_ie!=&aot->next_ie && aot_i<128) {
        if (aot_ie->type==IET_MAIN) aot_main_rip=aot_ie->rip;
        if (aot_ie->str)
          "zoneC: AOT gate b ie=%d type=%d rip=%X str=%s\n",
            aot_i,aot_ie->type,aot_ie->rip,aot_ie->str;
        else
          "zoneC: AOT gate b ie=%d type=%d rip=%X str=NULL\n",
            aot_i,aot_ie->type,aot_ie->rip;
        aot_ie=aot_ie->next;
        aot_i++;
      }
      if (aot_i==128)
        "zoneC: AOT gate b count=%d (guard)\n",aot_i;
      else
        "zoneC: AOT gate b count=%d\n",aot_i;
      aot_dump=aot->rip;
      // Root CmpBuf CAOT uses rip as origin (zero); its actual buffer length
      // is aot_U8s. IET_MAIN is absent for a bare function definition.
      if (!aot_dump) aot_dump=aot->aot_U8s;
      if (aot_dump>160) aot_dump=160;
      aot_words=aot->buf;
      aot_ret=FALSE;
      "zoneC: AOT gate c dump bytes=%d (rip=%d main=%X aot_U8s=%d)\n",
        aot_dump,aot->rip,aot_main_rip,aot->aot_U8s;
      for (aot_i=0;aot_i<aot_dump>>2;aot_i++) {
        "zoneC: AOT W[%02d]=%08X\n",aot_i,aot_words[aot_i];
        if (aot_words[aot_i]==0xD65F03C0) aot_ret=TRUE;
      }
      if (aot_words[0]==0xF81F0FFE && aot_ret)
        "zoneC: AOT gate c A64=recognizable (prologue=%08X ret=%d)\n",
          aot_words[0],aot_ret;
      else
        "zoneC: AOT gate c A64=unrecognized (prologue=%08X ret=%d)\n",
          aot_words[0],aot_ret;
    } else
      "zoneC: AOT gate b/c skipped: NULL AOT\n";

    aot=NULL;
    aot_err=0;
    aot_warn=0;
    try {
      // Keep the GaiaMod body; distinct AOT name avoids overwriting the live
      // hosted export through the deliberately shared hash-chain tail.
      aot=CmpBuf("I64 GaiaModAOT() {I64 s,i; s=0; i=0; while (i<10) {s=s+i; i=i+1;} if (s>40) s=s+55; return s;}",NULL,&aot_err,&aot_warn,0);
      aot_bytes=0;
      aot_rip=0;
      if (aot) {
        aot_bytes=aot->aot_U8s;
        aot_rip=aot->rip;
      }
      "zoneC: AOT gate d GaiaMod aot=%X err=%d warn=%d bytes=%d rip=%d\n",
        aot,aot_err,aot_warn,aot_bytes,aot_rip;
    } catch {
      "zoneC: AOT gate d CAUGHT %X\n",Fs->except_ch;
      Fs->catch_except=TRUE;
    }

    // C4 slice 3b: imported AOT call. The target is an 8-byte literal island;
    // its IET_IMM_I64 record is deliberately compatible with the donor's raw
    // I64 patcher. Import replaces the temporary extern probe (IC_CALL_EXTERN
    // remains intentionally unhandled on A64).
    aot=NULL;
    aot_err=0;
    aot_warn=0;
    try {
      aot=CmpBuf("import U0 KPutS(U8 *s); U0 AotHello() {KPutS(\"AOT: the Temple grows\\n\");}",NULL,&aot_err,&aot_warn,0);
      aot_bytes=0;
      aot_rip=0;
      if (aot) {
        aot_bytes=aot->aot_U8s;
        aot_rip=aot->rip;
      }
      "zoneC: 3b gate a import aot=%X err=%d warn=%d bytes=%d rip=%d\n",
        aot,aot_err,aot_warn,aot_bytes,aot_rip;
    } catch {
      "zoneC: 3b gate a CAUGHT %X\n",Fs->except_ch;
      Fs->catch_except=TRUE;
    }
    if (aot) {
      aot_ie=aot->next_ie;
      aot_i=0;
      aot_imports=0;
      aot_import_rip=INVALID_PTR;
      aot_main_rip=INVALID_PTR;
      while (aot_ie!=&aot->next_ie && aot_i<128) {
        //Hosted CmpBuf exposes IET_MAIN as runtime type 16 (not the Shrine
        //macro value); AotHello is the root entry at offset zero.
        if (aot_ie->type==16 && aot_ie->str &&
            !StrCmp(aot_ie->str,"AotHello"))
          aot_main_rip=aot_ie->rip;
        if (aot_ie->type==IET_IMM_I64 && aot_ie->str) {
          //This source has one named immediate import: KPutS. Other NULL
          //IET_IMM_I64 entries are AOT-internal relocation records.
          aot_imports++;
          aot_import_rip=aot_ie->rip;
        }
        if (aot_ie->str)
          "zoneC: 3b gate b ie=%d type=%d rip=%X str=%s\n",
            aot_i,aot_ie->type,aot_ie->rip,aot_ie->str;
        else
          "zoneC: 3b gate b ie=%d type=%d rip=%X str=NULL\n",
            aot_i,aot_ie->type,aot_ie->rip;
        aot_ie=aot_ie->next;
        aot_i++;
      }
      "zoneC: 3b gate b count=%d named-imm64=%d KPutS-rip=%X\n",
        aot_i,aot_imports,aot_import_rip;
      aot_dump=aot->aot_U8s;
      if (aot_dump>240) aot_dump=240;
      aot_words=aot->buf;
      aot_island=INVALID_PTR;
      "zoneC: 3b gate c dump bytes=%d (aot_U8s=%d)\n",aot_dump,aot->aot_U8s;
      for (aot_i=0;aot_i<aot_dump>>2;aot_i++) {
        "zoneC: 3b gate c W[%02d]=%08X\n",aot_i,aot_words[aot_i];
        if (aot_i+4<aot_dump>>2 &&
            aot_words[aot_i]==0x58000050 && aot_words[aot_i+1]==0x14000003 &&
            !aot_words[aot_i+2] && !aot_words[aot_i+3] &&
            aot_words[aot_i+4]==0xD63F0200)
          aot_island=aot_i;
      }
      if (aot_island!=INVALID_PTR)
        "zoneC: 3b gate c island W[%d] rip=%X expected=%X match=%d\n",
          aot_island,aot_import_rip,aot_island*4+8,
          aot_import_rip==aot_island*4+8;
      else
        "zoneC: 3b gate c island MISSING rip=%X\n",aot_import_rip;

      //C4 slice 3c-hosted: IET_IMM_I64 is a raw-I64 site; patch data before
      //the W^X copy, then enter the root IET_MAIN (AotHello at offset zero).
      aot_i=0;
      aot_ie=aot->next_ie;
      while (aot_ie!=&aot->next_ie) {
        if (aot_ie->type==IET_IMM_I64 && aot_ie->str &&
            !StrCmp(aot_ie->str,"KPutS")) {
          *((aot->buf+aot->rip+aot_ie->rip)(I64 *))=&PutS;
          aot_i++;
        }
        aot_ie=aot_ie->next;
      }
      if (aot_i==1 && aot_main_rip==0) {
        xbuf=__MAlloc(aot->aot_U8s,Fs->code_heap);
        WriteProtectMemCpy(xbuf,aot->buf+aot->rip,aot->aot_U8s);
        Call(xbuf);
        "zoneC: 3c hosted AOT call ok\n";
      } else
        "zoneC: 3c hosted AOT call FAIL imports=%d main=%X\n",
          aot_i,aot_main_rip;
    } else
      "zoneC: 3b gate b/c skipped: NULL AOT\n";
    //C4 slice 3c: AOT module export — kernel consumes these unpatched bytes
    //and patches IET_IMM_I64 imports by their recorded names.
    aot=NULL;
    aot_err=0;
    aot_warn=0;
    try {
      aot=CmpBuf("import U0 KPutS(U8 *s); U0 AotBoot() {KPutS(\"AOT-linked module: love=1, the Temple grows\\n\");}",NULL,&aot_err,&aot_warn,0);
      "zoneC: AotBoot compile aot=%X err=%d warn=%d\n",aot,aot_err,aot_warn;
    } catch {
      "zoneC: AotBoot CAUGHT %X\n",Fs->except_ch;
      Fs->catch_except=TRUE;
    }
    if (aot) {
      "zoneC: AotBoot bytes begin sz=%d\n",aot->aot_U8s;
      b=aot->buf+aot->rip;
      for (aot_i=0;aot_i<aot->aot_U8s;aot_i++) {
        "0x%02X,",b[aot_i];
        if ((aot_i+1)%12==0) "\n";
      }
      "\nzoneC: AotBoot bytes end\n";
      aot_ie=aot->next_ie;
      aot_i=0;
      aot_imports=0;
      aot_main_rip=INVALID_PTR;
      while (aot_ie!=&aot->next_ie && aot_i<128) {
        if (aot_ie->type==IET_IMM_I64 && aot_ie->str) {
          "zoneC: AotBoot import %s rip=%d\n",aot_ie->str,aot_ie->rip;
          aot_imports++;
        }
        // Hosted CmpBuf's IET_MAIN ABI is runtime type 16.
        if (aot_ie->type==16 && aot_ie->str && !StrCmp(aot_ie->str,"AotBoot")) {
          aot_main_rip=aot_ie->rip;
          "zoneC: AotBoot entry %s rip=%d\n",aot_ie->str,aot_ie->rip;
        }
        aot_ie=aot_ie->next;
        aot_i++;
      }
      // Hosted sanity: patch only this W^X copy; export above stays raw.
      aot_ie=aot->next_ie;
      aot_i=0;
      while (aot_ie!=&aot->next_ie) {
        if (aot_ie->type==IET_IMM_I64 && aot_ie->str &&
            !StrCmp(aot_ie->str,"KPutS")) {
          *((aot->buf+aot->rip+aot_ie->rip)(I64 *))=&PutS;
          aot_i++;
        }
        aot_ie=aot_ie->next;
      }
      if (aot_i==aot_imports && aot_main_rip!=INVALID_PTR) {
        xbuf=__MAlloc(aot->aot_U8s,Fs->code_heap);
        WriteProtectMemCpy(xbuf,aot->buf+aot->rip,aot->aot_U8s);
        Call(xbuf+aot_main_rip);
        "zoneC: AotBootHosted ok\n";
      } else
        "zoneC: AotBootHosted FAIL imports=%d patched=%d entry=%d\n",
          aot_imports,aot_i,aot_main_rip;
    } else
      "zoneC: AotBoot export FAIL\n";

    "zoneC: 3b gate d regression = rounds 1-9 + GaiaModHosted + GaiaSayHosted + 3a above\n";

    //C4 slice 4: GaiaPlot — first HolyC-drawn pixels. Single entry fn (inter-
    //AOT-fn calls emit absolute IC_CALL w/ unset callee addr = unproven, so
    //GrPlot0's algorithm d=w*y+x; body[d]=color is inlined into the draw
    //loops). Imports FbBase()/FbWidth() by name; direct U32 XRGB stores.
    aot=NULL;
    aot_err=0;
    aot_warn=0;
    try {
      //zoneC codegen debt: a binary op `memvar <op> live-x0-temp` clobbers the
      //temp (the memvar load reuses x0). It only bites WITHIN one statement;
      //across statements the allocator spills correctly. Rounds 1-9 never fed
      //a sub-expr result into another op vs memory. Workaround: never write
      //`off = off <op> memvar` inline — split so the memvar is a fresh local
      //(z=x; off=off+z), forcing off to spill and the add to be var+var.
      //GrPlot0 inlined (inter-AOT-fn calls emit abs IC_CALL w/ unset callee).
      aot=CmpBuf("import I64 FbBase(); import I64 FbWidth(); U0 GaiaPlot() {I64 b,w,x,y,off,z,col; b=FbBase(); w=FbWidth(); y=100; while (y<=379) {x=100; while (x<=539) {off=w*y; z=x; off=off+z; off=off*4; z=b; off=off+z; *(off(U32*))=0x00FF00FF; x=x+1;} y=y+1;} x=0; while (x<280) {y=100; z=x; y=y+z; off=w*y; z=y; off=off+z; off=off*4; z=b; off=off+z; *(off(U32*))=0x00FFFF00; col=539; col=col-x; off=w*y; z=col; off=off+z; off=off*4; z=b; off=off+z; *(off(U32*))=0x00FFFF00; x=x+1;}}",NULL,&aot_err,&aot_warn,0);
      "zoneC: GaiaPlot compile aot=%X err=%d warn=%d\n",aot,aot_err,aot_warn;
    } catch {
      "zoneC: GaiaPlot CAUGHT %X\n",Fs->except_ch;
      Fs->catch_except=TRUE;
    }
    if (aot) {
      "zoneC: GaiaPlot bytes begin sz=%d\n",aot->aot_U8s;
      b=aot->buf+aot->rip;
      for (aot_i=0;aot_i<aot->aot_U8s;aot_i++) {
        "0x%02X,",b[aot_i];
        if ((aot_i+1)%12==0) "\n";
      }
      "\nzoneC: GaiaPlot bytes end\n";
      aot_ie=aot->next_ie;
      aot_i=0;
      aot_imports=0;
      aot_main_rip=INVALID_PTR;
      while (aot_ie!=&aot->next_ie && aot_i<128) {
        if (aot_ie->type==IET_IMM_I64 && aot_ie->str) {
          "zoneC: GaiaPlot import %s rip=%d\n",aot_ie->str,aot_ie->rip;
          aot_imports++;
        }
        if (aot_ie->type==16 && aot_ie->str && !StrCmp(aot_ie->str,"GaiaPlot")) {
          aot_main_rip=aot_ie->rip;
          "zoneC: GaiaPlot entry %s rip=%d\n",aot_ie->str,aot_ie->rip;
        }
        aot_ie=aot_ie->next;
        aot_i++;
      }
      //Hosted proof: patch this W^X copy's import islands to the host stubs.
      aot_ie=aot->next_ie;
      aot_i=0;
      while (aot_ie!=&aot->next_ie) {
        if (aot_ie->type==IET_IMM_I64 && aot_ie->str) {
          if (!StrCmp(aot_ie->str,"FbBase")) {
            *((aot->buf+aot->rip+aot_ie->rip)(I64 *))=&ZcFbBaseStub;
            aot_i++;
          } else if (!StrCmp(aot_ie->str,"FbWidth")) {
            *((aot->buf+aot->rip+aot_ie->rip)(I64 *))=&ZcFbWidthStub;
            aot_i++;
          }
        }
        aot_ie=aot_ie->next;
      }
      if (aot_i==aot_imports && aot_main_rip!=INVALID_PTR) {
        zc_hosted_fb=MAlloc(640*480*4);
        xbuf=__MAlloc(aot->aot_U8s,Fs->code_heap);
        WriteProtectMemCpy(xbuf,aot->buf+aot->rip,aot->aot_U8s);
        Call(xbuf+aot_main_rip);
        "zoneC: GaiaPlotHosted rect(300,240)=%08X diagTL(150,150)=%08X diagTR(489,150)=%08X corner(0,0)=%08X edge(101,101)=%08X\n",
          zc_hosted_fb[640*240+300],zc_hosted_fb[640*150+150],
          zc_hosted_fb[640*150+489],zc_hosted_fb[0],zc_hosted_fb[640*101+101];
      } else
        "zoneC: GaiaPlotHosted FAIL imports=%d patched=%d entry=%d\n",
          aot_imports,aot_i,aot_main_rip;
    } else
      "zoneC: GaiaPlot export FAIL\n";

    aot=NULL;
    aot_err=0;
    aot_warn=0;
    try {
      aot=CmpBuf("U0 AotLoop(I64 n) {I64 i; i=0; while (i<n) i=i+1;}",NULL,&aot_err,&aot_warn,0);
      aot_bytes=0;
      aot_rip=0;
      if (aot) {
        aot_bytes=aot->aot_U8s;
        aot_rip=aot->rip;
      }
      "zoneC: AOT p2 control aot=%X err=%d warn=%d bytes=%d rip=%d\n",
        aot,aot_err,aot_warn,aot_bytes,aot_rip;
    } catch {
      "zoneC: AOT p2 control CAUGHT %X\n",Fs->except_ch;
      Fs->catch_except=TRUE;
    }
  } else
    "zoneC: LexStmt2Bin failed (%X)\n",jbuf;
  CmpCtrlDel(jcc);
}

"zoneC: JIT hello begins\n";
ZoneCJitSmoke;
"zoneC: JIT hello returned\n";
