#!/bin/sh
# zoneC C4: stage a flat boot dir for the hosted Shrine+BackA64 load.
# Usage: mkboot.sh [target-dir]   (default /tmp/a64boot)
# Then:  aiwnios -c -t <target-dir> LoadA64.HC
set -e
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
DST="${1:-/tmp/a64boot}"
mkdir -p "$DST"

# Shrine compiler KEEP set (Compiler.PRJ order minus the C3 cut list)
for f in CompilerA.HH Templates.HC CExts.HC CInit.HC CExcept.HC LexLib.HC \
         Lex.HC CHash.HC PrsLib.HC AsmLib.HC AsmResolve.HC OptLib.HC \
         PrsExp.HC PrsVar.HC CMisc.HC CMain.HC PrsStmt.HC OptPass012.HC \
         OptPass3.HC OptPass4.HC OptPass5.HC OptPass6.HC; do
  cp "$REPO/shrine/Compiler/$f" "$DST/"
done

# zoneC: hosted extract + aarch64 back end + loader
for f in ShrineHost.HH A64Defs.HH Arm64Asm.HC BackA64Lib.HC BackA64.HC \
         BackA64Br.HC BackA64F.HC OptPass789A64.HC A64Stubs.HC LoadA64.HC; do
  cp "$REPO/kernel-arm/zoneC/$f" "$DST/"
done

echo "staged $(ls "$DST" | wc -l | tr -d ' ') files in $DST"
