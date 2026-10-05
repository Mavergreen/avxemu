# AVXEmu

AVXEmu emulates AVX2/FMA/BMI instructions using AVX1 and SSE4.2.
It is primarily useful on pre-Haswell CPUs such as Sandy Bridge and Ivy Bridge.

It was originally developed by @Wowfunhappy to coax Claude Code for darwin-x64 to run on a wider variety of Mavericks systems (such as my MacPro6,1).

## Using the emulator

To run an unmodified program with the emulator:

```sh
DYLD_INSERT_LIBRARIES=/usr/local/mavergreen/avxemu/lib/libavxemu.dylib <program>
```

To link a program with it directly, pass `-rpath /usr/local/mavergreen/avxemu/lib` (install name is `@rpath/libavxemu.dylib`).

To modify an existing Mach-O binary's linkage, see [Drydock](https://github.com/Mavergreen/drydock).
