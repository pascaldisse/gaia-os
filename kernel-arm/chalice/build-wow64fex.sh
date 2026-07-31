#!/usr/bin/env bash
# CHALICE C4-69B — build libwow64fex.dll FROM SOURCE with symbols.
#
# Builds FEX's WOW64 arm64->x86 emulation DLL (the one Hangover loads as
# "libwow64fex.dll") from the vendored FEX source tree, with an
# aarch64-w64-mingw32 cross toolchain, RelWithDebInfo (DWARF-in-DLL debug
# sections, unstripped symbol table) so the black-box blob problem (C4-67:
# FF /2 indirect calls die byte-identical inside the stock Hangover-11.9
# blob, forensics on that symbol-less binary exhausted) can finally be
# instrumented from FEX's own source.
#
# LAWS (non-negotiable, matches CHALICE task law):
#   - the vendored FEX/ tree (VENDORED_FEX_DIR) is READ-ONLY. This script
#     NEVER writes into it. It git-clones (--local) a throwaway working
#     copy out-of-tree and does all submodule/build work there.
#   - never /tmp. All artifacts live under ordered project dirs.
#   - every long-running step is wrapped in `timeout`.
#
# USAGE:
#   ./build-wow64fex.sh [options]
#
# OPTIONS (env vars, all overridable):
#   VENDORED_FEX_DIR   Path to the read-only vendored FEX tree.
#                       default: ~/projects/gaia-os/FEX
#   BUILD_ROOT          Out-of-tree working dir (source clone + build dir
#                        both live here). default: ~/projects/fex-wow64-build
#   TOOLCHAIN_DIR        llvm-mingw install root (expects bin/<triple>-clang
#                        et al inside). default: ~/projects/toolchains/llvm-mingw
#   TOOLCHAIN_URL         Official llvm-mingw release tarball to fetch if
#                        TOOLCHAIN_DIR is missing and no aarch64-w64-mingw32-clang
#                        is already on PATH. default: llvm-mingw 20260616
#                        ucrt-macos-universal release.
#   BUILD_TYPE           CMake build type. default: RelWithDebInfo (keeps
#                        debug symbols -- this is the whole point of the atom).
#   JOBS                 Parallel build jobs. default: nproc/sysctl hw.ncpu.
#   CONFIGURE_TIMEOUT     Seconds. default 300.
#   BUILD_TIMEOUT         Seconds. default 1800.
#
# Pascal's law: missing native build -> clone+build. This script clone+builds
# llvm-mingw's PREBUILT OFFICIAL RELEASE rather than building LLVM from
# source (LLVM-from-source is a multi-hour, many-GB undertaking completely
# out of proportion to this atom; the official llvm-mingw project ships
# reproducible, source-attributable prebuilt cross toolchains precisely for
# this use case, and FEX's own CI (.github/workflows/mingw_build.yml) uses
# the exact same $HOME/llvm-mingw/build/bin layout this script produces).
#
# FRICTION LOG (each fix discovered empirically while building this atom):
#   1. FEX's CMakeLists.txt TUNE_CPU defaults to "native", which on aarch64
#      hosts shells out to Scripts/aarch64_fit_native.py "/proc/cpuinfo" --
#      that path only exists on Linux. On a macOS cross-compile host this
#      throws (FileNotFoundError) and produces an EMPTY string, which then
#      breaks `string(STRIP ${AARCH64_CPU} AARCH64_CPU)` with "STRIP requires
#      two arguments" and aborts configure. Fix: -DTUNE_CPU=none (host CPU
#      tuning is meaningless for a cross build of a Windows target anyway).
#   2. Homebrew installs a native macOS `fmt` (and `xxhash`) package with its
#      own CMake config files. CMake's find_package(fmt/xxHash QUIET) picks
#      those up even while cross-compiling (they're on the default
#      CMAKE_PREFIX_PATH via /opt/homebrew), producing a host-arch IMPORTED
#      target with no Windows import library -- "IMPORTED_IMPLIB not set for
#      imported target fmt::fmt configuration RELWITHDEBINFO" at generate
#      time. Fix: -DCMAKE_DISABLE_FIND_PACKAGE_fmt=ON and same for
#      range-v3/unordered_dense (belt-and-suspenders; only fmt actually
#      triggered the error in practice, but all four bundled External/
#      submodules are what FEX's toolchain_mingw.cmake configuration
#      expects to use anyway).
#   3. The vendored FEX tree ships with ALL git submodules uninitialized.
#      Only a subset is needed for MINGW+wow64fex builds (jemalloc_glibc,
#      vixl, tracy, zydis, Catch2 are all conditionally skipped by
#      CMakeLists.txt for this exact configuration -- see CMakeLists.txt
#      around ENABLE_JEMALLOC_GLIBC_ALLOC/BUILD_TESTING/ENABLE_ZYDIS/
#      ENABLE_FEXCORE_PROFILER). Needed: External/{fmt,range-v3,rpmalloc,
#      unordered_dense,xxhash}, Source/Common/cpp-optparse. This script
#      inits exactly that subset, --depth 1, in the OUT-OF-TREE clone only.
#
set -euo pipefail

VENDORED_FEX_DIR="${VENDORED_FEX_DIR:-$HOME/projects/gaia-os/FEX}"
BUILD_ROOT="${BUILD_ROOT:-$HOME/projects/fex-wow64-build}"
TOOLCHAIN_DIR="${TOOLCHAIN_DIR:-$HOME/projects/toolchains/llvm-mingw}"
TOOLCHAIN_URL="${TOOLCHAIN_URL:-https://github.com/mstorsjo/llvm-mingw/releases/download/20260616/llvm-mingw-20260616-ucrt-macos-universal.tar.xz}"
BUILD_TYPE="${BUILD_TYPE:-RelWithDebInfo}"
JOBS="${JOBS:-$(command -v nproc >/dev/null 2>&1 && nproc || sysctl -n hw.ncpu)}"
CONFIGURE_TIMEOUT="${CONFIGURE_TIMEOUT:-300}"
BUILD_TIMEOUT="${BUILD_TIMEOUT:-1800}"
MINGW_TRIPLE="${MINGW_TRIPLE:-aarch64-w64-mingw32}"

SRC_DIR="$BUILD_ROOT/src"
BIN_DIR="$BUILD_ROOT/build"

echo "== CHALICE C4-69B build-wow64fex.sh =="
echo "VENDORED_FEX_DIR=$VENDORED_FEX_DIR (read-only, never written)"
echo "BUILD_ROOT=$BUILD_ROOT"
echo "TOOLCHAIN_DIR=$TOOLCHAIN_DIR"
echo "BUILD_TYPE=$BUILD_TYPE  JOBS=$JOBS  MINGW_TRIPLE=$MINGW_TRIPLE"

if [ ! -d "$VENDORED_FEX_DIR" ]; then
  echo "FATAL: vendored FEX tree not found at $VENDORED_FEX_DIR" >&2
  exit 1
fi

# --- 1. Toolchain: llvm-mingw aarch64 cross ---------------------------------
if command -v "${MINGW_TRIPLE}-clang" >/dev/null 2>&1; then
  echo "Found ${MINGW_TRIPLE}-clang already on PATH: $(command -v ${MINGW_TRIPLE}-clang)"
elif [ -x "$TOOLCHAIN_DIR/bin/${MINGW_TRIPLE}-clang" ]; then
  echo "Using existing toolchain at $TOOLCHAIN_DIR"
  export PATH="$TOOLCHAIN_DIR/bin:$PATH"
else
  echo "No ${MINGW_TRIPLE}-clang found -- fetching official llvm-mingw release into $TOOLCHAIN_DIR"
  mkdir -p "$(dirname "$TOOLCHAIN_DIR")"
  TARBALL="$(dirname "$TOOLCHAIN_DIR")/$(basename "$TOOLCHAIN_URL")"
  timeout 600 curl -sL --fail -o "$TARBALL" "$TOOLCHAIN_URL"
  EXTRACT_DIR="$(dirname "$TOOLCHAIN_DIR")/$(basename "$TARBALL" .tar.xz)"
  timeout 300 tar xJf "$TARBALL" -C "$(dirname "$TOOLCHAIN_DIR")"
  rm -f "$TARBALL"
  ln -sfn "$EXTRACT_DIR" "$TOOLCHAIN_DIR"
  export PATH="$TOOLCHAIN_DIR/bin:$PATH"
fi
"${MINGW_TRIPLE}-clang" --version

# --- 2. Out-of-tree source clone (never touches VENDORED_FEX_DIR) ----------
mkdir -p "$BUILD_ROOT"
if [ ! -d "$SRC_DIR/.git" ]; then
  echo "Cloning vendored FEX tree out-of-tree to $SRC_DIR (git clone --local, read-only source untouched)"
  git clone --local --no-hardlinks "$VENDORED_FEX_DIR" "$SRC_DIR"
else
  echo "Reusing existing out-of-tree clone at $SRC_DIR"
fi
SRC_REV="$(git -C "$SRC_DIR" rev-parse HEAD)"
echo "Source revision: $SRC_REV"

# --- 3. Submodules needed for MINGW + wow64fex (subset only, see friction #3) --
echo "Initializing required submodules (depth 1) in the out-of-tree clone..."
timeout 300 git -C "$SRC_DIR" submodule update --init --depth 1 -- \
  External/rpmalloc \
  External/unordered_dense \
  External/xxhash \
  External/fmt \
  External/range-v3 \
  Source/Common/cpp-optparse

# --- 4. Configure (out-of-tree build dir) ----------------------------------
rm -rf "$BIN_DIR"
mkdir -p "$BIN_DIR"
timeout "$CONFIGURE_TIMEOUT" cmake -S "$SRC_DIR" -B "$BIN_DIR" \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DCMAKE_TOOLCHAIN_FILE="$SRC_DIR/Data/CMake/toolchain_mingw.cmake" \
  -DMINGW_TRIPLE="$MINGW_TRIPLE" \
  -G Ninja \
  -DENABLE_LTO=False \
  -DENABLE_ASSERTIONS=True \
  -DBUILD_TESTING=False \
  -DTUNE_CPU=none \
  -DCMAKE_DISABLE_FIND_PACKAGE_fmt=ON \
  -DCMAKE_DISABLE_FIND_PACKAGE_xxHash=ON \
  -DCMAKE_DISABLE_FIND_PACKAGE_range-v3=ON \
  -DCMAKE_DISABLE_FIND_PACKAGE_unordered_dense=ON \
  -DCMAKE_INSTALL_PREFIX="$BIN_DIR/install"

# --- 5. Build the wow64fex target only --------------------------------------
timeout "$BUILD_TIMEOUT" ninja -C "$BIN_DIR" wow64fex -j"$JOBS"

DLL="$BIN_DIR/Bin/libwow64fex.dll"
if [ ! -f "$DLL" ]; then
  echo "FATAL: build finished but $DLL not found" >&2
  exit 1
fi

echo "== BUILD OK =="
echo "DLL: $DLL"
ls -la "$DLL"
file "$DLL"
"${MINGW_TRIPLE}-objdump" -h "$DLL" | grep -i debug || true
echo "sha256: $(shasum -a 256 "$DLL" | awk '{print $1}')"
echo "source revision: $SRC_REV"
