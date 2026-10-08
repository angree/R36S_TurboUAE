TurboUAE 0.1.0 for R36S (ArkOS) - port by Grzegorz Korycki
============================================================

An Amiga emulator for the R36S handheld (and other RK3326 / ArkOS consoles) with a working
68k JIT for 64-bit ARM. Based on Amiberry v3.3 (BlitterStudio), built and fixed for this
console. Runs from the Ports menu, no RetroArch needed.

Home page and source: https://github.com/angree/R36S_TurboUAE

Measured on an R36S (Cortex-A35, 1.5 GHz), emulated 68020 with JIT: about 135 000 Dhrystones
per second (xSysInfo: ~78 MIPS, faster than an A4000/060) - 14x the interpreter.


INSTALL
-------
1. Copy the "ports" folder of this archive onto the EASYROMS partition of the SD card, so
   that you get ports/TurboUAE.sh and ports/turbouae/.
2. Put your own Kickstart ROM and Workbench hardfile in place (NOT included - they are
   copyrighted; Amiga Forever from Cloanto is a legal source):
       ports/turbouae/kickstarts/kick31.rom      Kickstart 3.1 (A1200 or A4000)
       ports/turbouae/hdf/workbench.hdf          bootable Workbench 3.x hardfile
   Other names/paths: change them in the GUI (ROM and Hard drives/CD panels) or edit
   ports/turbouae/conf/Workbench.uae (kickstart_rom_file= and hardfile2= lines).
3. Restart EmulationStation; TurboUAE appears in Ports.

The supplied machine (conf/Workbench.uae): A1200/AGA, 68020 + JIT, 2 MB chip RAM, 64 MB
Zorro III fast RAM, 16 MB RTG (uaegfx / Picasso96) card. If your hardfile has Picasso96,
Work:run switches the Workbench to a 640x480 RTG screen on the first boot.


CONTROLS
--------
In emulation (read straight from the built-in pad):
    left stick        Amiga mouse
    A                 left mouse button
    B                 right mouse button
    X                 double click (open the icon under the pointer)
    d-pad + A         joystick in port 2 (fire = A; note A also clicks the left mouse button)
    Select + X        settings GUI
    Select + Y        on-screen keyboard (see below)
    Select + Start    quit to the Ports menu

Settings GUI:
    d-pad             move between controls          A        activate
    left stick        move the pointer               B        click
    Resume / Start    back to the Amiga - settings are SAVED automatically on leaving the GUI

On-screen keyboard (Select + Y):
    d-pad             choose a key (hold to repeat)
    A                 press the highlighted key
    B                 hide the keyboard
    X                 Backspace (shortcut)
    Y                 Return (shortcut)
    Shift, Ctrl, Alt and both Amiga keys ("A") are sticky: press one, it lights up and stays
    down until the next ordinary key - e.g. Shift then A types "A", left-Amiga then Q = Amiga-Q.
    Del, Help, F1-F10, Esc and the cursor keys are on the keyboard too.


TUNING
------
CPU and FPU panel:
    CPU Speed "Fastest" + JIT is the default. "CPU Idle" lets the CPU give time back to the
    chipset and sound when the Amiga waits - raise it if sound breaks up, lower it for more
    CPU power. It is saved with the configuration.
RTG (Picasso96) screens are much cheaper to emulate than AGA ones; software that can use an
RTG screen runs considerably faster.


WHAT WAS CHANGED AGAINST AMIBERRY v3.3
--------------------------------------
- JIT: Amiberry's access-fault handler (which lets the JIT survive programs probing for
  memory or hardware) was silently replaced by SDL's own handler on ArkOS - every such
  access crashed the emulator. It is put back on top.
- JIT: unbacked parts of the Amiga address space are now made inaccessible, as in WinUAE.
  Before, the JIT read and wrote host memory there and Kickstart "found" RAM that does not
  exist (crashes in SysInfo, xSysInfo, 0 Dhrystones).
- JIT: resolved access faults no longer count as errors ("Too many access violations").
- Amiga memory mapped without MAP_FIXED (it overwrote the program heap 1 start in 8).
- Display drawn in its own thread (50+ fps instead of 44 on the R36S).
- Pad as mouse, on-screen keyboard, hotkeys, GUI pointer, GUI saves on exit, CPU Idle saved.
Full details are in source/build/patches and source/native.


CREDITS AND LICENCES
--------------------
TurboUAE is free software under the GNU General Public License version 3 (see LICENSE).
It is a modified version of Amiberry; modifications by Grzegorz Korycki, 2026-10.

    Amiberry v3.3 (2020-09-17) - Dimitris Panokostas (MiDWaN) and contributors, BlitterStudio,
        https://github.com/BlitterStudio/amiberry - GPL v3
    UAE / WinUAE - Bernd Schmidt, Toni Wilen and many others - GPL v2 or later
    uae4arm and the ARM / AArch64 JIT - TomB; Chips (original Raspberry Pi port)
    68k JIT compiler - Bernd Meyer; Gwenole Beauchesne (Basilisk II); ARAnyM team - GPL v2+
    guisan GUI library - based on Guichan by Olof Naessen and Per Larsson - BSD licence
    Topaz font (data/AmigaTopaz.ttf) as shipped with Amiberry
    Foxel35 3x5 pixel font - "Fox", OpenGameArt, CC0 1.0 (https://opengameart.org/content/foxel35)
    SDL2, SDL2_image, SDL2_ttf (zlib licence), libpng, zlib, libxml2, FLAC, mpg123, libmpeg2
        - the console's system libraries, used under their own licences

Included Amiga software (in ports/turbouae/work):
    rtgmode - part of TurboUAE (source in source/amiga-tools), GPL v3
Recommended, not included (third-party): xSysInfo (https://github.com/reinauer/xSysInfo) - put
xSysInfo in ports/turbouae/work and identify.library in ports/turbouae/work/Libs; Work:run
already assigns that folder to LIBS:.

NOT included and NOT covered by these licences: Kickstart ROMs, AmigaOS / Workbench, games.

SOURCE CODE (GPL v3, section 6)
    source/amiberry-v3.3-upstream.tar.gz   the unmodified Amiberry v3.3 source
    source/build/                          build scripts (build_v33.sh) and patches applied to it
    source/native/                         new source files (r36s_hook.cpp, r36s_vkbd.cpp, ...)
    source/amiga-tools/                    the Amiga-side tools (rtgmode, dhrybench, ...)
The binary is built with build/build_v33.sh (Ubuntu gcc-9 AArch64 cross compiler against an
Ubuntu 19.10 arm64 sysroot - see the comments in the script).

NO WARRANTY - see LICENSE.
