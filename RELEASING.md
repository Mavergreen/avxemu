# Releasing

A release happens when `UPSTREAM_VERSION` on `main` names source that no release carries
yet: the nightly reconcile dispatches `release.yml`, or you can now:

    gh workflow run release.yml -R Mavergreen/avxemu --ref main

A packaging-only re-release of the same source (a new `.N`):

    gh workflow run release.yml -R Mavergreen/avxemu --ref main -f repackage=true

## Before bumping `UPSTREAM_VERSION`

CI cross-builds and runs the `hermetic` and `replay` tests under Rosetta. Two checks need
real hardware and are done by hand; the release's notes file in `release-notes/` says both
were done.

**On a Mac OS X 10.9 machine without AVX2** (the target):

    shipyard-cmake --preset native && shipyard-cmake --build --preset native
    CLAUDE_BIN=<a Claude Code binary> shipyard-ctest --preset native

Everything passes or skips; this is the only place the `mavericks` label (10.9's dyld) runs.

**On an Intel Mac with AVX2, FMA and BMI:**

    shipyard-cmake --preset cross && shipyard-cmake --build --preset cross
    CLAUDE_BIN=<a Claude Code binary> shipyard-ctest --preset cross -L hardware

Every `*-record` test must say `recording matches`: the committed references still agree
with silicon. (`patchtest` and `bmimem` have no references and no registered tests until
release 2; at this source they fault at load on every CPU.)

## Before dispatching

CI builds with the runner image's clang, which can change from one run to the next, so the
dylib a release ships has not been through the checks above. Run it on the 10.9 machine before
the release is dispatched. The nightly reconcile dispatches a bump by itself, so bring a bump
in by pull request and take that pull request's green run; otherwise take `main`'s.

    gh run download <run-id> -R Mavergreen/avxemu -n avxemu -D avxemu-run
    sh contrib/extract-libavxemu.sh avxemu-run/avxemu-<version>.pkg avxemu-run
    cd avxemu-run
    shasum -a 256 libavxemu.dylib

On Mac OS X 10.9, without AVX2, in that directory (copy `libavxemu.dylib` there if you
downloaded it on another Mac):

    otool -l libavxemu.dylib | grep -A3 LC_VERSION_MIN_MACOSX   # version 10.9
    otool -l libavxemu.dylib | grep 'cmd ?(0x00000032)'          # nothing: no LC_BUILD_VERSION
    AVXEMU_SELFTEST=1 DYLD_INSERT_LIBRARIES=$PWD/libavxemu.dylib /usr/bin/true

Then load it into a Claude Code binary prepared for 10.9, the way README's "Install" section
loads it into a program. Name the binary itself, not a `claude` wrapper, and use one that does
not link avxemu, or two copies load and the one under test may not be the one that runs.

    otool -L <claude binary> | grep avxemu        # nothing; else two copies load
    DYLD_PRINT_LIBRARIES=1 DYLD_INSERT_LIBRARIES=$PWD/libavxemu.dylib <claude binary> --version 2>&1 | grep -E 'libavxemu|^[0-9]'
    DYLD_INSERT_LIBRARIES=$PWD/libavxemu.dylib <claude binary> -p 'Say ok.'

The library list shows this directory's `libavxemu.dylib`, the version prints, and the reply
comes back.

Record the result in the release's notes file:

    Checked before release on Mac OS X 10.9 without AVX2: this build's `libavxemu.dylib`
    (sha256 `<sha256>`) targets 10.9, passes `AVXEMU_SELFTEST` and runs Claude Code <version>.

After the dispatch, extract the published `.pkg`'s dylib the same way: its sha256 must be the
one recorded. If it is not, the runner image moved between the runs; check the published dylib
the same way and correct the release's notes.

If a release ships ahead of the 10.9 run, the run happens immediately after publishing, on the
published release's dylib: its sha256 must match the CI artifact's, and if the run fails, the
release is pulled.

## Re-recording a reference

Only when an emulator change is meant to change an op's output, or an op is added. On the
Intel Mac, run the `*-record` test; it prints the `cp` that adopts the new recording. Commit
the `.ref` file on its own, saying why it changed.
