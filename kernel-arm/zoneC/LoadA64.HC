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

U0 ZoneCJitSmoke()
{
  CCmpCtrl *jcc;
  U8 *jbuf,*xbuf,*b;
  U32 *jp;
  CHashFun *jfun;
  I64 jtype,ji,jn,sz;
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
  } else
    "zoneC: LexStmt2Bin failed (%X)\n",jbuf;
  CmpCtrlDel(jcc);
}

"zoneC: JIT hello begins\n";
ZoneCJitSmoke;
"zoneC: JIT hello returned\n";
