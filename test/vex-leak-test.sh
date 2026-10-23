#!/bin/sh
# platform: host-agnostic -- Darwin and Linux
set -eu
scan=$1
positive=$2
negative=$3
d=$(mktemp -d "${TMPDIR:-/tmp}/vex-test.XXXXXX")
trap 'rm -rf "$d"' EXIT
: > "$d/empty"
sh "$scan" "$negative" "$positive" "$negative"
if sh "$scan" "$positive" "$positive" "$negative" >"$d/out" 2>&1; then exit 1; fi
if sh "$scan" "$negative" "$negative" "$negative" >"$d/out" 2>&1; then exit 1; fi
for list in "$d/empty" "$d/missing"; do
  if sh "$scan" "$list" "$positive" "$negative" >"$d/out" 2>&1; then exit 1; fi
done
if [ "$(uname -s)" = Linux ]; then
  if OBJDUMP=false sh "$scan" "$negative" "$positive" "$negative" >"$d/out" 2>&1; then exit 1; fi
fi
echo 'vex-leak: controls and failures OK'
