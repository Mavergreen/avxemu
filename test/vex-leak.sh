#!/bin/sh
# platform: macOS-only -- otool disassembles Mach-O objects
#   usage: vex-leak.sh OBJECT-LIST CONTROL-LIST
#          Fails when any object named in OBJECT-LIST (one path per line) disassembles to a VEX
#          mnemonic. At least one object in CONTROL-LIST must show VEX, or the scan is blind and
#          this fails too.
set -eu
list=${1:?usage: vex-leak.sh OBJECT-LIST CONTROL-LIST}
control=${2:?usage: vex-leak.sh OBJECT-LIST CONTROL-LIST}

vex_of() {
  printf '%s\n' "$1" | awk -F'\t' '$2 ~ /^v/ {print $2}' | sort -u
}

seen=0
while IFS= read -r o; do
  [ -n "$o" ] || continue
  dis=$(otool -tV "$o") || { echo "vex-leak: otool failed on $o"; exit 2; }
  if [ -n "$(vex_of "$dis")" ]; then seen=1; fi
done < "$control"
[ "$seen" -eq 1 ] || { echo "vex-leak: no VEX found in any control object -- the scan is blind"; exit 1; }

n=0; bad=0
while IFS= read -r o; do
  [ -n "$o" ] || continue
  n=$((n + 1))
  dis=$(otool -tV "$o") || { echo "vex-leak: otool failed on $o"; exit 2; }
  leak=$(vex_of "$dis")
  if [ -n "$leak" ]; then echo "VEX LEAK in $o: $leak"; bad=1; fi
done < "$list"
[ "$n" -gt 0 ] || { echo "vex-leak: $list names no objects"; exit 1; }
[ "$bad" -eq 0 ] || exit 1
echo "clean: $n object(s), no VEX"
