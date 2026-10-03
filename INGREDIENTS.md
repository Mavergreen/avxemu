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
