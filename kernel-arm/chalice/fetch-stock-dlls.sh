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
# CHALICE C3c-2n: NLS data blobs -- locale.nls (NtInitializeNlsFiles maps
# it, wine/dlls/ntdll/unix/env.c:2243, opens "$dir/nls/locale.nls") +
# sortdefault.nls/l_intl.nls (NtGetNlsSectionPtr maps these on demand,
# env.c:2200 get_nls_file_path -- kernelbase's load_sortdefault_nls calls
# NtGetNlsSectionPtr(NLS_SECTION_SORTKEYS=9,...) during the SAME boot-time
# init_locale chain as locale.nls, dlls/kernelbase/locale.c:494; l_intl.nls
# backs NLS_SECTION_CASEMAP=10, cheap to carry too). None are PE images (no
# MZ/PE Machine field to verify), so handled separately from TARGETS/
# verify_one below. Two sources per file, tried in order: (1) the same
# Hangover deb's data.tar.xz, wherever its NLS payload actually lives (path
# unconfirmed -- Wine installs share/wine/nls/ on the host side, separate
# from the aarch64-windows guest-DLL dir the 4 TARGETS above come from;
# discovered via a tar listing grep, not hardcoded); (2) this repo's
# gitignored wine/ donor tree (wine/nls/<name>, Wine's own generated
# output -- identical format, no network needed). Donor tried FIRST when
# present (fast, no 260MB re-fetch for an already-cloned dev tree); deb
# path stays as the fresh-clone/no-donor fallback.
NLS_TARGETS="locale.nls sortdefault.nls l_intl.nls"

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

# ---- CHALICE C3c-2n: NLS data blobs, sourced separately (not PE images). ----
for NLS_TARGET in $NLS_TARGETS; do
    DONOR_NLS="../../wine/nls/$NLS_TARGET"   # relative to this script's dir (chalice/)
    if [ -s "$OUT_DIR/$NLS_TARGET" ]; then
        echo "fetch-stock-dlls: cached, $OUT_DIR/$NLS_TARGET already present -- skipping"
        continue
    fi
    if [ -s "$DONOR_NLS" ]; then
        cp "$DONOR_NLS" "$OUT_DIR/$NLS_TARGET"
        echo "fetch-stock-dlls: $NLS_TARGET <- donor wine/ tree ($DONOR_NLS), $(wc -c < "$OUT_DIR/$NLS_TARGET") bytes"
        continue
    fi

    echo "fetch-stock-dlls: no donor copy at $DONOR_NLS -- fetching $NLS_TARGET from the Hangover deb ..."
    WORK=$(mktemp -d)
    trap 'rm -rf "$WORK"' EXIT

    echo "fetch-stock-dlls: downloading $ASSET (~260MB) ..."
    curl -fL -o "$WORK/hangover.tar" "$ASSET_URL"

    echo "fetch-stock-dlls: extracting $DEB_MEMBER ..."
    ( cd "$WORK" && tar -xf hangover.tar "$DEB_MEMBER" )

    echo "fetch-stock-dlls: extracting data.tar.xz from the .deb (ar) ..."
    ( cd "$WORK" && ar x "$DEB_MEMBER" data.tar.xz )

    NLS_MEMBER=$( ( cd "$WORK" && tar -tJf data.tar.xz ) | grep -i "/$( echo "$NLS_TARGET" | sed 's/\./\\./' )\$" | head -1 )
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
done

status=0
for NLS_TARGET in $NLS_TARGETS; do
    [ -s "$OUT_DIR/$NLS_TARGET" ] || { echo "fetch-stock-dlls: FAILED -- $OUT_DIR/$NLS_TARGET missing/empty" >&2; status=1; }
done
[ "$status" -eq 0 ] || exit 1
echo "fetch-stock-dlls: OK -- $OUT_DIR/{$( echo $NLS_TARGETS | tr ' ' ',' )}"

# ---- CHALICE C4 leg1 Atom C4-1 -- the 32-bit x86 WOW64 chain. ----
# Recon this session (C4-SPEC.md): the hangover-11.9 release tar bundles
# FOUR .deb payloads, not one -- the main wine deb (already handled above)
# PLUS three small companion debs carrying the CPU-simulation backends
# (Wine ships no wow64cpu.dll/xtajit.dll for x86-on-arm64 at all -- see
# C4-SPEC.md #1 -- Hangover's own replacements come from these debs).
# X86_TARGETS: i386-windows guest tree, Machine 0x14C (verified separately
# below -- verify_one() above hardcodes 0xAA64, wrong check for these).
# WOW64_NATIVE_TARGETS: aarch64-windows native-side dispatcher, Machine
# 0xAA64 (same verify_one() as the existing TARGETS list).
# CPU_BACKEND: exactly ONE of libwow64fex.dll (FEX engine) / wowbox64.dll
# (Box64 engine) is required to mount the chain -- both ship, engine choice
# is Pascal's ruling (HANDOFF NEXT, still open at spec-writing time), so
# this script fetches BOTH (small, ~650-900KB each) and lets the kernel
# side pick at mount time; not a source-of-truth engine decision here.
# NOTE (scar this session): i386-windows and aarch64-windows ship files
# with the SAME BASENAMES (ntdll.dll/kernel32.dll/kernelbase.dll exist in
# BOTH trees, different Machine fields) -- writing them flat into $OUT_DIR
# clobbers the slice-3 ARM64 stock/{ntdll,kernel32,kernelbase}.dll silently
# (no error, just wrong-arch bytes under a right-looking name). Dedicated
# subdirs required: stock/i386-windows/ (32-bit guest tree) and
# stock/wow64-chain/ (native-side WOW64 dispatcher + CPU backends, all
# aarch64-windows-origin but logically separate from the plain-64-bit-Wine
# set slice-3 already owns).
X86_TARGETS="ntdll.dll kernel32.dll kernelbase.dll"
X86_SUBDIR="./usr/lib/wine/i386-windows"
X86_OUT_DIR="$OUT_DIR/i386-windows"
# CHALICE C4-6: wow64win.dll added -- process_init (dlls/wow64/syscall.c
# :1035) loads it right after resolving the CPU backend's BTCpu* exports,
# UNCONDITIONALLY (its own sdwhwin32 SYSTEM_SERVICE_TABLE export feeds
# syscall_tables[1] before BTCpuProcessInit is ever called) -- a missing
# wow64win.dll fails the whole process_init cleanly (STATUS_DLL_NOT_FOUND,
# NtTerminateProcess) before BTCpuProcessInit/BTCpuSimulate ever run,
# confirmed live (pe10 probe, C4-6). Same aarch64-windows source dir as
# wow64.dll itself -- no new fetch mechanism, just one more filename.
WOW64_NATIVE_TARGETS="wow64.dll wow64win.dll"
WOW64_OUT_DIR="$OUT_DIR/wow64-chain"
CPU_BACKEND_DEBS="hangover-libwow64fex_11.9_arm64.deb:libwow64fex.dll hangover-wowbox64_11.9_arm64.deb:wowbox64.dll"

verify_machine() {
    f="$1"; want="$2"
    python3 - "$f" "$want" <<'PYEOF'
import struct, sys
path, want = sys.argv[1], int(sys.argv[2], 16)
with open(path, "rb") as fp:
    d = fp.read(0x200)
if len(d) < 0x40 or d[0:2] != b"MZ":
    sys.stderr.write("fetch-stock-dlls: %s: not an MZ/PE file\n" % path); sys.exit(1)
pe_off = struct.unpack_from("<I", d, 0x3c)[0]
if len(d) < pe_off + 6 or d[pe_off:pe_off+4] != b"PE\x00\x00":
    sys.stderr.write("fetch-stock-dlls: %s: no PE signature\n" % path); sys.exit(1)
machine = struct.unpack_from("<H", d, pe_off + 4)[0]
if machine != want:
    sys.stderr.write("fetch-stock-dlls: %s: Machine=0x%04x, expected 0x%04x\n" % (path, machine, want)); sys.exit(1)
sys.stdout.write("%s: Machine=0x%04x OK, %d bytes\n" % (path, machine, len(open(path,'rb').read())))
PYEOF
}

x86_all_present() {
    for f in $X86_TARGETS; do [ -s "$X86_OUT_DIR/$f" ] || return 1; done
    for f in $WOW64_NATIVE_TARGETS; do [ -s "$WOW64_OUT_DIR/$f" ] || return 1; done
    for pair in $CPU_BACKEND_DEBS; do
        name=${pair#*:}
        [ -s "$WOW64_OUT_DIR/$name" ] || return 1
    done
    return 0
}

if x86_all_present; then
    echo "fetch-stock-dlls: cached, WOW64 x86 chain already present in $X86_OUT_DIR/ + $WOW64_OUT_DIR/ -- skipping"
else
    mkdir -p "$X86_OUT_DIR" "$WOW64_OUT_DIR"
    WORK=$(mktemp -d)
    trap 'rm -rf "$WORK"' EXIT

    echo "fetch-stock-dlls: downloading $ASSET (~260MB, carries all 4 debs) ..."
    curl -fL -o "$WORK/hangover.tar" "$ASSET_URL"

    echo "fetch-stock-dlls: extracting main wine deb for i386-windows + wow64.dll ..."
    ( cd "$WORK" && tar -xf hangover.tar "$DEB_MEMBER" && ar x "$DEB_MEMBER" data.tar.xz )
    for f in $X86_TARGETS; do
        ( cd "$WORK" && tar -xJf data.tar.xz "$X86_SUBDIR/$f" )
        cp "$WORK/$X86_SUBDIR/$f" "$X86_OUT_DIR/$f"
    done
    for f in $WOW64_NATIVE_TARGETS; do
        ( cd "$WORK" && tar -xJf data.tar.xz "$DLL_SUBDIR/$f" )
        cp "$WORK/$DLL_SUBDIR/$f" "$WOW64_OUT_DIR/$f"
    done

    echo "fetch-stock-dlls: extracting CPU-backend companion debs ..."
    for pair in $CPU_BACKEND_DEBS; do
        deb=${pair%:*}; name=${pair#*:}
        ( cd "$WORK" && tar -xf hangover.tar "$deb" && ar p "$deb" data.tar.xz > "${deb}.data.tar.xz" )
        ( cd "$WORK" && tar -xJf "${deb}.data.tar.xz" "$DLL_SUBDIR/$name" )
        cp "$WORK/$DLL_SUBDIR/$name" "$WOW64_OUT_DIR/$name"
    done

    rm -rf "$WORK"
    trap - EXIT
fi

echo "fetch-stock-dlls: verifying WOW64 x86 chain Machine fields ..."
status=0
for f in $X86_TARGETS; do
    verify_machine "$X86_OUT_DIR/$f" 0x14C || status=1
done
for f in $WOW64_NATIVE_TARGETS; do
    verify_machine "$WOW64_OUT_DIR/$f" 0xAA64 || status=1
done
for pair in $CPU_BACKEND_DEBS; do
    name=${pair#*:}
    verify_machine "$WOW64_OUT_DIR/$name" 0xAA64 || status=1
done
[ "$status" -eq 0 ] || { echo "fetch-stock-dlls: FAILED WOW64 chain verification"; exit 1; }
echo "fetch-stock-dlls: OK -- $X86_OUT_DIR/{$( echo $X86_TARGETS | tr ' ' ',' )}, $WOW64_OUT_DIR/{$( echo $WOW64_NATIVE_TARGETS | tr ' ' ',' )}, CPU backends: $( for p in $CPU_BACKEND_DEBS; do echo -n "${p#*:} "; done )"

ls -la "$OUT_DIR" "$X86_OUT_DIR" "$WOW64_OUT_DIR"
