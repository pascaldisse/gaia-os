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

#include "Templates.HC"
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
