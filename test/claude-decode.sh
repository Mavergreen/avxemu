#!/bin/sh
# platform: macOS-only -- otool reads the binary's sections and disassembly
#   usage: claude-decode.sh BINTEST PATCHDIFF ZDECODE WORKDIR
#          Decoder and lzcnt-patch checks over a real AVX2-targeted binary named by $CLAUDE_BIN.
#          Exit 77 when CLAUDE_BIN is unset.
set -eu
[ -n "${CLAUDE_BIN:-}" ] || { echo "SKIP: set CLAUDE_BIN to a Claude Code binary"; exit 77; }
[ -f "$CLAUDE_BIN" ] || { echo "CLAUDE_BIN=$CLAUDE_BIN is not a file"; exit 1; }
bintest=${1:?usage}; patchdiff=${2:?usage}; zdecode=${3:?usage}; out=${4:?usage}
mkdir -p "$out"
otool -l "$CLAUDE_BIN" > "$out/lc.txt"
info=$(awk '/sectname __text/{t=1} t&&/addr /{a=$2} t&&/offset /{print a, $2; t=0}' "$out/lc.txt")
vm=$(echo "$info" | awk '{print $1}'); fo=$(echo "$info" | awk '{print $2}')
[ -n "$vm" ] && [ -n "$fo" ] || { echo "no __text section found in $CLAUDE_BIN"; exit 1; }
otool -tV "$CLAUDE_BIN" > "$out/dis.txt"
awk -F'\t' '
  NR>1 && pc { print prevaddr, prevmnem, $1 }
  { prevaddr=$1; prevmnem=$2; pc=($2 ~ /^(v|mulx|pdep|pext|bzhi|sarx|shlx|shrx|rorx|andn|bextr|blsi|blsmsk|blsr|tzcnt|lzcnt|movbe)/) }
' "$out/dis.txt" > "$out/cand.txt"
[ -s "$out/cand.txt" ] || { echo "no candidate instructions found -- the scan is blind"; exit 1; }
"$bintest" "$CLAUDE_BIN" "$out/cand.txt" "$vm" "$fo" > "$out/bintest.txt"
grep -E "decoded|mismatch" "$out/bintest.txt"
"$patchdiff" "$CLAUDE_BIN" > "$out/patchdiff.txt"; cat "$out/patchdiff.txt"
"$zdecode" "$CLAUDE_BIN" > "$out/zdecode.txt"; cat "$out/zdecode.txt"
