# AVXEmu

AVXEmu emulates AVX2/FMA/BMI instructions using AVX1 and SSE4.2.
It is primarily useful on pre-Haswell CPUs such as Sandy Bridge and Ivy Bridge.

It was originally developed by @Wowfunhappy to coax Claude Code for darwin-x64 to run on a wider variety of Mavericks systems (such as my MacPro6,1).

## Using the emulator

On macOS, run an unmodified program with the emulator:

```sh
DYLD_INSERT_LIBRARIES=/usr/local/mavergreen/avxemu/lib/libavxemu.dylib <program>
```

To link a program with it directly, pass `-rpath /usr/local/mavergreen/avxemu/lib` (install name is `@rpath/libavxemu.dylib`).

To modify an existing Mach-O binary's linkage, see [Drydock](https://github.com/Mavergreen/drydock).

On x86-64 glibc Linux, build and install `libavxemu.so` from source (see
[DEVELOPING.md](DEVELOPING.md)), then preload it into a dynamically linked executable:

```sh
LD_PRELOAD=/absolute/install/prefix/lib/libavxemu.so <program>
```

For direct dynamic linkage, retain the library even when the program references no exported
symbols: `cc program.c -L/absolute/install/prefix/lib -Wl,--no-as-needed -lavxemu
-Wl,--as-needed -Wl,-rpath,/absolute/install/prefix/lib -o program`. With the common
`--as-needed` default, `-lavxemu` alone can be discarded and its constructor never runs.
Preload variables also reach child processes; privileged/secure execution may ignore them.

Both platforms require SSE4.2, AVX1, and OS support for AVX register state. Linux validation
targets Ubuntu 22.04 and 24.04 glibc userspaces. Linux rewrites the main executable only and
requires enough trustworthy ELF function metadata to prove required LZCNT/TZCNT patches;
ambiguous or missing coverage fails startup. Stripping can remove that metadata, though
validated unwind records can still supply bounds. See [platform architecture and
limitations](DEVELOPING.md#platform-architecture).

Linux caches use `$XDG_CACHE_HOME/avxemu` when `XDG_CACHE_HOME` is absolute, otherwise
`$HOME/.cache/avxemu`. Set `AVXEMU_CACHE_DIR` to an absolute directory to override this;
`AVXEMU_NOCACHE=1` disables caching. Darwin retains `~/Library/Caches/avxemu`.
The Linux deliverable is a source-built shared library; releases continue to publish the
macOS package. Musl, static executable injection, other architectures and pre-AVX CPUs are
outside the supported scope.
