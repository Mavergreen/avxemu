#!/bin/sh
# platform: macOS-only -- DYLD_INSERT_LIBRARIES loads avxemu into the binary
#   usage: claude-trampoline.sh CPUPROBE LIBAVXEMU WORKDIR
#          Compares a forced-trampolined `$CLAUDE_BIN --help` with the native one. Native needs real
#          AVX2 silicon: exit 77 without CLAUDE_BIN, without AVX2, or under Rosetta. Identical output
#          means something only if a trampoline actually ran: where the RWX pool cannot sit within
#          rel32 of __text (macOS 15 puts it in a far zone) none is placed, and the comparison would
#          be native against native, so that exits 77 too. No placement report at all fails.
set -eu
[ -n "${CLAUDE_BIN:-}" ] || { echo "SKIP: set CLAUDE_BIN to a Claude Code binary"; exit 77; }
probe=${1:?usage}; lib=${2:?usage}; out=${3:?usage}
feats=$("$probe")
case " $feats " in *" translated "*) echo "SKIP: translated, not real silicon"; exit 77 ;; esac
case " $feats " in *" avx2 "*) ;; *) echo "SKIP: this CPU has no AVX2"; exit 77 ;; esac
mkdir -p "$out"
"$CLAUDE_BIN" --help > "$out/nat.txt" 2>/dev/null || true
env AVXEMU_FORCETRAMP=1 AVXEMU_NATIVE_STATS=1 DYLD_INSERT_LIBRARIES="$lib" \
  "$CLAUDE_BIN" --help > "$out/tramp.txt" 2>"$out/tramp.err" || true
[ -s "$out/nat.txt" ] || { echo "native --help printed nothing"; exit 1; }
placed=$(awk -F'\t' '/^placed runs/{print $2}' "$out/tramp.err")
if [ -z "$placed" ]; then
  echo "avxemu never reported placed runs (did not load, or died first)"; tail -5 "$out/tramp.err"; exit 1
fi
if [ "$placed" = 0 ]; then
  built=$(awk -F'\t' '/^(single|multi)-insn runs/{n+=$2} END{print n+0}' "$out/tramp.err")
  echo "SKIP: no trampoline could be placed on this host (runs built: $built; pool beyond rel32 reach)"
  exit 77
fi
if cmp -s "$out/nat.txt" "$out/tramp.txt"; then
  echo "forced-trampolined --help IDENTICAL to native ($(wc -c < "$out/nat.txt") bytes, $placed runs placed)"
else
  echo "forced-trampolined --help DIFFERS from native"; exit 1
fi
