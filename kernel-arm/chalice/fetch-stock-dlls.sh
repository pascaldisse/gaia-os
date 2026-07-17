#!/bin/sh
# CHALICE SLICE3 Atom 3C-1 — stock ntdll/kernel32/kernelbase artifacts.
# Source DECIDED (SLICE3-SPEC Atom 3C, recon C): Hangover release
# hangover-11.9, debian13(trixie) arm64 distro tar -> hangover-wine deb ->
# data.tar.xz:./usr/lib/wine/aarch64-windows/{ntdll,kernel32,kernelbase}.dll
# -- genuine Machine=0xAA64 (NOT ARM64EC, which reports 0xA641). License
# LGPL 2.1 (Wine, vendored via Hangover's build). WineHQ/distros ship NO
# arm64 PE builds (verified 404 at spec-writing time) -- this is the only
# known source of real ARM64 PE ntdll-class binaries.
#
# Cached: if all 3 targets already exist in chalice/stock/, skip the whole
# fetch (idempotent re-run, gate-safe -- no network dependency once fetched
# once).
set -e
cd "$(dirname "$0")"

RELEASE_TAG="hangover-11.9"
ASSET="hangover_11.9_debian13_trixie_arm64.tar"
ASSET_URL="https://github.com/AndreRH/hangover/releases/download/${RELEASE_TAG}/${ASSET}"
DEB_MEMBER='hangover-wine_11.9~trixie_arm64.deb'
DLL_SUBDIR="./usr/lib/wine/aarch64-windows"
OUT_DIR="stock"
# CHALICE C3c-2h: apisetschema.dll added -- same aarch64-windows dir, holds
# the .apiset section PEB->ApiSetMap points at (import-name resolution for
# api-ms-win-*/api-set imports, wine/dlls/ntdll/unix/loader.c load_apiset_dll).
TARGETS="ntdll.dll kernel32.dll kernelbase.dll apisetschema.dll"
# CHALICE C3c-2n: locale.nls -- the combined locale/casing/codepage NLS data
# blob NtInitializeNlsFiles maps (wine/dlls/ntdll/unix/env.c:2243, opens
# "$dir/nls/locale.nls"). NOT a PE image (no MZ/PE Machine field to verify),
# so it's handled separately from TARGETS/verify_one below. Two sources,
# tried in order: (1) the same Hangover deb's data.tar.xz, wherever its NLS
# payload actually lives (path unconfirmed -- Wine installs share/wine/nls/
# on the host side, separate from the aarch64-windows guest-DLL dir the 4
# TARGETS above come from; discovered via a tar listing grep, not hardcoded);
# (2) this repo's gitignored wine/ donor tree (wine/nls/locale.nls,
# Wine's own generated output -- identical format, no network needed).
# Donor tried FIRST when present (fast, no 260MB re-fetch for an already-
# cloned dev tree); deb path stays as the fresh-clone/no-donor fallback.
NLS_TARGET="locale.nls"
DONOR_NLS="../../wine/nls/locale.nls"   # relative to this script's dir (chalice/)

all_present() {
    for f in $TARGETS; do
        [ -s "$OUT_DIR/$f" ] || return 1
    done
    return 0
}

if all_present; then
    echo "fetch-stock-dlls: cached, all 3 present in $OUT_DIR/ -- skipping fetch"
else
    mkdir -p "$OUT_DIR"
    WORK=$(mktemp -d)
    trap 'rm -rf "$WORK"' EXIT

    echo "fetch-stock-dlls: downloading $ASSET (~260MB) ..."
    curl -fL -o "$WORK/hangover.tar" "$ASSET_URL"

    echo "fetch-stock-dlls: extracting $DEB_MEMBER ..."
    ( cd "$WORK" && tar -xf hangover.tar "$DEB_MEMBER" )

    echo "fetch-stock-dlls: extracting data.tar.xz from the .deb (ar) ..."
    ( cd "$WORK" && ar x "$DEB_MEMBER" data.tar.xz )

    echo "fetch-stock-dlls: extracting the 3 DLLs from data.tar.xz ..."
    for f in $TARGETS; do
        ( cd "$WORK" && tar -xJf data.tar.xz "$DLL_SUBDIR/$f" )
    done

    for f in $TARGETS; do
        cp "$WORK/$DLL_SUBDIR/$f" "$OUT_DIR/$f"
    done
    rm -rf "$WORK"
    trap - EXIT
fi

# ---- verify: PE Machine field == 0xAA64 (IMAGE_FILE_MACHINE_ARM64), reject
# ARM64EC (0xA641) or anything else with a clear error. e_lfanew @ 0x3c
# (u32 LE) -> PE sig+FileHeader; Machine is the first u16 of FileHeader
# (PE\0\0 sig is 4 bytes, so Machine @ e_lfanew+4). ----
verify_one() {
    f="$1"
    python3 - "$f" <<'PYEOF'
import struct, sys
path = sys.argv[1]
with open(path, "rb") as fp:
    d = fp.read(0x200)
if len(d) < 0x40 or d[0:2] != b"MZ":
    sys.stderr.write("fetch-stock-dlls: %s: not an MZ/PE file\n" % path)
    sys.exit(1)
pe_off = struct.unpack_from("<I", d, 0x3c)[0]
if len(d) < pe_off + 6 or d[pe_off:pe_off+4] != b"PE\x00\x00":
    sys.stderr.write("fetch-stock-dlls: %s: no PE signature at 0x%x\n" % (path, pe_off))
    sys.exit(1)
machine = struct.unpack_from("<H", d, pe_off + 4)[0]
if machine != 0xAA64:
    sys.stderr.write(
        "fetch-stock-dlls: %s: Machine=0x%04x, expected 0xAA64 (ARM64) -- "
        "REJECTED (0xA641 == ARM64EC, not the genuine-ARM64 artifact this "
        "atom requires)\n" % (path, machine))
    sys.exit(1)
sys.stdout.write("%s: Machine=0x%04x OK, %d bytes\n" % (path, machine, len(open(path,'rb').read())))
PYEOF
}

echo "fetch-stock-dlls: verifying PE Machine field (0xAA64 required) ..."
status=0
for f in $TARGETS; do
    verify_one "$OUT_DIR/$f" || status=1
done
[ "$status" -eq 0 ] || { echo "fetch-stock-dlls: FAILED verification"; exit 1; }

echo "fetch-stock-dlls: OK -- $OUT_DIR/{$( echo $TARGETS | tr ' ' ',' )}"

# ---- CHALICE C3c-2n: locale.nls, sourced separately (not a PE image). ----
if [ -s "$OUT_DIR/$NLS_TARGET" ]; then
    echo "fetch-stock-dlls: cached, $OUT_DIR/$NLS_TARGET already present -- skipping"
elif [ -s "$DONOR_NLS" ]; then
    cp "$DONOR_NLS" "$OUT_DIR/$NLS_TARGET"
    echo "fetch-stock-dlls: $NLS_TARGET <- donor wine/ tree ($DONOR_NLS), $(wc -c < "$OUT_DIR/$NLS_TARGET") bytes"
else
    echo "fetch-stock-dlls: no donor copy at $DONOR_NLS -- fetching $NLS_TARGET from the Hangover deb ..."
    WORK=$(mktemp -d)
    trap 'rm -rf "$WORK"' EXIT

    echo "fetch-stock-dlls: downloading $ASSET (~260MB) ..."
    curl -fL -o "$WORK/hangover.tar" "$ASSET_URL"

    echo "fetch-stock-dlls: extracting $DEB_MEMBER ..."
    ( cd "$WORK" && tar -xf hangover.tar "$DEB_MEMBER" )

    echo "fetch-stock-dlls: extracting data.tar.xz from the .deb (ar) ..."
    ( cd "$WORK" && ar x "$DEB_MEMBER" data.tar.xz )

    NLS_MEMBER=$( ( cd "$WORK" && tar -tJf data.tar.xz ) | grep -i '/locale\.nls$' | head -1 )
    if [ -z "$NLS_MEMBER" ]; then
        echo "fetch-stock-dlls: FAILED -- $NLS_TARGET not found anywhere in $DEB_MEMBER's data.tar.xz" >&2
        rm -rf "$WORK"
        trap - EXIT
        exit 1
    fi
    echo "fetch-stock-dlls: found $NLS_TARGET at $NLS_MEMBER"
    ( cd "$WORK" && tar -xJf data.tar.xz "$NLS_MEMBER" )
    cp "$WORK/$NLS_MEMBER" "$OUT_DIR/$NLS_TARGET"
    echo "fetch-stock-dlls: $NLS_TARGET <- deb ($NLS_MEMBER), $(wc -c < "$OUT_DIR/$NLS_TARGET") bytes"
    rm -rf "$WORK"
    trap - EXIT
fi

[ -s "$OUT_DIR/$NLS_TARGET" ] || { echo "fetch-stock-dlls: FAILED -- $OUT_DIR/$NLS_TARGET missing/empty" >&2; exit 1; }
echo "fetch-stock-dlls: OK -- $OUT_DIR/$NLS_TARGET"
ls -la "$OUT_DIR"
