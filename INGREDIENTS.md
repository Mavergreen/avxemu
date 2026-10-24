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
product runs beyond Mac OS X 10.9: Linux users build `libavxemu.so` from source. Linux CI
gates the existing macOS release; it does not create a Linux package asset.

## Linux build and test tools

The Linux runtime links the target's glibc, libdl, libm and pthread facilities. GCC/Clang,
GNU binutils and CMake >=3.25 are build tools; the Shipyard CMake package remains required.
Ubuntu 22.04 and 24.04 CI install compiler/system development packages from their configured
Ubuntu repositories. These tool versions follow the runner distribution and are logged.

`test/prepare-linux-ci.sh` owns checksum-pinned test dependencies: QEMU 11.1.2 source plus
the exact `test/qemu-count-features.patch` correction, Ubuntu kernel
`6.14.0-37.37~24.04.1`, and fallback Kitware CMake 3.28.3. Static BusyBox, cpio and gzip
construct a full-system initramfs using each runner's own glibc/test binaries. QEMU's pinned
source declares its Python and DTC build dependencies. See DEVELOPING.md, "Linux CI
dependencies", for checksums, preparation and the pristine-QEMU limitation.

These pins are deliberately not Renovate-tracked: they are behavioral validation fixtures,
not ingredients baked into the shipped library/package; changing them requires repeating
the independent instruction controls and full reduced-CPU matrix. No additional
`repackage-on-ingredient-bump` caller is warranted for test tools. There are no product
ingredient pin files here requiring that caller; Shipyard owns SDK/Sparkle pins and updates.

## Declared state

- upstream: UPSTREAM_VERSION

## Conformance deviations

- shipyard-cmake-only:test/prepare-linux-ci.sh: Ubuntu-only CI provisioning checks CMake's version before configuration and installs a verified fallback when needed; Shipyard provides no Linux shipyard-cmake executable. The helper rejects other hosts; macOS retains Shipyard's commands.
- scheme: AVXEmu is its own upstream (no one else's release to repackage), so it versions itself by date as YYYYMMDD.N with no -mavericks.N axis

- on-target:avx-only: KVM's CPUID mask does not force AVX2 instructions to fault. The Mavericks guest uses cpu-isa: avx2 and threadtest reports its expected skip. Real AVX-only faulting and Claude Code acceptance still require the physical Mavericks machine; Linux's required TCG matrix covers reduced-ISA Linux behavior.
- on-target:claude-code: the guest does not fetch Claude Code; the claude-binary label remains an explicit physical-machine acceptance step below.
- on-target:updater-ui: the installed updater must reach Cocoa startup with this run's version. Interactive Sparkle installation and its embedded relaunch helper are not driven by this runtime acceptance job; exit: a deterministic end-to-end updater acceptance fixture.

## Upstream release notes

No upstream release notes: this repo is AVXEmu's upstream; there is no one else's release to link.

## Releasing

A release happens when `UPSTREAM_VERSION` on `main` names source that no release carries
yet: the nightly reconcile dispatches `release.yml`, or you can now:

    gh workflow run release.yml -R Mavergreen/avxemu --ref main

A packaging-only re-release of the same source (a new `.N`):

    gh workflow run release.yml -R Mavergreen/avxemu --ref main -f repackage=true

### Before bumping `UPSTREAM_VERSION`

CI cross-builds on macos-26 and inspects the object instruction inventory there. Its
`on-mavericks` job installs that run's package on 10.9.5, checks its manifest and dylib bytes,
loads the installed dylib for its self-test and repo runtime tests, checks updater startup,
and compares hardware oracles on an AVX2 guest. A CI-only bundle preserves the generated
CTest registry and original paths, plus the same installed Shipyard tools; no compiler or
CLT is required in the guest. Publication needs this job. CI also requires GCC
and Clang Linux validation on Ubuntu 22.04/24.04, including hardware oracles where eligible
and the patched-QEMU full-system AVX1 matrix. Hardware/application skips remain visible;
the lowcpu label is mandatory and missing prerequisites fail. Two macOS checks need
real hardware and are done by hand; the release's notes file in `release-notes/` says both
were done.

**On a Mac OS X 10.9 machine without AVX2** (the target):

    shipyard-cmake --preset native && shipyard-cmake --build --preset native
    CLAUDE_BIN=<a Claude Code binary> shipyard-ctest --preset native

Everything passes or skips. Unlike the KVM guest, this physical AVX-only CPU really
faults on unsupported instructions, exercising runtime relocation and Claude Code.

**On an Intel Mac with AVX2, FMA and BMI:**

    shipyard-cmake --preset cross && shipyard-cmake --build --preset cross
    CLAUDE_BIN=<a Claude Code binary> shipyard-ctest --preset cross -L hardware

Every `*-record` test must say `recording matches`: the committed references still agree
with silicon.

### Before dispatching

CI builds with the runner image's clang, which can change from one run to the next, so the
dylib a release ships has not been through the checks above. Run it on the 10.9 machine before
the release is dispatched. Push the bump to `main`, take that push build's green run for the
check below, and dispatch before the nightly reconcile at 06:41 UTC: it is the backstop that
would otherwise publish the bump unchecked.

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

Record the result in the release's notes file, in the words the notes use:

    Checked on Mac OS X 10.9 without AVX2: the released `libavxemu.dylib` passes its self-test
    (`AVXEMU_SELFTEST`) and runs Claude Code. Its `hardware` tests pass on an Intel Mac with AVX2,
    FMA and BMI.

The sha256 is not written into the notes: it is checked during the 10.9 run. Note it there.
After the dispatch, extract the published `.pkg`'s dylib the same way: its sha256 must be the
one noted. If it is not, the runner image moved between the runs; check the published dylib
the same way, and if it fails, the release is pulled.

If a release ships ahead of the 10.9 run, the run happens immediately after publishing, on the
published release's dylib: its sha256 must match the CI artifact's, and if the run fails, the
release is pulled.

### Re-recording a reference

Only when an emulator change is meant to change an op's output, or an op is added. On the
Intel Mac, run the `*-record` test; it prints the `cp` that adopts the new recording. Commit
the `.ref` file on its own, saying why it changed.
