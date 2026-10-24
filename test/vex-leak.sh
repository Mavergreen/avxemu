#!/bin/sh
# platform: host-agnostic -- Darwin (otool) and Linux (objdump or llvm-objdump)
#   usage: vex-leak.sh OBJECT-LIST CONTROL-LIST
#          Fails when any object named in OBJECT-LIST (one path per line) disassembles to a VEX
#          mnemonic. At least one object in CONTROL-LIST must show VEX, or the scan is blind and
#          this fails too.
set -eu
list=${1:?usage: vex-leak.sh OBJECT-LIST CONTROL-LIST}
control=${2:?usage: vex-leak.sh OBJECT-LIST CONTROL-LIST}

case "$(uname -s)" in
  # platform: guarded macOS-only call -- Darwin case of uname
  Darwin) disassemble() { otool -tV "$1"; }
          vex_of() { printf '%s\n' "$1" | awk -F'\t' '$2 ~ /^v/ {print $2}' | sort -u; } ;;
  Linux) tool=${OBJDUMP:-objdump}
         disassemble() { "$tool" -d --no-show-raw-insn "$1"; }
         vex_of() { printf '%s\n' "$1" | awk '$1 ~ /^[0-9a-f]+:$/ && $2 ~ /^v/ {print $2}' | sort -u; } ;;
  *) echo "vex-leak: unsupported platform"; exit 2 ;;
esac
negative=${3:?usage: vex-leak.sh OBJECT-LIST CONTROL-LIST NEGATIVE-LIST}
n=0
while IFS= read -r o; do
  [ -n "$o" ] || continue
  n=$((n + 1))
  dis=$(disassemble "$o") || { echo "vex-leak: disassembly failed on $o"; exit 2; }
  [ -z "$(vex_of "$dis")" ] || { echo "vex-leak: negative control contains VEX"; exit 1; }
done < "$negative"
[ "$n" -gt 0 ] || { echo "vex-leak: empty negative control"; exit 1; }

seen=0
while IFS= read -r o; do
  [ -n "$o" ] || continue
  dis=$(disassemble "$o") || { echo "vex-leak: disassembly failed on $o"; exit 2; }
  if [ -n "$(vex_of "$dis")" ]; then seen=1; fi
done < "$control"
[ "$seen" -eq 1 ] || { echo "vex-leak: no VEX found in any control object -- the scan is blind"; exit 1; }

n=0; bad=0
while IFS= read -r o; do
  [ -n "$o" ] || continue
  n=$((n + 1))
  dis=$(disassemble "$o") || { echo "vex-leak: disassembly failed on $o"; exit 2; }
  leak=$(vex_of "$dis")
  if [ -n "$leak" ]; then echo "VEX LEAK in $o: $leak"; bad=1; fi
done < "$list"
[ "$n" -gt 0 ] || { echo "vex-leak: $list names no objects"; exit 1; }
[ "$bad" -eq 0 ] || exit 1
echo "clean: $n object(s), no VEX"
