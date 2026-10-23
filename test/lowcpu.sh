#!/bin/sh
# platform: host-agnostic -- Linux x86-64 host and full-system Linux guest only
# platform: QEMU 8.2 user-mode lacks process_vm_readv; use full-system TCG.
# platform: Full-system TCG keeps every fork/exec child on the selected CPU.
# platform: QEMU 8.2 ignores invalid LOCK; 11.1.2 swaps LZCNT/BMI1 gating.
# platform: Use test/patch-lowcpu-qemu.sh on verified QEMU 11.1.2 source before building.
# platform: Both instruction faults and independently advertised counts are checked.
# platform: Requires qemu-system-x86_64, static busybox, cpio, gzip, ldd, GNU
# platform: Also requires GNU timeout and a readable Linux x86-64 kernel with
# platform: initramfs, procfs, devtmpfs, ELF and serial support built in.
# platform: Set LOWCPU_KERNEL=/boot/vmlinuz-...; optional QEMU_SYSTEM_X86_64 and
# platform: LOWCPU_BUSYBOX override tools. Enable CTest with AVXEMU_TEST_LOWCPU=ON.
# platform: Enabled tests fail when a dependency or guest check is missing.
set -eu
[ "$#" -eq 1 ] || { echo "usage: $0 BUILD_DIR" >&2; exit 2; }
[ "$(uname -s)" = Linux ] && [ "$(uname -m)" = x86_64 ] || exit 2
build=$(cd "$1" && pwd)
qemu=${QEMU_SYSTEM_X86_64:-qemu-system-x86_64}
busybox=${LOWCPU_BUSYBOX:-busybox}
for tool in "$qemu" "$busybox" cpio gzip ldd timeout awk grep find; do
    command -v "$tool" >/dev/null || { echo "lowcpu: missing tool: $tool" >&2; exit 2; }
done
[ -n "${LOWCPU_KERNEL:-}" ] && [ -r "$LOWCPU_KERNEL" ] || {
    echo "lowcpu: set LOWCPU_KERNEL to a readable x86-64 Linux kernel" >&2; exit 2;
}
busybox=$(command -v "$busybox")
if ldd "$busybox" 2>&1 | grep -q '=>'; then
    echo "lowcpu: LOWCPU_BUSYBOX must be statically linked" >&2; exit 2
fi
work=$(mktemp -d "${TMPDIR:-/tmp}/avxemu-lowcpu.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
root=$work/root
mkdir -p "$root/bin" "$root/lib" "$root/proc" "$root/sys" "$root/dev" "$root/tmp" "$build/lowcpu-logs"
cp "$busybox" "$root/bin/busybox"
ln -s busybox "$root/bin/sh"
copy_elf() {
    src=$1
    dst=$2
    [ -r "$src" ] || { echo "lowcpu: missing build artifact $src" >&2; exit 2; }
    cp "$src" "$root/$dst"
    ldd "$src" > "$work/dependencies"
    if grep -q 'not found' "$work/dependencies"; then cat "$work/dependencies" >&2; exit 2; fi
    awk '{ for (i=1;i<=NF;i++) if ($i ~ /^\//) print $i }' "$work/dependencies" > "$work/paths"
    while IFS= read -r dep; do
        mkdir -p "$root$(dirname "$dep")"
        cp "$dep" "$root$dep"
    done < "$work/paths"
}
copy_elf "$build/libavxemu.so" lib/libavxemu.so
for program in lowcpu lowcpu-stripped context-contract threadtest; do
    copy_elf "$build/test/$program" "$program"
done
cat > "$root/init" <<'GUEST'
#!/bin/sh
export PATH=/bin
/bin/busybox --install -s /bin
mount -t proc proc /proc || poweroff -f
mount -t sysfs sysfs /sys || poweroff -f
mount -t devtmpfs devtmpfs /dev || poweroff -f
export LD_LIBRARY_PATH=/lib
ulimit -c 0
fail() { echo "LOWCPU_GUEST_FAIL: $*"; poweroff -f; exit 1; }
variant=base
for word in $(cat /proc/cmdline); do
    case "$word" in lowcpu=*) variant=${word#lowcpu=} ;; esac
done
uname -a
/lowcpu features "$variant" || fail features
/lowcpu native
rc=$?
[ "$rc" -eq 132 ] || fail "native AVX2 negative control status=$rc, expected SIGILL/132"
echo "negative control: native AVX2 SIGILL (132), no preload"
for op in bsr bsf; do
    /lowcpu "native-lock-$op"
    rc=$?
    [ "$rc" -eq 132 ] || fail "invalid LOCK $op status=$rc; QEMU must deliver SIGILL/132"
    echo "negative control: invalid LOCK $op SIGILL (132), no preload"
done
env AVXEMU_ASSUME_CAPABLE=1 AVXEMU_NOCACHE=1 /context-contract || fail context-contract
for binary in /lowcpu /lowcpu-stripped; do
    for reloc in 0 1; do
        for native in 0 1; do
            for minspill in 0 1; do
                cache=/tmp/cache
                rm -rf "$cache"
                mkdir "$cache"
                for state in cold warm; do
                    echo "matrix: $variant $binary reloc=$reloc native=$native minspill=$minspill cache=$state"
                    env LD_PRELOAD=/lib/libavxemu.so AVXEMU_RELOC="$reloc" \
                        AVXEMU_NATIVE="$native" AVXEMU_MINSPILL="$minspill" \
                        AVXEMU_CACHE_DIR="$cache" AVXEMU_CACHE_STATS=1 \
                        "$binary" emulated > /tmp/result 2>&1 || { cat /tmp/result; fail matrix; }
                    cat /tmp/result
                    if [ "$state" = cold ]; then
                        grep -q 'avxemu cache: miss, written ' /tmp/result || fail 'cache was not written'
                    else
                        grep -q 'avxemu cache: hit ' /tmp/result || fail 'cache did not hit'
                    fi
                done
            done
        done
    done
done
# platform: The linked test forks and execs inside this guest, proving concurrent
# platform: This exercises concurrent faults and relocation of freshly copied code.
env AVXEMU_NOCACHE=1 /threadtest 2 4 || fail threadtest
echo "LOWCPU_GUEST_PASS: $variant"
poweroff -f
GUEST
chmod +x "$root/init"
(cd "$root" && find . -print > "$work/files" && cpio -o -H newc < "$work/files" > "$work/initrd" 2> "$work/cpio.log")
gzip < "$work/initrd" > "$work/initrd.gz"
"$qemu" --version | head -1
for variant in base lzcnt bmi1; do
    case "$variant" in
        base) cpu=SandyBridge,-tsc-deadline ;;
        lzcnt) cpu=SandyBridge,+abm,-tsc-deadline ;;
        bmi1) cpu=SandyBridge,+bmi1,-abm,-tsc-deadline ;;
    esac
    log=$build/lowcpu-logs/$variant.log
    echo "lowcpu: full-system CPU=$cpu kernel=$LOWCPU_KERNEL (log: $log)"
    if ! timeout --kill-after=10 240 "$qemu" -accel tcg -cpu "$cpu" -m 512 -smp 2 \
        -kernel "$LOWCPU_KERNEL" -initrd "$work/initrd.gz" \
        -append "console=ttyS0 rdinit=/init panic=-1 lowcpu=$variant" \
        -display none -serial stdio -monitor none -nic none -no-reboot > "$log" 2>&1; then
        cat "$log"; echo "lowcpu: QEMU failed or timed out" >&2; exit 1
    fi
    if grep -q 'LOWCPU_GUEST_FAIL:' "$log" || ! grep -q "^LOWCPU_GUEST_PASS: $variant" "$log"; then
        cat "$log"; echo "lowcpu: guest did not pass" >&2; exit 1
    fi
    grep -E 'raw features:|negative control:|context-contract:|matrix:|lowcpu: PASS|threadtest:|LOWCPU_GUEST_PASS:' "$log"
done
