#!/usr/bin/env bash
# CHALICE FEX offset map — static PE/symbol/source correlation; never runs guest code.
set -euo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(cd "$script_dir/../.." && pwd)

# Defaults — override every environment-selected input; positional args override DLL/offsets.
default_dll=${FEX_DLL:-"$script_dir/stock/wow64-chain/libwow64fex.dll"}
default_offsets=${FEX_OFFSETS:-"0x1545DC 0x165D14"}
llvm_bin=${LLVM_BIN:-/opt/homebrew/opt/llvm@20/bin}
fex_source=${FEX_SOURCE:-"$repo_root/FEX"}
disasm_before=${FEX_DISASM_BEFORE:-0x40}
disasm_after=${FEX_DISASM_AFTER:-0x80}

usage() {
  cat <<EOF
usage: $(basename "$0") [DLL [OFFSET ...]]

No arguments: DLL=$default_dll; offsets=$default_offsets
Inputs: FEX_DLL FEX_OFFSETS LLVM_BIN FEX_SOURCE FEX_DISASM_BEFORE FEX_DISASM_AFTER
OFFSET is a PE RVA, e.g. 0x1545DC. Static analysis only.
EOF
}

case ${1:-} in
  -h|--help) usage; exit 0 ;;
esac

if (($#)); then
  dll=$1
  shift
else
  dll=$default_dll
fi
if (($#)); then
  offsets=("$@")
else
  # Intentional word split: FEX_OFFSETS is the documented space-separated list.
  read -r -a offsets <<<"$default_offsets"
fi

for tool in llvm-nm llvm-readobj llvm-objdump; do
  [[ -x "$llvm_bin/$tool" ]] || { echo "missing LLVM tool: $llvm_bin/$tool" >&2; exit 2; }
done
[[ -s "$dll" ]] || { echo "DLL missing or empty: $dll" >&2; exit 2; }
[[ -d "$fex_source" ]] || { echo "FEX source missing: $fex_source" >&2; exit 2; }

work=$(mktemp -d "${TMPDIR:-/tmp}/map-fex-offsets.XXXXXX")
trap 'rm -rf "$work"' EXIT

"$llvm_bin/llvm-nm" --numeric-sort --demangle --defined-only "$dll" >"$work/nm.txt"
# Deliberately run both metadata rungs: embedded exports/debug and ARM64 .pdata unwind starts.
"$llvm_bin/llvm-readobj" --file-headers --coff-exports --coff-debug-directory "$dll" >"$work/meta.txt"
"$llvm_bin/llvm-readobj" --unwind "$dll" >"$work/unwind.txt"
image_base=$(awk '/^[[:space:]]*ImageBase:/{print $2; exit}' "$work/meta.txt")
[[ -n "$image_base" ]] || { echo "could not read PE ImageBase from $dll" >&2; exit 2; }

printf 'DLL: %s\nPE ImageBase: %s\nFEX source: %s\n' "$dll" "$image_base" "$fex_source"
printf 'metadata: symbols=%s exports=%s debug=%s\n\n' \
  "$(grep -cE '^[[:xdigit:]]+[[:space:]]+[A-Za-z][[:space:]]' "$work/nm.txt" || true)" \
  "$(grep -c '^Export {' "$work/meta.txt" || true)" \
  "$(grep -c '^DebugDirectory' "$work/meta.txt" || true)"

for offset in "${offsets[@]}"; do
  target=$(python3 - "$image_base" "$offset" <<'PY'
import sys
base, rva = (int(value, 0) for value in sys.argv[1:])
print(f"0x{base + rva:x}")
PY
)

  # Rung 1: exact / nearest embedded COFF symbol. Rung 2: .pdata function start.
  symbol=$(python3 - "$work/nm.txt" "$target" <<'PY'
import re, sys
path, target = sys.argv[1], int(sys.argv[2], 0)
prior = None
for line in open(path, encoding='utf-8', errors='replace'):
    m = re.match(r'^([0-9A-Fa-f]+)\s+([A-Za-z])\s+(.*)$', line.rstrip())
    if not m or m.group(2).lower() not in ('t', 'w'):
        continue
    address = int(m.group(1), 16)
    if address > target:
        break
    prior = (address, m.group(3))
if prior:
    print(f"0x{prior[0]:x}\t{prior[1]}")
PY
)
  pdata=$(python3 - "$work/unwind.txt" "$target" <<'PY'
import re, sys
path, target = sys.argv[1], int(sys.argv[2], 0)
prior = None
for line in open(path, encoding='utf-8', errors='replace'):
    m = re.search(r'^\s*Function:\s*(.*?)\s*\(0x([0-9A-Fa-f]+)\)', line)
    if not m:
        continue
    address = int(m.group(2), 16)
    if address > target:
        continue
    prior = (address, m.group(1) or '<unnamed .pdata function>')
if prior:
    print(f"0x{prior[0]:x}\t{prior[1]}")
PY
)

  source_name=${symbol#*$'\t'}
  source_start=${symbol%%$'\t'*}
  resolution=embedded-symbol
  confidence=high
  if [[ -z "$symbol" ]]; then
    source_name=${pdata#*$'\t'}
    source_start=${pdata%%$'\t'*}
    resolution=.pdata
    confidence=medium
  fi
  if [[ -z "$source_name" || "$source_name" == "$source_start" ]]; then
    source_name='<unresolved>'
    source_start='<none>'
    resolution=none
    confidence=low
  fi

  # Rung 3: source identifier/xref pin. Prefer an out-of-line definition over headers/calls.
  token=$(python3 - "$source_name" <<'PY'
import re, sys
name = sys.argv[1].split('(', 1)[0]
parts = re.findall(r'[A-Za-z_][A-Za-z0-9_]*', name)
print(parts[-1] if parts else '')
PY
)
  source_hit=''
  qualified=$(python3 - "$source_name" <<'PY'
import sys
stem = sys.argv[1].split('(', 1)[0]
parts = stem.split('::')
print('::'.join(parts[-2:]) if len(parts) >= 2 else '')
PY
)
  if [[ -n "$qualified" ]]; then
    source_hit=$(rg -n --glob '*.{c,cc,cpp,cxx}' \
      "${qualified}[[:space:]]*\\(" "$fex_source" 2>/dev/null | tail -n 1 || true)
  fi
  if [[ -z "$source_hit" && -n "$token" && "$token" != unresolved ]]; then
    source_hit=$(rg -n --glob '*.{c,cc,cpp,cxx}' \
      "^[^#]*[[:space:]]${token}[[:space:]]*\\(" "$fex_source" 2>/dev/null | head -n 1 || true)
  fi
  source_note=''
  if [[ -n "$source_hit" ]]; then
    source_path=${source_hit%%:*}
    source_rest=${source_hit#*:}
    source_line=${source_rest%%:*}
    source_note=${source_path#"$fex_source"/}:$source_line
  else
    confidence=$([[ "$confidence" == high ]] && echo medium || echo "$confidence")
  fi

  # Branch/call labels are the binary's immediate xrefs; retain them with donor grep hits.
  window="$work/window-${offset//[^[:alnum:]]/_}.txt"
  start=$(python3 - "$target" "$disasm_before" <<'PY'
import sys
print(hex(max(0, int(sys.argv[1], 0) - int(sys.argv[2], 0))))
PY
)
  stop=$(python3 - "$target" "$disasm_after" <<'PY'
import sys
print(hex(int(sys.argv[1], 0) + int(sys.argv[2], 0)))
PY
)
  "$llvm_bin/llvm-objdump" --disassemble --demangle --start-address="$start" --stop-address="$stop" "$dll" >"$window"
  xref_tokens=$(grep -oE '<[^>]+>' "$window" | tr -d '<>' | \
    python3 -c 'import re,sys; print("\n".join(dict.fromkeys((re.findall(r"[A-Za-z_][A-Za-z0-9_]*", x)[-1] for x in sys.stdin if re.findall(r"[A-Za-z_][A-Za-z0-9_]*", x)))))' 2>/dev/null || true)

  printf '%s => %s' "$offset" "$source_name"
  [[ -n "$source_note" ]] && printf ' (%s)' "$source_note"
  printf ' confidence %s [%s; start=%s]\n' "$confidence" "$resolution" "$source_start"
  if [[ -n "$xref_tokens" ]]; then
    printf 'source-xref tokens:'
    while IFS= read -r xref; do
      [[ -n "$xref" ]] && (rg -l --glob '*.{c,cc,cpp,cxx,h,hpp,hxx,inl}' -m1 "$xref" "$fex_source" 2>/dev/null | head -n1 | sed "s#^$fex_source/##" | sed "s#^# $xref=>#" || true)
    done <<<"$xref_tokens"
    printf '\n'
  fi
  printf '%s\n' '-- disassembly window --'
  cat "$window"
  printf '\n'
done
