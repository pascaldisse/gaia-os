#!/bin/sh
# GAIA-OS skin installer for the aiwnios Temple GUI (whip-16x).
# Stage-time overlay only: aiwnios/ is a gitignored donor tree, NEVER
# committed and NEVER hand-edited outside this script. All tracked skin
# sources live under kernel-arm/gaia-skin/ + kernel-arm/tools/TempleDbg.HC;
# this script copies them in and applies sed/perl source transforms
# (mkboot.sh pattern: kernel-arm/zoneC/mkboot.sh) at install/build time.
#
# Usage: kernel-arm/gaia-skin/install.sh [repo-root]
#   repo-root defaults to this script's own repo (../.. from here).
# Idempotent: safe to re-run; every transform checks before applying.
set -e
SKIN_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO="${1:-$(cd "$SKIN_DIR/../.." && pwd)}"
AIW="$REPO/aiwnios"

if [ ! -d "$AIW" ]; then
  echo "install.sh: no aiwnios/ donor tree at $AIW (clone it first)" >&2
  exit 1
fi

echo "install.sh: staging GAIA-OS skin into $AIW"

# 1) Copy tracked sources into the donor tree (root-mounted ::/ paths).
cp -f "$REPO/kernel-arm/tools/TempleDbg.HC" "$AIW/TempleDbg.HC"
cp -f "$SKIN_DIR/GaiaHome.HC" "$AIW/GaiaHome.HC"

# 2) Window/dialog title: c/*.c literal "AIWNIOS" -> "GAIA-OS" (SDL_CreateWindow
#    at c/windows.c + every SDL_ShowSimpleMessageBox title in c/windows.c and
#    c/main.c). Idempotent: skip if no more "AIWNIOS" literals remain.
if grep -l '"AIWNIOS"' "$AIW"/c/*.c >/dev/null 2>&1; then
  sed -i '' 's/"AIWNIOS"/"GAIA-OS"/g' "$AIW"/c/*.c
  echo "install.sh: title-string transform applied (c/windows.c, c/main.c)"
else
  echo "install.sh: title-string transform already applied"
fi

# 3) Front page: UserStartUp() Type()s the boot doc (Src/FULL_PACKAGE.HC,
#    "U0 UserStartUp()" body). Point it at GaiaHome.HC instead of the donor
#    Doc/Start.DD (left on disk untouched, still reachable via the Help &
#    Index link on the new page). Idempotent via grep guard.
if grep -q 'Type("::/Doc/Start.DD");' "$AIW/Src/FULL_PACKAGE.HC"; then
  sed -i '' 's#Type("::/Doc/Start.DD");#Type("::/GaiaHome.HC");#' \
    "$AIW/Src/FULL_PACKAGE.HC"
  echo "install.sh: front-page transform applied (Src/FULL_PACKAGE.HC)"
else
  echo "install.sh: front-page transform already applied"
fi

# 4) Boot-exec proof hook: same GUI-mode load path as TempleDbg.HC (GUI mode
#    ignores -t boot-file argv; ~/HomeKeyPlugIns.HC is the one path
#    FULL_PACKAGE.HC runs unconditionally in GUI mode). Idempotent via
#    grep guard; appended after the existing TempleDbg hook so both fire.
if ! grep -q 'GaiaHome.HC' "$AIW/HomeKeyPlugIns.HC"; then
  cat >> "$AIW/HomeKeyPlugIns.HC" <<'EOF'

//GaiaHome auto-load (GAIA-OS skin, whip-16x): same GUI-mode load path as
//TempleDbg above (~/HomeKeyPlugIns.HC is ExeFile'd unconditionally at boot
//when !IsCmdLineMode; -t boot-file argv is CLI-mode only). Proves the front
//page banner was ExeFile'd (writes ::/dbg/probe.txt) independent of whether
//UserStartUp's Type() call rendered it.
if(FileFind("::/GaiaHome.HC"))
  #include "::/GaiaHome.HC";
EOF
  echo "install.sh: HomeKeyPlugIns.HC hook appended"
else
  echo "install.sh: HomeKeyPlugIns.HC hook already present"
fi

# 5) Rebuild the C binary (title change only takes effect after relink).
#    RUNTIME_OUTPUT_DIRECTORY is aiwnios/ itself and MACOSX_BUNDLE TRUE
#    (misc/CMakeLists.txt) makes CMake link straight into
#    aiwnios.app/Contents/MacOS/aiwnios - no manual bundle copy needed
#    (proven 2026-07-16: build output mtime moves under the .app path).
if [ -d "$AIW/build" ]; then
  echo "install.sh: rebuilding aiwnios binary"
  cmake --build "$AIW/build" --target aiwnios -j4
  echo "install.sh: binary rebuilt (linked into aiwnios.app/Contents/MacOS)"

  # 6) Bootstrap: HCRT2.BIN is a PREcompiled runtime image (c/main.c:1936-1939
  #    loads <t_drive>/HCRT2.BIN at boot, it does not recompile Src/ live) -
  #    the Type()/HomeKeyPlugIns transforms above only take effect once this
  #    image is regenerated from Src/ (proven 2026-07-16: skipping this step
  #    left the old Start.DD front page on screen after a full relaunch).
  #    Same command as the CMakeLists.txt HCRT2.BIN custom target (aiwnios -b),
  #    run through cmake so target-name substitution / deps stay authoritative.
  echo "install.sh: bootstrapping HCRT2.BIN from Src/"
  cmake --build "$AIW/build" --target HCRT2.BIN -j4
  echo "install.sh: HCRT2.BIN rebuilt"
else
  echo "install.sh: WARNING no $AIW/build dir - run cmake configure first" >&2
fi

echo "install.sh: done"
