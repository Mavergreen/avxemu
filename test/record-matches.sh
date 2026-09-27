#!/bin/sh
# platform: host-agnostic
#   usage: record-matches.sh PROGRAM RECORDED COMMITTED
#          Runs PROGRAM in record mode (a live comparison against this CPU) into RECORDED, then fails
#          unless its non-comment lines equal COMMITTED's. PROGRAM's exit 77 (no silicon) passes through.
set -eu
prog=${1:?usage: record-matches.sh PROGRAM RECORDED COMMITTED}
rec=${2:?usage: record-matches.sh PROGRAM RECORDED COMMITTED}
com=${3:?usage: record-matches.sh PROGRAM RECORDED COMMITTED}
mkdir -p "$(dirname "$rec")"
rm -f "$rec"
rc=0; "$prog" record "$rec" || rc=$?
[ "$rc" -eq 0 ] || exit "$rc"
[ -f "$com" ] || { echo "no committed reference at $com; to adopt this recording: cp '$rec' '$com'"; exit 1; }
a=$(grep -v '^#' "$rec" || true)
b=$(grep -v '^#' "$com" || true)
[ -n "$a" ] || { echo "the recording at $rec is empty"; exit 1; }
if [ "$a" = "$b" ]; then echo "recording matches $com"; exit 0; fi
echo "recording differs from $com:"
diff "$com" "$rec" || true
echo "if intended, re-record deliberately: cp '$rec' '$com', and say why in the commit"
exit 1
