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

U0 ZoneCJitSmoke()
{
  CCmpCtrl *jcc;
  U8 *jbuf;
  U32 *jp;
  I64 jtype,ji,jn;
  "zoneC: addrs Dbg=%X Lex=%X CmpCtrlNew=%X LexGetChar=%X LexStmt2Bin=%X bmp=%X\n",
	&Dbg,&Lex,&CmpCtrlNew,&LexGetChar,&LexStmt2Bin,char_bmp_alpha_numeric;
  "zoneC: step 1b SwTest('A')=%d\n",ZoneCSwTest('A');
  // (SwTest(300) removed: out-of-range on a no-bounds switch is expected UB,
  // not evidence — *src++ in LexGetChar is a U8 deref, always 0..255.)
  "zoneC: step 1c SwTest(0)=%d SwTest(10)=%d\n",
	ZoneCSwTest(0),ZoneCSwTest(10);
  jp=&ZoneCSwTest;
  for (ji=0;ji<64;ji++)
    "zoneC SWT %08X\n",jp[ji];
  jp=&LexGetChar;
  for (ji=0;ji<512;ji++)
    "zoneC LGC %08X\n",jp[ji];
  jcc=CmpCtrlNew("ABC",CCF_DONT_FREE_BUF);
  "zoneC: step 2 probe jcc=%X buf=%s\n",jcc,jcc->cur_buf_ptr;
  "zoneC: step 3 LexGetChar=%d\n",LexGetChar(jcc);
  "zoneC: step 4 LexGetChar=%d\n",LexGetChar(jcc);
  CmpCtrlDel(jcc);
  "zoneC: step 5 probe cc deleted\n";
  jcc=CmpCtrlNew("\"Hello GAIA\\n\";",CCF_DONT_FREE_BUF);
  Lex(jcc);
  "zoneC: step 6 Lex ok token=%d\n",jcc->token;
  jbuf=LexStmt2Bin(jcc,&jtype);
  "zoneC: step 7 LexStmt2Bin ok\n";
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
