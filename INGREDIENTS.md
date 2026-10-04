# Build ingredients

Everything baked into what AVXEmu ships, and how a change to it reaches a release.

AVXEmu is **its own upstream**. It began in Wowfunhappy/Mavericks-Porting-Resources (see
`PROVENANCE.md`); this repo is now where it is developed, and nothing external releases it.

| Ingredient | Pinned in | Renovate | On a bump |
|---|---|---|---|
| the emulator's source (own upstream) | `UPSTREAM_VERSION`: the date of the newest source a release carries, bumped by hand | ❌ untrackable: nothing external releases it | bumping it on `main` is the decision to release; the nightly reconcile publishes it |
| MacOSX10.9 SDK, CMake modules, compat guard, packaging and signing scripts | `Mavergreen/shipyard@v1` | ✅ github-actions manager tracks the tag | `@v1` moves without the pin changing, so nothing repackages by itself |
| Sparkle 1.27.3, in the updater | shipyard's `fetch_sparkle_framework.sh`, pinned by hash there | ❌ untrackable here: shipyard owns that pin | follows shipyard |
| `test/reference/*.ref` | committed | ❌ untrackable: recorded on real AVX2/FMA/BMI silicon, deliberately frozen | never bumped by a bot; re-recording is its own commit saying why |

Not ingredients: `CMakeLists.txt`, `test/`, `packaging/`, `contrib/` and the workflows are this
repo's own recipe.

The display name is **AVXEmu** in every register (package title, appcast channel, updater,
release notes), without the family's "Mavericks" / "for Mavericks" wording, because the
product is meant to run beyond Mac OS X 10.9: a Linux build is a planned follow-up.

## Declared state

- upstream: UPSTREAM_VERSION

## Conformance deviations

- scheme: AVXEmu is its own upstream (no one else's release to repackage), so it versions itself by date as YYYYMMDD.N with no -mavericks.N axis
- rosetta:.github/workflows/release.yml: the release job runs on an Apple Silicon (arm64) runner and primes Rosetta ("Ensure this runner can run x86_64 (Rosetta)") so ctest can execute the shipped x86_64 libavxemu.dylib, and the x86_64 test programs that load it (the hermetic and replay labels), before they ship. It cannot run natively: AVXEmu emulates x86 instructions, so the product under test is x86_64 by nature. The hardware label deliberately does not run translated: Rosetta is not ground truth for AVX2/FMA/BMI, and record mode refuses to run under it. Reconsider when these tests can move to an x86_64 host (the 10.9 box, an Intel runner, or the Mavericks VM runner the family is bringing to GitHub Actions); at the latest before macOS 28 removes Rosetta.
- rosetta:test/CMakeLists.txt: the `overread-fault` test runs translated in CI, on the arm64 runner, as a declared known failure (`--fails-when rosetta`): under Rosetta avxemu's fault handler does not repair and retry the read that straddles an unmapped page, and the test segfaults. It cannot run natively there yet: CI has only arm64 runners, and the program under test is x86_64 by nature. Native 10.9 and Intel-with-AVX2 runs pass it; only the translated CI leg is marked, and run.sh fails the test if it unexpectedly passes there. Reconsider when overread-fault can move to the Mavericks VM runner the family is bringing to GitHub Actions, or to the 10.9 box, at which point the mark comes off; at the latest before macOS 28 removes Rosetta.

## Upstream release notes

No upstream release notes: this repo is AVXEmu's upstream; there is no one else's release to link.

## Releasing

A release happens when `UPSTREAM_VERSION` on `main` names source that no release carries
yet: the nightly reconcile dispatches `release.yml`, or you can now:

    gh workflow run release.yml -R Mavergreen/avxemu --ref main

A packaging-only re-release of the same source (a new `.N`):

    gh workflow run release.yml -R Mavergreen/avxemu --ref main -f repackage=true

### Before bumping `UPSTREAM_VERSION`

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

### Before dispatching

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

### Re-recording a reference

Only when an emulator change is meant to change an op's output, or an op is added. On the
Intel Mac, run the `*-record` test; it prints the `cp` that adopts the new recording. Commit
the `.ref` file on its own, saying why it changed.
