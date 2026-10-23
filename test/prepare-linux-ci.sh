#!/bin/sh
# platform: host-agnostic -- provisions Ubuntu 22.04/24.04 x86-64 CI hosts only
# spec: DEVELOPING.md "Linux CI dependencies" owns the test-tool versions and scope.
set -eu
[ "$#" -eq 2 ] || { echo "usage: $0 TOOL_DIR ENV_FILE" >&2; exit 2; }
. /etc/os-release
[ "${ID:-}" = ubuntu ] || { echo "Linux CI requires Ubuntu" >&2; exit 2; }
case "$VERSION_ID" in 22.04|24.04) ;; *) echo "unsupported Ubuntu: $VERSION_ID" >&2; exit 2 ;; esac
[ "$(uname -m)" = x86_64 ] || exit 2
tools=$1
mkdir -p "$tools"
tools=$(cd "$tools" && pwd)
env_file=$2
repo=$(cd "$(dirname "$0")/.." && pwd)
apt_install() {
    if [ "$(id -u)" -eq 0 ]; then apt-get "$@"; else sudo apt-get "$@"; fi
}
apt_install update -qq -o APT::Update::Error-Mode=any
apt_install install -y --no-install-recommends build-essential clang binutils \
    curl ca-certificates git ninja-build pkg-config python3 python3-venv python3-pip python3-tomli \
    libglib2.0-dev libpixman-1-dev zlib1g-dev busybox-static cpio gzip patch xz-utils bzip2
fetch() {
    url=$1
    digest=$2
    dest=$3
    if [ ! -f "$dest" ]; then
        curl --fail --location --retry 3 --output "$dest.part" "$url"
        mv "$dest.part" "$dest"
    fi
    printf '%s  %s\n' "$digest" "$dest" | sha256sum --check --status
}
cmake_ok() {
    command -v cmake >/dev/null 2>&1 || return 1
    cmake --version > "$tools/cmake-version"
    python3 - "$tools/cmake-version" <<'PY'
import re, sys
text = open(sys.argv[1]).read()
match = re.match(r"cmake version (\d+)\.(\d+)", text)
sys.exit(0 if match and tuple(map(int, match.groups())) >= (3, 25) else 1)
PY
}
if ! cmake_ok; then
    fetch https://cmake.org/files/v3.28/cmake-3.28.3-linux-x86_64.tar.gz \
        804d231460ab3c8b556a42d2660af4ac7a0e21c98a7f8ee3318a74b4a9a187a6 \
        "$tools/cmake.tar.gz"
    tar --no-same-owner -xzf "$tools/cmake.tar.gz" -C "$tools"
    PATH=$tools/cmake-3.28.3-linux-x86_64/bin:$PATH
    export PATH
fi
cmake_ok || { echo "CMake >=3.25 required" >&2; exit 1; }
fetch https://download.qemu.org/qemu-11.1.2.tar.xz \
    731b5681e4bb18be313231579b8efd0296c5b015fa36dc533874b639ba838016 "$tools/qemu.tar.xz"
tar --no-same-owner -xJf "$tools/qemu.tar.xz" -C "$tools"
sh "$repo/test/patch-lowcpu-qemu.sh" "$tools/qemu-11.1.2"
mkdir -p "$tools/qemu-build"
(
    cd "$tools/qemu-build"
    # spec: DEVELOPING.md "Linux CI dependencies" fixes the test-tool compiler separately.
    CC=gcc CXX=g++ "$tools/qemu-11.1.2/configure" --python=/usr/bin/python3 \
        --target-list=x86_64-softmmu \
        --without-default-features --enable-tcg --enable-system --disable-docs \
        --disable-werror --disable-tools --with-pkgversion=avxemu-count-feature-gates
    ninja -j2 qemu-system-x86_64
)
fetch https://archive.ubuntu.com/ubuntu/pool/main/l/linux-signed-hwe-6.14/linux-image-6.14.0-37-generic_6.14.0-37.37~24.04.1_amd64.deb \
    9c17bb3f50741bf8f21b0f8a2afd088f5d0ae5b0d1cc00b7a866a63c8492d29a "$tools/kernel.deb"
dpkg-deb --extract "$tools/kernel.deb" "$tools/kernel"
qemu=$tools/qemu-build/qemu-system-x86_64
kernel=$tools/kernel/boot/vmlinuz-6.14.0-37-generic
[ -x "$qemu" ] && [ -r "$kernel" ]
for tool in gcc clang as ld objdump strip ctest busybox cpio gzip ldd timeout; do
    command -v "$tool"
done
cmake --version
gcc --version
clang --version
ld --version
"$qemu" --version
{
    printf 'CMAKE_BIN=%s\n' "$(dirname "$(command -v cmake)")"
    printf 'QEMU_SYSTEM_X86_64=%s\n' "$qemu"
    printf 'LOWCPU_KERNEL=%s\n' "$kernel"
    printf 'LOWCPU_BUSYBOX=%s\n' "$(command -v busybox)"
} >> "$env_file"
