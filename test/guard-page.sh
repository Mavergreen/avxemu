#!/bin/sh
# platform: host-agnostic -- Darwin and glibc Linux
#   usage: guard-page.sh GUARD_PAGE_PROGRAM LIBAVXEMU
#          With avxemu loaded, a PROT_NONE guard page must still fault: the SIMD over-read
#          fixup must never mask it (the old build.sh's step 8a, second half).
set -eu
prog=${1:?usage: guard-page.sh GUARD_PAGE_PROGRAM LIBAVXEMU}
lib=${2:?usage: guard-page.sh GUARD_PAGE_PROGRAM LIBAVXEMU}
out=$(mktemp "${TMPDIR:-/tmp}/guard-page.XXXXXX")
trap 'rm -f "$out"' EXIT
for mode in scalar emulated; do
rc=0
arg=
[ "$mode" = scalar ] || arg=emulated
env AVXEMU_FORCEPATCH=1 sh "$(dirname "$0")/preload.sh" "$lib" "$prog" $arg >"$out" 2>/dev/null || rc=$?
if { [ "$rc" -eq 139 ] || [ "$rc" -eq 138 ] || [ "$rc" -eq 135 ]; } &&
   grep -q "^avxemu loaded$" "$out" && ! grep -q MASKED "$out"; then
  echo "PROT_NONE guard page left intact (exit $rc) -- PASS"
  continue
fi
echo "guard page was masked (exit $rc) -- FAIL"
cat "$out"
exit 1
done
