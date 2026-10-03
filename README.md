# AVXEmu

**This README has not been read or edited by a human yet.** Until it has, this project cannot cut
its first release.

AVXEmu runs AVX2/FMA/BMI binaries on pre-Haswell CPUs that have AVX1 and SSE4.2 (Sandy Bridge,
Ivy Bridge). Wowfunhappy wrote it for [Mavericks Forever](https://mavericksforever.com/claude/),
and Amitai Schleier's fixes and features followed in the same repository. It is now developed
here; see `PROVENANCE.md`.

## Install

Download `avxemu-<version>.pkg` from the
[latest release](https://github.com/Mavergreen/avxemu/releases/latest) and open it (Mac OS X
10.9.5 or later, Intel). It installs `/usr/local/mavergreen/avxemu/lib/libavxemu.dylib` and an
updater that keeps it current. To load it into a program:

    DYLD_INSERT_LIBRARIES=/usr/local/mavergreen/avxemu/lib/libavxemu.dylib <program>

or link it, adding `-rpath /usr/local/mavergreen/avxemu/lib` (its install name is
`@rpath/libavxemu.dylib`). An installer that ships its own copy can take the dylib out of the
package without installing it: `sh contrib/extract-libavxemu.sh avxemu-<version>.pkg <dir>`.

A trap-and-emulate + in-memory-rewrite layer that lets a binary compiled for
Intel **Haswell** (AVX2 + FMA + BMI1/BMI2 + LZCNT + MOVBE) run on a CPU that only
has **AVX1 + SSE4.2** — i.e. **Sandy Bridge (2011) / Ivy Bridge (2012)**. Built to
get the native Claude Code binary running on those Macs under *Mavericks
Forever*, but the emulator itself is application-agnostic and reusable.

## Why this exists (and why not the obvious alternative)

Claude Code ships as a single `bun build --compile` executable, and Anthropic
only publish an **AVX2** build for `darwin-x64`. On a pre-Haswell CPU it dies
(`SIGILL` on the first AVX2 opcode, or a "CPU lacks AVX support" guard).

The tempting fix — transplant the JS bundle onto a stock **`bun-baseline`**
(SSE4.2) runtime — **does not work**, because Claude Code does not run on stock
Bun. It runs on **`bun-anthropic`**, a private fork which may include (now or in
the future) additional attestation we do not know about.

Emulating AVX2 **keeps the original, genuine binary intact** — attestation and
all — and only fills in the instructions the CPU can't execute. This is both the
safest path (survives attestation enforcement) and the most honest (the real
client's real token).

## The trap set (what must be emulated)

Measured by disassembling Claude Code `2.1.166` (15,380,168 instructions). On an
**AVX1** target, everything Haswell added over Sandy/Ivy Bridge faults:

| class | static count | notes |
|---|---|---|
| AVX2 256-bit integer (`vpaddd`, `vpand`, `vpshufb`, `vpcmpeqb`, …) | ~250k touch `ymm`, of which ~95k are non-move ALU | **most are per-128-lane → trivial half-split** |
| AVX2 cross-lane (`vperm*`, `vpbroadcast*`, `v{ins,ext}racti128`, `vpmovzx*`) | thousands | need real lane logic |
| FMA3 (`vfmadd*`/`vfnmadd*`, mostly scalar `sd`) | 419 | needs correct fused rounding (software FMA) |
| BMI1/BMI2 (`shlx`,`shrx`,`sarx`,`rorx`,`mulx`,`bzhi`,`pdep`,`pext`,`andn`,`blsi/blsr`) | ~38,000 | GPR+flags domain; individually trivial |
| LZCNT / TZCNT / MOVBE | included above | trivial |
| F16C (`vcvtph2ps`) | 4 | trivial |
| **gather (`vpgather*`)** | **0** | the one nasty feature — *absent*. |

Two facts make this tractable: **zero gathers**, and the 256-bit moves
(`vmovups`/`vmovdqu`/`vmovaps`, ~155k) are **AVX1 — they run native**, no
emulation. The real work is a few dozen regular integer ops, ~38k dead-simple
scalar BMI ops, and a small careful core (FMA rounding, cross-lane permutes).

> NB: the target floor is **AVX1**. A no-AVX target (Nehalem/Core 2) is a far
> bigger job — every VEX op faults and the 256-bit `ymm` state has no hardware
> home, forcing a software shadow register file. Not attempted here.

## How it works

Four cooperating mechanisms, all armed from a load-time constructor:

1. **Eager trampolining (the fast path).** At load, before the patched code can
   run, the installer scans the main executable's `__text` (guided by
   `LC_FUNCTION_STARTS`) and rewrites each run of consecutive faulting
   instructions to a 5-byte `jmp` into a generated **thunk**. The thunk spills
   the live registers, runs the shared emulator on a private per-thread side
   stack, reloads the (updated) state, and jumps to the instruction after the
   run — so those instructions **never trap**. A function that decodes cleanly is
   walked linearly; one with embedded jump-table data is mapped by **recursive
   descent** (following control flow and resolving switch tables) so switch-heavy
   code is covered too instead of skipped. **~97% of faulting sites end up
   trampolined.** (`src/tramp.c`, `src/tramp.s`, `src/lde.c`.)

2. **`SIGILL` handler (the fallback).** The ~2.6% of sites that can't host a
   5-byte jump safely — isolated 4-byte instructions wedged against a branch
   target, code the scanner can't prove reachable — keep trapping. On `#UD` the
   handler decodes at `rip`, reads the source registers straight from the AVX
   signal frame (real `ymm` is present on an AVX1 CPU, so **no shadow register
   file is needed**), runs the **same** emulator core, writes the result back,
   and advances `rip`. Genuine `ud2`/assert traps that JSC/Bun emit on purpose
   are **chained** to the previous handler, never swallowed. (`src/handler.c`.)

3. **lzcnt/tzcnt in-memory patching.** These don't fault on a pre-Haswell CPU —
   they silently run as `bsr`/`bsf` (wrong results), so they can't be
   trap-and-emulated. At load each one's `F3` prefix is rewritten to `F0` (lock)
   *in mapped memory*, making it `lock bsr/bsf` → `#UD` → emulated correctly. The
   on-disk binary is never modified; only mapped code pages, only on CPUs that
   lack LZCNT. (`src/patch_mem.c`.)

4. **SIMD over-read fixup (defensive).** Optimized code reads a full vector past
   a buffer's logical end, relying on the trailing page being mapped — true on
   the AVX2 machines it was built for, but on the target that page can be
   unmapped. A `SIGSEGV`/`SIGBUS` handler maps a zero page just off the end of a
   mapped region and retries (the over-read bytes aren't used by correct code),
   reproducing the AVX2 machine's behaviour. Anything it can't attribute to an
   over-read chains straight to the previous handler.

The shared **emulator core** (`src/exec.c`, `exec_bmi.c`, `softfma.c`) is
compiled **SSE4.2-only, `-mno-avx`**, verified to emit no VEX, so it is safe to
run on the target. The dominant op family is *per-128-bit-lane*, emulated by
applying the SSE equivalent to the low and high halves (`HALFOP` in `exec.c`).
Both the SIGILL and trampoline paths call the exact same `avxemu_emulate()`, so
there is one emulation code path, not two.

The dylib can be loaded either way. `DYLD_INSERT_LIBRARIES` is the zero-setup
route, and dyld honours the `__DATA,__interpose` section for inserted images. A
host that would rather not have every child process inherit the emulator links
it into the Mach-O instead, with `change_dylib -insert` so that dyld initializes
it ahead of every other dependency; on that path the dylib reproduces dyld's end
state itself by rebinding the `sigaction`/`signal` symbol pointers of each loaded
image (`AVXEMU_NO_REBIND=1` opts out). Either way it is armed before the app's
first AVX2 instruction, and the app itself needs no change.

> **Frontline handler.** Bun installs its own crash reporter on `SIGILL` during
> startup. The dylib interposes `sigaction`/`signal`: a runtime's `SIGILL`
> registration is recorded as our chain target rather than replacing us, so we
> keep first crack at the instructions we emulate while the runtime's reporter
> still fires for genuine `#UD`s. The genuine libc `sigaction` is reached by
> parsing `libsystem_c.dylib`'s symbol table (dyld interposition poisons
> `dlsym`).

## Performance

Per emulated instruction, measured on Haswell:

| path | cost / instruction |
|---|---|
| pure `SIGILL` trap → kernel → handler → return | ~4,200 ns |
| **trampoline thunk** (save state → emulate → restore) | **~113 ns** |
| (for reference) native 128-bit AVX1 doing the same work | ~0.76 ns |

Trampolining removes the kernel round-trip — **~38× cheaper per instruction** —
which is the whole reason it exists. Against a hypothetical binary *compiled*
without AVX2 (native AVX1) a trampolined op is still ~150× slower, because the
thunk spills the full register context every time and native code never does.

But AVX2 is a thin slice of what actually executes. Measured end to end on a
real (CPU-bound) `claude` launch:

- **steady-state execution ≈ 1.2× native** — the ~150×/op diluted across a
  mostly-scalar instruction mix (bytecode interpreter, GC, allocation, syscalls);
- plus a **one-time ~1 s scan** at every launch (walking the 60 MB `__text` and
  writing the trampolines).

So a quick `claude --help` is ~4× slower (scan-dominated), while a real
session — launch once, then network-bound interactive work — is roughly a
one-second-longer startup and ~20% slower execution. A SIMD-bound burst (huge
file/string crunch) is worse on those phases. The only way to beat this is a
native no-AVX2 build; emulation can't match "just run the instruction."

## Building

    shipyard-cmake --preset native      # on Mac OS X 10.9
    shipyard-cmake --preset cross       # on a modern Mac, against the pinned 10.9 SDK
    shipyard-cmake --build --preset <native|cross>
    shipyard-ctest --preset <native|cross>

`shipyard-cmake` comes from [shipyard](https://github.com/Mavergreen/shipyard). The build is
out of tree; `libavxemu.dylib` lands in `$TMPDIR/mm-build/<checkout>-<preset>/`.

## Testing without the target hardware

Every step of the old `build.sh` is a ctest, labelled by what it needs:

| label | needs |
|---|---|
| `hermetic` | only the emulator, on any x86_64 |
| `replay` | only the emulator: checks it against outputs recorded on AVX2 silicon |
| `hardware` | a real AVX2/FMA/BMI CPU (not Rosetta): re-records and compares |
| `mavericks` | Mac OS X 10.9's dyld |
| `claude-binary` | `CLAUDE_BIN` naming a Claude Code binary |

A test whose needs are unmet skips. The differential oracles `oracle` and `bmi_oracle` run the
real instruction beside the emulator in `record` mode and write `test/reference/<name>.ref`: per
op, a digest of the hardware's outputs. Their `check` mode replays the emulator alone against
the committed reference, so it runs on any x86_64: CI runs it under Rosetta, and the 10.9
machine runs it on the target CPU. `RELEASING.md` says which machine runs what before a release.

At release 1's source, a test program that links the emulator's core into itself faults at
load on every CPU: the load-time constructor patches the `__text` it is running from. Release 2
brings the fixes. `overread-fault` is a known failure under Rosetta, where the fault handler cannot yet repair the straddling read. Until then `inject`, `memtest`, `tramptest` and `overread` are marked as known
failures, and the `patchtest` and `bmimem` oracles are built but not registered as tests, and
have no references.

## Status

- [x] **Emulator core** — all AVX2 integer ops, FMA3 (ss/sd/ps/pd), F16C,
      BMI1/BMI2/LZCNT/TZCNT/MOVBE. Validated bit-for-bit vs hardware (vector+FMA
      oracle, BMI oracle, and the 140k-run native-vs-emulated fuzzer over the real
      binary). FMA via software `fma()` (no `vfmadd`), bit-exact vs silicon.
- [x] **Decoder** — VEX2/VEX3 + legacy, ModRM/SIB/disp, segment prefixes,
      RIP-relative. Validated against every VEX/BMI instruction in the real binary
      (178,750 instructions, 0 mismatches). The corpus test caught two real
      target-crashing bugs random testing missed (`0x67`-padded VEX, `MOVBE`
      store) plus a VEX BMI2 memory-operand width bug.
- [x] **SIGILL handler** — reads/writes the AVX mcontext, advances RIP, chains
      genuine `ud2`; stays frontline via `sigaction`/`signal` interposition.
- [x] **lzcnt/tzcnt in-memory patch** — `F3`→`F0` via `vm_protect(COPY)`, located
      with a recursive-descent, jump-table-aware scan. 5,158 sites patched,
      validated all-`F3`→`F0` and all real zcnt.
- [x] **Trampoline** — eager, load-time, run-based; per-thread side stack;
      recursive-descent scanner covers switch-heavy functions (**~97% of faulting
      sites**, the rest fall back to SIGILL). Forced-trampolined output is
      byte-identical to native.
- [x] **SIMD over-read fixup** — maps a zero page on a genuine just-past-a-buffer
      over-read; leaves real wild pointers and PROT_NONE guard pages untouched.
- [x] **Remote validation — on a real Ivy Bridge Mac.** Confirmed the AVX1
      mcontext flavor, coexistence with Bun/JSC handlers, and end-to-end use.
      This is where the decisive bug surfaced: the trampoline thunk set up its
      frame with `sub`, which clobbers the arithmetic flags **before** they were
      saved, so a flag-neutral instruction (e.g. a constant-loading `vpbroadcast`
      the compiler scheduled between a `cmp` and its `jcc`) corrupted the branch.
      Fixed by using `lea` (flag-neutral). `tramptest` seeds every arithmetic flag
      to catch the class, but at release 1 it is a known failure (see "Testing
      without the target hardware"), so it guards against a regression only from
      release 2.

## Layout

```
CMakeLists.txt    top-level build (out-of-tree; see Building)
src/ymm.h         256-bit value type (raw bytes)
src/regs.h        cpu_state the emulator touches
src/regfile.h     flat register-file the SIGILL + trampoline paths both fill
src/vexops.h      internal opcode enum + executor API
src/exec.c        SSE-only vector executor (AVX2 + F16C)
src/exec_bmi.c    BMI1/BMI2/LZCNT/TZCNT/MOVBE scalar executor
src/softfma.c     software FMA (libm) + half->float
src/names.c       mnemonic strings
src/decode.h/.c   VEX/legacy instruction decoder (+ segment prefixes)
src/lde.h/.c      length decoder + recursive-descent reachability/switch resolver
src/patch_mem.c   in-memory lzcnt/tzcnt F3->F0 patcher
src/tramp.c       eager trampoline scanner/installer + thunk dispatch
src/tramp.s       trampoline thunk template + side-stack switch
src/handler.c     SIGILL emulate, SIGSEGV/SIGBUS over-read fixup, constructor
src/selftest.s/.c in-dylib preflight (AVXEMU_SELFTEST)
test/CMakeLists.txt  test build + ctest registration (labels, skips, known failures)
test/oracle.c        vector + FMA differential oracle
test/bmi_oracle.c    BMI differential oracle (values + flags)
test/bmimem.c/.s     BMI with a memory operand: emulated vs native
test/bintest.c       decoder vs every VEX/BMI instruction in the real binary
test/fuzz.c/.s       native-vs-emulated differential fuzzer (full 16-YMM compare)
test/memtest.c/.s    end-to-end checks of every EA / addressing mode
test/patchtest.c     patched (F0/lock) lzcnt/tzcnt fault->emulate vs hardware
test/patchdiff.c     lzcnt patch safety over the real binary
test/zdecode.c       decoder coverage over the real binary
test/tramptest.c     trampoline thunk: emulate + full register/flag preservation
test/overread.c      page-safe vector load across an unmapped page
test/overread_fault.c / guard_page.c   runtime over-read fixup + guard-page safety
test/inject.c        decoder check + end-to-end fault-injection driver
packaging/        build-pkg.sh: stages the dylib and updater, builds the .pkg with shipyard
contrib/          scripts for consumers, e.g. extract-libavxemu.sh
```

## Operation & debugging

The shipped dylib is **silent in normal use** — genuine `#UD`s it can't emulate
are chained to the runtime, so a real coverage gap surfaces as Bun's own
"illegal instruction" crash report (with the faulting address), no separate
logging needed. The verbose per-trap diagnostics used while bringing the
emulator up were removed once it stabilized. Four env knobs remain:

- **`AVXEMU_SELFTEST=1`** — preflight. Validates the whole
  trap→decode→emulate→writeback path on that exact CPU and exits PASS/FAIL,
  *without* needing Claude Code. First thing to run on a new machine:
  ```sh
  AVXEMU_SELFTEST=1 DYLD_INSERT_LIBRARIES=/usr/local/mavergreen/avxemu/lib/libavxemu.dylib /usr/bin/true
  ```
- **`AVXEMU_DISABLE=1`** — bypass the emulator entirely (escape hatch).
- **`AVXEMU_FORCETRAMP=1` / `AVXEMU_FORCEPATCH=1`** — *dev/test only.* Force
  trampolining / lzcnt-patching on an AVX2 host so the test suite can exercise
  the whole path on hardware that wouldn't otherwise fault. No effect worth using
  in production.

What this does *not* catch is a wrong emulation the oracle missed (silent
corruption). The mitigations are the exhaustive differential oracle (the math is
proven vs silicon), the byte-identical forced-trampoline check, and
`AVXEMU_SELFTEST` on the target.

## Installer integration

Before this release, `Mavericks Forever/public/claude/install.sh` downloaded
`libavxemu.dylib` into `~/.local/share/claude-mavericks/`, and its `claude` wrapper
exported `DYLD_INSERT_LIBRARIES=…/libavxemu.dylib` only when the CPU lacked AVX2
(`sysctl machdep.cpu.leaf7_features`); AVX2-capable Macs ran the native binary untouched,
older Macs got transparent emulation. The dylib needs none of the Mach-O patching the main
binary requires: it was built natively on Mavericks then, and is cross-built against the
pinned 10.9 SDK now.
