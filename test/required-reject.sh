#!/bin/sh
# platform: host-agnostic -- glibc Linux
set -eu
lib=$1
prog=$2
out=$(mktemp "${TMPDIR:-/tmp}/required-reject.XXXXXX")
trap 'rm -f "$out"' EXIT
rc=0
env AVXEMU_FORCEPATCH=1 sh "$(dirname "$0")/preload.sh" "$lib" "$prog" >"$out" 2>&1 || rc=$?
[ "$rc" -eq 127 ] && grep -q 'unsupported executable: cannot safely discover required LZCNT/TZCNT' "$out"
