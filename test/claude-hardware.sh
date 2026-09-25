#!/bin/sh
# platform: macOS-only -- otool reads the binary's sections and disassembly
#   usage: claude-hardware.sh CPUPROBE FUZZ WORKDIR
#          Runs every register-only vector instruction in $CLAUDE_BIN natively and emulated. That
#          needs real AVX2 silicon: exit 77 without CLAUDE_BIN, without AVX2, or under Rosetta.
#          claude-trampoline.sh compares a forced-trampolined `--help` with the native one.
set -eu
[ -n "${CLAUDE_BIN:-}" ] || { echo "SKIP: set CLAUDE_BIN to a Claude Code binary"; exit 77; }
probe=${1:?usage}; fuzz=${2:?usage}; out=${3:?usage}
feats=$("$probe")
case " $feats " in *" translated "*) echo "SKIP: translated, not real silicon"; exit 77 ;; esac
case " $feats " in *" avx2 "*) ;; *) echo "SKIP: this CPU has no AVX2"; exit 77 ;; esac
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
"$fuzz" "$CLAUDE_BIN" "$out/cand.txt" "$vm" "$fo" > "$out/fuzz.txt"
grep -E "distinct|runs|mismatch" "$out/fuzz.txt"
distinct=$(awk -F': ' '/^distinct reg-only insns:/{print $2}' "$out/fuzz.txt")
[ -n "$distinct" ] && [ "$distinct" -gt 0 ] 2>/dev/null || { echo "fuzz found zero distinct register-only instructions -- the scan is blind"; exit 1; }
