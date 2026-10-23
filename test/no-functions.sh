#!/bin/sh
# platform: host-agnostic -- Linux; preserve ELF load segments while removing function metadata
set -eu
tool=$1
input=$2
output=$3
d=$(mktemp -d "${TMPDIR:-/tmp}/no-functions.XXXXXX")
trap 'rm -rf "$d"' EXIT
"$tool" --dump-section .eh_frame="$d/frame" "$input" "$d/copy"
size=$(wc -c < "$d/frame")
dd if=/dev/zero of="$d/zeros" bs="$size" count=1 2>/dev/null
"$tool" --strip-all --rename-section .eh_frame_hdr=.unwind_header_unused --update-section .eh_frame="$d/zeros" "$input" "$output"
