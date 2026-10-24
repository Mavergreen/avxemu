#!/bin/sh
# platform: macOS-only -- installs this run's pkg and runs its cross-built tests on 10.9
# usage: accept-mavericks.sh BUNDLE-DIR PKG VERSION
set -eu
bundle=$(cd "${1:?bundle directory}" && pwd)
pkg=${2:?package path}
version=${3:?package version}
case "$(sw_vers -productVersion)" in 10.9.*) ;; *) echo 'requires Mavericks'; exit 1 ;; esac
IFS= read -r source < "$bundle/source-path"
IFS= read -r build < "$bundle/build-path"
IFS= read -r TMPDIR < "$bundle/tmp-path"
export TMPDIR
# platform: the disposable guest restores this workflow's own build at its original paths;
#           generated CTest files, reference operands and dylib rpaths remain valid.
sudo tar -xzf "$bundle/root.tar.gz" -C /
sudo chown -R "$(id -u):$(id -g)" "$source" "$build"
sudo chown "$(id -u):$(id -g)" "$TMPDIR"
sudo mkdir -p /usr/local/mavergreen/bin
sudo ln -sf ../shipyard/bin/shipyard-ctest /usr/local/mavergreen/bin/shipyard-ctest
PATH=/usr/local/mavergreen/bin:$PATH
SHIPYARD_SCRIPTS=/usr/local/mavergreen/shipyard/share/cmake/MavericksShipyard/scripts
export PATH SHIPYARD_SCRIPTS
sudo installer -pkg "$pkg" -target /
installed=/usr/local/mavergreen/avxemu
[ "$(/usr/libexec/PlistBuddy -c 'Print :version' "$installed/mavergreen.plist")" = "$version" ]
cmp "$installed/lib/libavxemu.dylib" "$build/libavxemu.dylib"
# spec: DEVELOPING.md "Mavericks CI" -- linked and inserted tests must use the installed pkg;
#       embedded-core tests still use the same cross build.
rm "$build/libavxemu.dylib"
ln -s "$installed/lib/libavxemu.dylib" "$build/libavxemu.dylib"
log="$build/installed-selftest.log"
AVXEMU_NOCACHE=1 AVXEMU_SELFTEST=1 DYLD_INSERT_LIBRARIES="$installed/lib/libavxemu.dylib" \
  "$build/test/selftest_host" > "$log" 2>&1
cat "$log"
grep -q 'avxemu selftest: all shapes OK' "$log"
# platform: the updater has no --version mode; require Cocoa startup with this version,
#           then terminate it so acceptance does not depend on a live appcast.
app="/$(sh "$SHIPYARD_SCRIPTS/product-name.sh" updater-app avxemu)"
exe=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$app/Contents/Info.plist")
[ "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$app/Contents/Info.plist")" = "$version" ]
"$app/Contents/MacOS/$exe" --background > "$build/updater-start.log" 2>&1 &
updater=$!
trap 'kill "$updater" 2>/dev/null || :; wait "$updater" 2>/dev/null || :' EXIT
n=0
until grep -Fq "start: mode=background version=$version" "$build/updater-start.log"; do
  n=$((n + 1))
  if [ "$n" -ge 30 ] || ! kill -0 "$updater" 2>/dev/null; then
    cat "$build/updater-start.log"; echo 'updater did not reach Cocoa startup'; exit 1
  fi
  sleep 1
done
cat "$build/updater-start.log"
kill "$updater" 2>/dev/null || :
wait "$updater" 2>/dev/null || :
trap - EXIT
cd "$source"
status=0
sh "$SHIPYARD_SCRIPTS/run-repo-tests.sh" mavericks-vm > "$build/mavericks-tests.log" 2>&1 || status=$?
cat "$build/mavericks-tests.log"
[ "$status" -eq 0 ] || exit 1
# platform: threadtest probes the instruction and skips when it cannot fault on this AVX2 host.
#           Every other selected test, including the hardware oracles, must run.
if grep '\*\*\*Skipped' "$build/mavericks-tests.log" | grep -v 'threadtest'; then
  echo 'unexpected skipped Mavericks acceptance test'; exit 1
fi
echo "Mavericks acceptance passed for installed AVXEmu $version"
