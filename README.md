# TurboUAE

An Amiga emulator with a working 68k **JIT for 64-bit ARM**, for the **R36S** handheld and other
RK3326 / ArkOS consoles. Runs from the Ports menu, no RetroArch needed. Port by Grzegorz Korycki.

TurboUAE is based on [Amiberry](https://github.com/BlitterStudio/amiberry) **v3.3** (the last release
on the lighter uae4arm-derived core that already has the AArch64 JIT), with fixes that make the JIT
usable on this console and additions for a handheld without keyboard and mouse.

**Download:** see [Releases](https://github.com/angree/R36S_TurboUAE/releases) - installation,
controls and credits are in the `README.txt` inside the archive (also in [release/README.txt](release/README.txt)).

## Measured on an R36S (Cortex-A35, 1.5 GHz), emulated 68020

| | Dhrystone 2.1 / s |
|---|---|
| interpreter | ~9 400 |
| JIT | ~135 000 (xSysInfo: ~78 MIPS) |

## What was changed against Amiberry v3.3

- **JIT fault handler restored.** SDL on ArkOS replaced Amiberry's SIGSEGV handler, so every JIT
  access the handler should have redone through the memory banks crashed the emulator (SysInfo,
  xSysInfo, memory-probing software). The handler is put back on top.
- **JIT memory holes.** Unbacked parts of the 24-bit address space are made inaccessible, as WinUAE
  does; before, the JIT read and wrote host memory there and Kickstart found RAM that does not exist.
- Resolved JIT faults are normal operation, not errors ("Too many access violations" after long play).
- Amiga address space mapped without `MAP_FIXED` (it could overwrite the program heap).
- Display drawn in its own thread: 44 -> 50+ fps on the R36S.
- Pad as mouse, on-screen keyboard (Select+Y), hotkeys, visible GUI pointer, settings saved when
  leaving the GUI, CPU Idle saved in configs, RTG (Picasso96) Workbench by default.

## Layout

```
build/build_v33.sh      cross-build in WSL/Linux (Ubuntu gcc-9 AArch64 cross + Ubuntu 19.10 arm64 sysroot)
build/patches/          mechanical patches applied to the unmodified Amiberry v3.3 source
native/                 new source: r36s_hook.cpp (pad, hotkeys, JIT fixes, remote test hooks),
                        r36s_vkbd.cpp (on-screen keyboard), r36s_font35.h (Foxel35, CC0)
port/                   the Ports launcher and default configuration
baseline/bench/         Amiga-side tools: rtgmode, Dhrystone benchmark, sysclick
build/build_v221.sh     32-bit build of Amiberry v2.21, kept for the measurements
```

## Licence

GPL v3 (see [LICENSE](LICENSE)). Kickstart ROMs, AmigaOS / Workbench and games are not included.
