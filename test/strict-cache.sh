#!/bin/sh
# platform: host-agnostic -- glibc Linux runtime/cache regression
set -eu
lib=$1
prog=$2
expected=$3
work=$(mktemp -d "${TMPDIR:-/tmp}/avxemu-optional.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
for warmth in cold warm; do
    env -u AVXEMU_NOCACHE AVXEMU_FORCEPATCH=1 AVXEMU_FORCETRAMP=1 AVXEMU_CACHE_STATS=1 AVXEMU_CACHE_DIR="$work/cache" LD_PRELOAD="$lib" "$prog" >"$work/$warmth.out" 2>"$work/$warmth.err"
    cat "$work/$warmth.out" "$work/$warmth.err"
    grep -q "$expected" "$work/$warmth.out"
done
grep -q 'avxemu cache: hit ' "$work/warm.err"
