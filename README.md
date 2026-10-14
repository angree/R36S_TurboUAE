# TurboUAE

An Amiga emulator with a working 68k **JIT for 64-bit ARM**, for the **R36S** handheld and other
RK3326 / ArkOS consoles. Port by Grzegorz Korycki. Two ways to run it, both in one archive:

- **Ports version** - its own program in the Ports menu, no RetroArch needed.
- **RetroArch core** (`turbouae_libretro.so`) - started from EmulationStation's Amiga lists like
  any other core, as an emulator "turbouae" of its own next to the stock ones.

TurboUAE is based on [Amiberry](https://github.com/BlitterStudio/amiberry) **v3.3** (the last release
on the lighter uae4arm-derived core that already has the AArch64 JIT), with fixes that make the JIT
usable on this console and additions for a handheld without keyboard and mouse.

**Download:** see [Releases](https://github.com/angree/R36S_TurboUAE/releases) - installation (Ports
or core), controls and credits are in the `README.txt` inside the archive (also in
[release/README.txt](release/README.txt)).

**Kickstart for the JIT: 3.1 for the A4000 (40.068).** With the JIT the Amiga boots only from this
one (measured: the A1200, A3000 and A500/A600 3.1 and the A1200 3.0 hang at boot). TurboUAE finds
it by its contents or under the usual names (`kick31.rom`, `kick40068.A4000`,
`amiga-os-310-a4000.rom`, TOSEC names). No Kickstart, Workbench or game is included.

## Measured on an R36S (Cortex-A35, 1.5 GHz), emulated 68020

| | Dhrystone 2.1 / s |
|---|---|
| interpreter | ~9 400 |
| JIT | ~135 000 (xSysInfo: ~78 MIPS) |

## New in 0.2.0

- **libretro core**: an SDL shim turns the emulator's window, sound and pad into RetroArch's frame
  buffer, audio and input; the emulator runs in its own thread. 59 core options (machine, CPU,
  JIT, memory, chipset, video, sound, media, input), the complete Amiberry settings screen (L2) and
  the on-screen keyboard (R2), any pad button as an Amiga key, sticks / d-pad as cursor keys.
- **JIT in a shared library**: compiled code reaches the cycle counter at a fixed small distance
  from the register block - in a PIC build that distance was 900 KB and Kickstart hung. Both now
  live in one block. (This, not "code below 4 GB", was why the JIT seemed to need a non-PIE binary.)
- **WHDLoad with the JIT** in both versions: Amiberry's A1200 WHDLoad machine never boots with the
  JIT (A1200 chipset mode, Zorro II RAM and the A1200 Kickstarts each hang it), so WHDLoad games get
  the Workbench machine's setup: A4000 chipset mode, 64 MB Zorro III, the A4000 Kickstart 3.1.

## What was changed against Amiberry v3.3

- **JIT fault handler restored.** SDL on ArkOS replaced Amiberry's SIGSEGV handler, so every JIT
  access the handler should have redone through the memory banks crashed the emulator (SysInfo,
  xSysInfo, memory-probing software). The handler is put back on top.
- **JIT memory holes.** Unbacked parts of the 24-bit address space are made inaccessible, as WinUAE
  does; before, the JIT read and wrote host memory there and Kickstart found RAM that does not exist.
- Resolved JIT faults are normal operation, not errors ("Too many access violations" after long play).
- Amiga address space mapped without `MAP_FIXED` (it could overwrite the program heap).
- Display drawn in its own thread (Ports version): 44 -> 50+ fps on the R36S.
- Pad as mouse, on-screen keyboard, hotkeys, visible GUI pointer, settings saved when leaving the
  GUI, CPU Idle saved in configs, RTG (Picasso96) Workbench by default.

## Layout

```
build/build_ports.sh    cross-build of the Ports version in WSL/Linux (Ubuntu gcc-9 AArch64 cross +
                        Ubuntu 19.10 arm64 sysroot); build_lr.sh builds the libretro core
build/patches/          mechanical patches applied to the unmodified Amiberry v3.3 source
                        (lr_patch.py: the core; whd_jit.py: WHDLoad machine of the Ports version)
build/package_*.sh      the core's install package and the release archive
native/                 new source: r36s_hook.cpp (pad, hotkeys, JIT fixes, Kickstart search,
                        remote test hooks), r36s_vkbd.cpp (on-screen keyboard), r36s_font35.h
native/libretro/        the core: lr_core.cpp (libretro API, options, machines), lr_sdlshim.cpp
                        (SDL -> libretro), lr_options.h, libretro.h (MIT)
port/                   the Ports launcher and default configuration; port/core: the core's
                        installer (EmulationStation integration) and .info file
baseline/bench/         Amiga-side tools: rtgmode, Dhrystone benchmark, sysclick
build/build_v221.sh     32-bit build of Amiberry v2.21, kept for the measurements
```

## Licence

GPL v3 (see [LICENSE](LICENSE)). Kickstart ROMs, AmigaOS / Workbench and games are not included.
