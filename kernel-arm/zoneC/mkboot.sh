#!/bin/sh
# zoneC C4: stage a flat boot dir for the hosted Shrine+BackA64 load.
# Usage: mkboot.sh [target-dir]   (default /tmp/a64boot)
# Then:  aiwnios -c -t <target-dir> LoadA64.HC
set -e
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
DST="${1:-/tmp/a64boot}"
mkdir -p "$DST"

# Aiwnios runtime image — Load() reads <t_drive>/HCRT2.BIN (c/main.c:1936);
# a bare boot dir crashes the host (SIGABRT) before reading any source.
cp "$REPO/aiwnios/HCRT2.BIN" "$DST/"

# Shrine compiler KEEP set (Compiler.PRJ order minus the C3 cut list)
for f in CompilerA.HH CExts.HC CInit.HC CExcept.HC LexLib.HC \
         Lex.HC CHash.HC PrsLib.HC AsmLib.HC AsmResolve.HC OptLib.HC \
         PrsExp.HC PrsVar.HC CMisc.HC CMain.HC PrsStmt.HC OptPass012.HC \
         OptPass3.HC OptPass4.HC OptPass5.HC OptPass6.HC; do
  cp "$REPO/shrine/Compiler/$f" "$DST/"
done

# hosted-mode patches: strip redundant import decls (host symbols resolve natively)
sed -i '' '/^import /d' "$DST/CExts.HC"
# _extern needs a system sym in Aiwnios; CMP_TEMPLATES* are Templates.HC
# labels (cut with it) and nothing in the KEEP set reads cmp_templates*.
sed -i '' '/^_extern CMP_TEMPLATES/d' "$DST/CExts.HC"

# Lex.HC: the aiwnios host front end MISCOMPILES assignment-expression
# switch heads — smoke28 disasm proved `switch [cc->last_U16=*src++]`
# emits NO byte load and dispatches the jump table on a stale pointer
# temp (wild jump on first LexGetChar call). Only two heads in the KEEP
# set carry assignments (Lex.HC:119 and :450); hoist both at stage time.
# (Plain heads — locals, member loads, array indexes — emit correctly:
# proven by smoke27 SwTest and smoke28's inner doc_e->type_u8 dispatch.)
perl -pi -e 's/^(\s*)switch \[cc->last_U16=\*src\+\+\] \{/$1cc->last_U16=*src++;\n$1switch [cc->last_U16] {/' "$DST/Lex.HC"
perl -pi -e 's/^(\s*)switch \[ch=LexGetChar\(cc\)\] \{/$1ch=LexGetChar(cc);\n$1switch [ch] {/' "$DST/Lex.HC"

# CMisc.HC: the x86 _LAST_FUN asm trampoline (lines 11-48, the ONLY asm
# block in the KEEP set) -> hosted HolyC shim over C-registered CallArgs
# (signature pinned: aiwnios KernelA.HH:4028 I64 CallArgs(U8*,I64,I64*);
# same pattern as aiwnios Src/KUtils.HC:4). Aiwnios CTask HAS last_fun
# (KernelA.HH:3305), so Fs->last_fun compiles against the ALIAS CTask.
awk '
  /^asm \{/ && !done {skip=1}
  skip && /^_extern _LAST_FUN/ {
    skip=0; done=1
    print "// zoneC C4: x86 _LAST_FUN trampoline replaced for hosted mode."
    print "I64 LastFun(I64 argc,I64 *argv)"
    print "{//Execute last fun with args (CallArgs shim)."
    print "  CHashFun *f=Fs->last_fun;"
    print "  if (!f || !f->exe_addr) return 0;"
    print "  return CallArgs(f->exe_addr,argc,argv);"
    print "}"
    next
  }
  !skip {print}
' "$DST/CMisc.HC" > "$DST/CMisc.HC.tmp" && mv "$DST/CMisc.HC.tmp" "$DST/CMisc.HC"

# CMisc.HC: CmdLinePmt is Shrine's interactive shell prompt (RawDr, GetStr,
# DirCur, debug redraw...) — hosted mode never enters LEXF_CMD_LINE (the
# sole consumer, Lex.HC:227, only takes its pointer; we drive CCmpCtrl
# programmatically). Replace the whole function (last in the file) with a
# stub so none of its UI symbols ever need resolving.
sed -i '' '/^U8 \*CmdLinePmt()/,$d' "$DST/CMisc.HC"
cat >> "$DST/CMisc.HC" <<'EOF'
U8 *CmdLinePmt()
{//zoneC C4 hosted stub: no interactive lexer input in hosted mode.
  PrintErr("CmdLinePmt: hosted stub (LEXF_CMD_LINE not supported)\n");
  return StrNew("\n");
}
EOF

# Signature drift: Aiwnios's SysSymImportsResolve(U8*,I64 ld_flags) lost
# TempleOS's ld_flags=0 default (KLoadARM.HC:101). Four KEEP call sites all
# pass only the string -> append explicit ,0 at stage time.
sed -i '' 's/SysSymImportsResolve(\([^),]*\));/SysSymImportsResolve(\1,0);/' \
  "$DST/CMain.HC" "$DST/PrsStmt.HC"

# CMain.HC CInit: AsmHashLoad/UAsmHashLoad load Terry's x86 assembler
# opcode tables — cut with the x86 assembler in C3; our A64Stubs versions
# are deliberate no-ops AND load after CMain. Worse, bare 'AsmHashLoad'
# here binds to the HOST's aarch64 table loader (present in HCRT2.BIN),
# which must not be re-run into the shared hash. Delete both calls.
sed -i '' '/^  AsmHashLoad;$/d; /^  UAsmHashLoad;$/d' "$DST/CMain.HC"

# PrsExp.HC bug#8: variadic call marker. OptPass6 overwrites IC_CALL_START's
# ic_data (fun ptr -> clobber mask, OptPass6.HC:52) and the mid passes scrub
# arg2 of the IC_CALL node itself (proven: disp set at parse time reads 0 at
# pass-7 entry, same node address — smoke76 zc4/zc5), so the marker rides
# the high bits of ic_flags instead: (F+1)<<ICF_A64_VARG_SHIFT (F = fixed
# arg cnt; defines in ShrineHost.HH). Passes only |= / &=~ low ICF bits.
# BackA64Br's IC_CALL reads the marker to build aiwnios's (argc,argv) pair.
python3 - "$DST/PrsExp.HC" <<'PYPRSEXP'
import re,sys
p=sys.argv[1]; s=open(p).read()
pat=re.compile(r'(?m)^([ \t]+)\} else\n([ \t]+)ICAdd\(cc,IC_CALL,tmpf->exe_addr,tmpf->return_class\);$')
m=list(pat.finditer(s)); assert len(m)==1, 'PrsExp IC_CALL anchor drift: %d'%len(m)
g1,g2=m[0].group(1),m[0].group(2)
new=(g1+'} else {\n'
 +g2+'ICAdd(cc,IC_CALL,tmpf->exe_addr,tmpf->return_class);\n'
 +g2+'if (tmpm && tmpm->flags&MLF_DOT_DOT_DOT) //bug#8 variadic marker for BackA64\n'
 +g2+'  cc->coc.coc_head.last->ic_flags|=(tmpf->arg_cnt+1)<<ICF_A64_VARG_SHIFT;\n'
 +g1+'}')
s=s[:m[0].start()]+new+s[m[0].end():]
open(p,'w').write(s)
print('PrsExp bug#8 transform applied')
PYPRSEXP

# zoneC: hosted extract + aarch64 back end + loader
for f in ShrineHost.HH A64Defs.HH Arm64Asm.HC BackA64Lib.HC BackA64.HC \
         BackA64Br.HC BackA64F.HC OptPass789A64.HC A64Stubs.HC LoadA64.HC; do
  cp "$REPO/kernel-arm/zoneC/$f" "$DST/"
done

echo "staged $(ls "$DST" | wc -l | tr -d ' ') files in $DST"
