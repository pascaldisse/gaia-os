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

# zoneC: hosted extract + aarch64 back end + loader
for f in ShrineHost.HH A64Defs.HH Arm64Asm.HC BackA64Lib.HC BackA64.HC \
         BackA64Br.HC BackA64F.HC OptPass789A64.HC A64Stubs.HC LoadA64.HC; do
  cp "$REPO/kernel-arm/zoneC/$f" "$DST/"
done

echo "staged $(ls "$DST" | wc -l | tr -d ' ') files in $DST"
