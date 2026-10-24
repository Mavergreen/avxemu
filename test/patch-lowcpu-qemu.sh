#!/bin/sh
# platform: host-agnostic -- Linux test-tool preparation; never changes AVXEmu
# platform: QEMU 11.1.2's decoder swaps TZCNT/BMI1 and LZCNT/ABM gates.
# platform: Apply only to the exact official source, then build x86_64-softmmu.
# platform: Official qemu-11.1.2.tar.xz SHA256:
# platform: 731b5681e4bb18be313231579b8efd0296c5b015fa36dc533874b639ba838016
# platform: Raw count-semantics probes in lowcpu.sh independently verify the tool.
set -eu
[ "$#" -eq 1 ] || { echo "usage: $0 QEMU_11_1_2_SOURCE_DIR" >&2; exit 2; }
source_dir=$(cd "$1" && pwd)
file=$source_dir/target/i386/tcg/decode-new.c.inc
[ "$(cat "$source_dir/VERSION")" = 11.1.2 ] || {
    echo "lowcpu: expected QEMU 11.1.2 source" >&2; exit 1;
}
original=d22dea324bf55a8b037ad47200100540fb00db49830b449d487cddaedf47b800
patched=3118219fe4cd32d81184ab795cdd29671530e442f7e186eb6059e02f3896e3f7
actual=$(sha256sum "$file" | awk '{print $1}')
[ "$actual" = "$original" ] || {
    echo "lowcpu: unexpected QEMU decoder source (refusing to patch)" >&2; exit 1;
}
tmp=$(mktemp "${TMPDIR:-/tmp}/qemu-count-features.XXXXXX")
trap 'rm -f "$tmp"' EXIT HUP INT TERM
patch --batch --fuzz=0 --output "$tmp" "$file" < "$(dirname "$0")/qemu-count-features.patch"
[ "$(sha256sum "$tmp" | awk '{print $1}')" = "$patched" ] || {
    echo "lowcpu: unexpected patched QEMU decoder" >&2; exit 1;
}
cp "$tmp" "$file"
echo "lowcpu: QEMU 11.1.2 count-feature gates corrected; rebuild before use"
