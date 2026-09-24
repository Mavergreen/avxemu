# Developing AVXEmu

Build and test it now:

```sh
shipyard-cmake --preset native          # on Mac OS X 10.9; on a modern Mac use --preset cross
shipyard-cmake --build --preset native
shipyard-ctest --preset native
```

Done when ctest prints `100% tests passed`. Skips are fine. In CI the build takes about 15 s and
the tests about 5 s.

`shipyard-cmake` comes from [shipyard](https://github.com/Mavergreen/shipyard). The build is
out of tree: `libavxemu.dylib` lands in `$TMPDIR/mm-build/<checkout>-<preset>/`.

## Which machine runs which tests

Each test carries a label for what it needs. A test whose need is unmet skips.

| label | needs | runs on |
|---|---|---|
| `hermetic` | only the emulator, any x86_64 | everywhere, CI included (under Rosetta) |
| `replay` | only the emulator, checked against recordings from AVX2 silicon | everywhere, CI included |
| `hardware` | a real AVX2/FMA/BMI CPU, not Rosetta | an Intel Mac with AVX2 |
| `mavericks` | Mac OS X 10.9's dyld | the 10.9 machine |
| `claude-binary` | `CLAUDE_BIN=<a Claude Code binary>` | wherever you set it |

To run one label: `shipyard-ctest --preset native -L replay`.

**How replay works.** `oracle`, `bmi_oracle`, `patchtest` and `bmimem` run each instruction on
the real CPU and in the emulator side by side, in `record` mode, and write
`test/reference/<name>.ref`: per op, a digest of the hardware's outputs. `check` mode replays the
emulator alone against that file. So the emulator is checked against silicon on CPUs that lack
the instructions.

**Re-recording a reference:** see INGREDIENTS.md, "Releasing".

## Known failures

- `overread-fault` is marked `--fails-when rosetta`. Under Rosetta the fault handler does not
  repair the straddling read. Cause not yet diagnosed.

A marked test that starts passing fails with `UNEXPECTED PASS`. Remove its mark.

## Debugging

The first thing to run on a new machine, without Claude Code. It prints PASS or FAIL:

```sh
AVXEMU_SELFTEST=1 DYLD_INSERT_LIBRARIES=/usr/local/mavergreen/avxemu/lib/libavxemu.dylib /usr/bin/true
```

The dylib is silent in normal use. A `#UD` it can't emulate is chained to the program's own
handler, so a coverage gap shows up as Bun's "illegal instruction" crash report, with the
faulting address.

| env var | effect |
|---|---|
| `AVXEMU_SELFTEST=1` | check trap → decode → emulate → writeback on this CPU, then exit |
| `AVXEMU_DISABLE=1` | bypass the emulator entirely |
| `AVXEMU_NO_REBIND=1` | when linked rather than inserted, skip rebinding `sigaction`/`signal` |
| `AVXEMU_FORCETRAMP=1`, `AVXEMU_FORCEPATCH=1` | tests only: force trampolining or lzcnt patching on an AVX2 CPU |

**The risk the tests don't remove:** an emulation that is wrong in a way the oracles never
exercised corrupts results silently. The defences are the exhaustive differential oracles, the
check that forced-trampolined output is byte-identical to native, and `AVXEMU_SELFTEST` on the
target.

## How it works

A load-time constructor arms four mechanisms.

1. **Trampolines, the fast path.** Before the program runs, avxemu scans the main executable's
   `__text`, guided by `LC_FUNCTION_STARTS`. It rewrites each run of faulting instructions into
   a 5-byte `jmp` to a generated thunk. The thunk saves the registers, emulates on a per-thread
   side stack, restores them and jumps back. About 97% of faulting sites never trap.
   Jump-table functions are mapped by recursive descent. (`src/tramp.c`, `src/tramp.s`,
   `src/lde.c`)
2. **The `SIGILL` handler, the fallback.** It covers sites too small or too close to a branch
   target for a 5-byte jump. It decodes at `rip`, reads the real `ymm` registers from the AVX
   signal frame (an AVX1 CPU has them, so no shadow register file), emulates, writes back and
   advances `rip`. Genuine `ud2` traps are chained, never swallowed. After a site traps once,
   avxemu tries to relocate a window of the instructions around it into its code cache and
   patch the site with a `jmp`, so it stops trapping. If it can't prove that safe, the site
   keeps using the handler. (`src/handler.c`, `src/reloc.c`)
3. **lzcnt/tzcnt patching.** These don't fault on a pre-Haswell CPU: they silently run as
   `bsr`/`bsf` and give wrong answers. avxemu rewrites each one's `F3` prefix to `F0` (lock) in
   mapped memory, so it faults and gets emulated. The file on disk is never changed.
   (`src/patch_mem.c`)
4. **The over-read fixup.** Optimized code reads a full vector past a buffer's end, relying on
   the next page being mapped. On a `SIGSEGV`/`SIGBUS` just past a mapped region, avxemu maps a
   zero page and retries. Anything else is chained to the previous handler.

**One emulator core.** The trampolines and the handler both call `avxemu_emulate()`
(`src/exec.c`, `src/exec_bmi.c`, `src/softfma.c`). It is compiled SSE4.2-only with `-mno-avx`,
and a test checks it emits no VEX. Most AVX2 integer ops work per 128-bit lane, so the core
applies the SSE equivalent to each half (`HALFOP` in `exec.c`).

**Staying first in line.** Bun installs its own `SIGILL` reporter. avxemu interposes
`sigaction`/`signal`, so a program's `SIGILL` handler becomes avxemu's chain target instead of
replacing it. The real `sigaction` is found by parsing `libsystem_c.dylib`'s symbol table,
because interposition breaks `dlsym`.

**Two ways to load it:**

- **Inserted:** `DYLD_INSERT_LIBRARIES`. No setup, but every child process inherits it.
- **Linked:** `change_dylib -insert` (see [Drydock](https://github.com/Mavergreen/drydock)), so
  dyld initializes it before every other dependency. On this path avxemu does dyld's
  interposition itself, by rebinding each loaded image's `sigaction`/`signal` pointers.

Mavericks Forever's `claude` wrapper inserted it only when
`sysctl machdep.cpu.leaf7_features` showed no AVX2. That is a good pattern for any integrator.

## Why emulate instead of rebuilding

Claude Code ships as one `bun build --compile` executable, and the only `darwin-x64` build needs
AVX2. Moving its JavaScript onto a stock baseline Bun does not work: Claude Code runs on a
private Bun fork, which may carry attestation. Emulation keeps the genuine binary intact and
only supplies the instructions the CPU lacks.

The floor is AVX1. A CPU without AVX at all (Nehalem, Core 2) has no hardware `ymm` registers,
so it would need a software register file. That is not attempted.

## What must be emulated

Measured by disassembling Claude Code 2.1.166 (15,380,168 instructions):

| class | static count | difficulty |
|---|---|---|
| AVX2 256-bit integer (`vpaddd`, `vpshufb`, …) | ~95k ALU ops | easy: per-lane half-split |
| AVX2 cross-lane (`vperm*`, `vpbroadcast*`, `vpmovzx*`) | thousands | real lane logic |
| FMA3, mostly scalar `sd` | 419 | needs correct fused rounding (software FMA) |
| BMI1/BMI2, LZCNT, TZCNT, MOVBE | ~38,000 | easy, individually |
| F16C (`vcvtph2ps`) | 4 | easy |

Two facts keep this tractable:

- **No gathers:** `vpgather*` appears zero times.
- **The 256-bit moves are AVX1:** `vmovups`, `vmovdqu` and `vmovaps` (~155k) run natively.

## Performance

Per emulated instruction, measured on Haswell:

| path | cost |
|---|---|
| `SIGILL` trap → kernel → handler → return | ~4,200 ns |
| trampoline thunk | ~113 ns |
| native AVX1 doing the same work | ~0.76 ns |

End to end, on a real CPU-bound `claude` launch:

- **Steady-state execution:** about 1.2× native. AVX2 is a thin slice of what runs.
- **Startup:** a one-time scan of about 1 s per launch, walking 60 MB of `__text`.
- **A short command:** `claude --help` is about 4× slower, because the scan dominates.
- **SIMD-heavy bursts:** slower than that.

Only a native build without AVX2 beats this.

## What is proven, and how

- **Emulator core:** bit-for-bit against hardware. The vector/FMA and BMI oracles, plus a
  140k-run native-vs-emulated fuzzer over the real binary.
- **Decoder:** every VEX/BMI instruction in the real binary (178,750, zero mismatches). This
  corpus caught two crash bugs random testing missed (`0x67`-padded VEX, `MOVBE` store) and a
  BMI2 memory-operand width bug.
- **lzcnt/tzcnt patch:** 5,158 sites patched, each checked `F3`→`F0` and a real zcnt.
- **Trampolines:** forced-trampolined output is byte-identical to native.
- **On a real Ivy Bridge Mac:** the AVX1 signal-frame layout, coexistence with Bun's handlers,
  and end-to-end use. The decisive bug found there: the thunk used `sub` to set up its frame,
  which clobbered the flags before saving them, so a branch after a scheduled `vpbroadcast` went
  wrong. The fix is `lea`. `tramptest` seeds every flag to catch this class of bug.

## Where things are

**The emulator** (`src/`):

| file | what |
|---|---|
| `decode.c`, `lde.c` | instruction decoder; length decoder and recursive-descent scanner |
| `exec.c`, `exec_bmi.c`, `softfma.c` | the SSE-only emulator core |
| `tramp.c`, `tramp.s`, `reloc.c` | trampoline scanner, installer and thunk template; runtime relocation of trapping sites |
| `handler.c`, `patch_mem.c` | the constructor, signal handlers, lzcnt/tzcnt patcher |
| `selftest.c`, `selftest.s` | `AVXEMU_SELFTEST` |

The headers `ymm.h`, `regs.h`, `regfile.h` and `vexops.h` define the 256-bit value, the CPU
state the emulator touches, the register file both paths fill, and the opcode enum. `names.c`
holds mnemonic strings for diagnostics.

**The tests** (`test/`, registered in `test/CMakeLists.txt`):

| file | checks |
|---|---|
| `oracle.c`, `bmi_oracle.c` | emulator vs hardware: vectors and FMA; BMI values and flags |
| `patchtest.c`, `bmimem.c` | emulator vs hardware: patched lzcnt/tzcnt; BMI with a memory operand |
| `bintest.c`, `zdecode.c`, `patchdiff.c` | decoder and lzcnt patch over the real Claude binary |
| `fuzz.c` | native vs emulated, full 16-`ymm` compare |
| `tramptest.c`, `memtest.c`, `inject.c` | thunks, every addressing mode, fault injection end to end |
| `overread*.c`, `guard_page.c` | over-read fixup, and that guard pages stay untouched |

**The rest:** `packaging/build-pkg.sh` builds the `.pkg` with shipyard, `contrib/` holds scripts
for consumers (`extract-libavxemu.sh`), and `.github/workflows/` holds CI.

## Releasing

See INGREDIENTS.md, "Releasing": which checks run by hand, on which machine, before a release.
