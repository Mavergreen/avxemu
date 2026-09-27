#!/bin/sh
# platform: macOS-only -- DYLD_INSERT_LIBRARIES is how avxemu gets into the probe program
#   usage: guard-page.sh GUARD_PAGE_PROGRAM LIBAVXEMU
#          With avxemu loaded, a PROT_NONE guard page must still fault: the SIMD over-read
#          fixup must never mask it (the old build.sh's step 8a, second half).
set -eu
prog=${1:?usage: guard-page.sh GUARD_PAGE_PROGRAM LIBAVXEMU}
lib=${2:?usage: guard-page.sh GUARD_PAGE_PROGRAM LIBAVXEMU}
out=$(mktemp "${TMPDIR:-/tmp}/guard-page.XXXXXX")
trap 'rm -f "$out"' EXIT
rc=0
env AVXEMU_FORCEPATCH=1 DYLD_INSERT_LIBRARIES="$lib" "$prog" >"$out" 2>/dev/null || rc=$?
if [ "$rc" -ne 0 ] && ! grep -q MASKED "$out"; then
  echo "PROT_NONE guard page left intact (exit $rc) -- PASS"
  exit 0
fi
echo "guard page was masked (exit $rc) -- FAIL"
cat "$out"
exit 1
