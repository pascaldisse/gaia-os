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

// C4 JIT smoke (diagnostic form): compile one statement through the LOADED
// Shrine front end + BackA64, but do NOT execute the buffer — the exec form
// faulted at frame 0 inside the anonymous JIT buffer (smoke23). Dump the
// emitted machine code as hex words for offline disassembly instead.
"zoneC: JIT hello begins\n";
CCmpCtrl *jcc;
U8 *jbuf;
U32 *jp;
I64 jtype,ji,jn;
jcc=CmpCtrlNew("\"Hello GAIA\\n\";",CCF_DONT_FREE_BUF);
QueIns(jcc,Fs->last_cc);
Lex(jcc);
jbuf=LexStmt2Bin(jcc,&jtype);
if (jbuf && jbuf!=INVALID_PTR) {
  jn=MSize(jbuf);
  "zoneC: JIT buf=%X size=%d type=%d\n",jbuf,jn,jtype;
  if (jn>256) jn=256;
  jp=jbuf;
  for (ji=0;ji<(jn+3)>>2;ji++)
    "zoneC HEX %08X\n",jp[ji];
} else
  "zoneC: LexStmt2Bin failed (%X)\n",jbuf;
QueRem(jcc);
CmpCtrlDel(jcc);
"zoneC: JIT hello returned\n";
