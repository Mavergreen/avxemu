#!/bin/sh
# platform: host-agnostic -- Darwin and Linux
#   usage: run-test.sh RUN.SH
#          Pins run.sh's contract: an unmet --needs skips, and a --fails-when mark fails whenever
#          the command's outcome does not match the condition, in either direction; a skip stays one.
set -eu
run=${1:?usage: run-test.sh RUN.SH}
d=$(mktemp -d "${TMPDIR:-/tmp}/run-test.XXXXXX")
trap 'rm -rf "$d"' EXIT
printf '#!/bin/sh\necho "avx avx2 bmi1"\n' > "$d/with-avx2"
printf '#!/bin/sh\necho "avx"\n' > "$d/without-avx2"
printf '#!/bin/sh\necho "avx avx2 bmi1 translated"\n' > "$d/translated"
chmod +x "$d/with-avx2" "$d/without-avx2" "$d/translated"
fails=0
expect() {
  want=$1; shift
  got=0; sh "$run" "$@" >/dev/null 2>&1 || got=$?
  if [ "$got" -ne "$want" ]; then echo "FAIL: want exit $want, got $got: $*"; fails=$((fails + 1)); fi
}
expect 0  --cpuprobe "$d/with-avx2" -- true
expect 1  --cpuprobe "$d/with-avx2" -- false
expect 1  --cpuprobe "$d/without-avx2" --fails-when no-avx2 --because x -- true
expect 0  --cpuprobe "$d/without-avx2" --fails-when no-avx2 --because x -- false
expect 0  --cpuprobe "$d/with-avx2" --fails-when no-avx2 --because x -- true
expect 1  --cpuprobe "$d/with-avx2" --fails-when no-avx2 --because x -- false
expect 77 --cpuprobe "$d/without-avx2" --fails-when no-avx2 --because x -- sh -c 'exit 77'
expect 1  --cpuprobe "$d/translated" --fails-when rosetta --because x -- true
expect 0  --cpuprobe "$d/translated" --fails-when rosetta --because x -- false
expect 0  --cpuprobe "$d/with-avx2" --fails-when rosetta --because x -- true
expect 1  --cpuprobe "$d/with-avx2" --fails-when rosetta --because x -- false
expect 77 --cpuprobe "$d/translated" --fails-when rosetta --because x -- sh -c 'exit 77'
expect 2  --cpuprobe "$d/with-avx2" --fails-when no-avx2 -- true
expect 2  --cpuprobe "$d/with-avx2" --fails-when sideways --because x -- true
version=Linux
# platform: guarded macOS-only call -- this branch requires uname Darwin
if [ "$(uname -s)" = Darwin ]; then version=$(sw_vers -productVersion); fi
case "$version" in
  10.9|10.9.*) expect 0 --cpuprobe "$d/with-avx2" --needs mavericks -- true ;;
  *)           expect 77 --cpuprobe "$d/with-avx2" --needs mavericks -- true ;;
esac
if [ "$(uname -s)" = Linux ]; then
  expect 0 --cpuprobe "$d/with-avx2" --fails-when modern-macos --because x -- true
fi
[ "$fails" -eq 0 ] || exit 1
echo "run.sh: contract holds"
