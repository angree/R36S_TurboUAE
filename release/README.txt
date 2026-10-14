TurboUAE 0.2.0 for R36S (ArkOS) - port by Grzegorz Korycki
============================================================

An Amiga emulator for the R36S handheld (and other RK3326 / ArkOS consoles) with a working
68k JIT for 64-bit ARM. Based on Amiberry v3.3 (BlitterStudio), built and fixed for this
console. Two ways to run it, both in this archive:

    Ports version      ports/TurboUAE.sh - its own program, started from the Ports menu
    RetroArch core     turbouae_libretro.so - started from EmulationStation like any other
                       core, with an emulator "turbouae" of its own in the Amiga systems

Home page and source: https://github.com/angree/R36S_TurboUAE

Measured on an R36S (Cortex-A35, 1.5 GHz), emulated 68020 with JIT: about 135 000 Dhrystones
per second (xSysInfo: ~78 MIPS, faster than an A4000/060) - 14x the interpreter.


NEW IN 0.2.0
------------
- RetroArch core (libretro): picture, sound and pad through RetroArch, settings as core
  options, the complete Amiberry settings screen and the on-screen keyboard built in.
- WHDLoad games (.lha) in both versions, with the JIT. Amiberry's WHDLoad machine (A1200)
  cannot boot with the JIT - A1200 chipset mode, Zorro II RAM and the A1200 Kickstarts each
  hang it - so WHDLoad games get the Workbench machine's setup instead: JIT, 64 MB Zorro III
  RAM, booted from the A4000 Kickstart 3.1 (without it: no JIT).
- Core: any pad button can press an Amiga key; d-pad, left or right stick as cursor keys
  (works in every pad mode, CD32 pad too, so fire + key combinations work).
- On-screen keyboard: clear arrow keys. Stick mouse: smoother, speed adjustable (core).
- Core: CPU, GPU and memory clock at full speed while a game runs (EmulationStation leaves
  them on "ondemand": 3D games crawled).


INSTALL
-------
Kickstart ROMs, Workbench and games are NOT included (copyrighted; Amiga Forever from Cloanto
is a legal source).

THE KICKSTART FOR THE JIT: Kickstart 3.1 for the A4000 (40.068). With the JIT only this one
boots - measured: 3.1 for the A1200, A3000, A500/A600 and 3.0 for the A1200 all hang at boot
(the JIT itself is fine, the boot is not; with the JIT off any Kickstart works). TurboUAE finds
it by its contents under any name, or under one of these names:
    kick31.rom   kick40068.A4000   amiga-os-310-a4000.rom   kick31_020.rom
    Kickstart v3.1 rev 40.68 (1993)(Commodore)(A4000).rom   (and with "40.068")
Many WHDLoad games also need the Kickstart images WHDLoad itself loads (kick34005.A500,
kick40068.A1200, ...); they are found by their contents, names do not matter.

Copy the "ports" folder of this archive onto the EASYROMS partition of the SD card. You get
ports/TurboUAE.sh, ports/turbouae/, ports/Install TurboUAE core.sh and ports/turbouae-core/.
Then choose one of the two ways - or use both, they do not get in each other's way.

A) PORTS VERSION (own program, no RetroArch)
   1. Put in place:
          ports/turbouae/kickstarts/   the A4000 Kickstart 3.1 (see above) and, for WHDLoad
                                       games, the Kickstarts WHDLoad needs
          ports/turbouae/hdf/workbench.hdf   a bootable Workbench 3.x hardfile
      Other names / paths: change them in the settings GUI (ROM and Hard drives/CD panels) or
      in ports/turbouae/conf/Workbench.uae, which the first start writes from the template.
   2. Restart EmulationStation. Ports -> TurboUAE starts the settings GUI with the Workbench
      machine loaded; Start (or "Start" in the GUI) runs it.
   3. WHDLoad games: Select + X (settings GUI) -> Quickstart -> WHDLoad, pick the .lha.
   Not needed: ports/Install TurboUAE core.sh and ports/turbouae-core/ may be deleted.

B) RETROARCH CORE (started from the game lists of EmulationStation)
   1. Put the Kickstarts into bios/ (the RetroArch system folder, where the other Amiga cores
      want them too) or bios/turbouae/kickstarts/. Any names.
   2. Ports -> "Install TurboUAE core". It copies the core into RetroArch, its data to
      bios/turbouae, adds the emulator "turbouae" to the Amiga and Amiga CD32 systems (next to
      the ones that are there - the default stays as it was; a backup of es_systems.cfg is
      kept as es_systems.cfg.before-turbouae) and restarts EmulationStation. Run it again after
      an ArkOS update. If the Ports version is installed, its Workbench machine appears in the
      Amiga list as "Workbench (TurboUAE)", already set to the turbouae emulator.
   3. Games go into roms/amiga (.lha, .adf, .hdf, .uae, ...) and roms/amigacd32 as usual.
   4. Starting a game with TurboUAE: in the Amiga game list highlight the game, press Select
      to open its options and set the emulator to "turbouae" (core "turbouae"). For all Amiga
      games at once set it as the Amiga system's emulator instead. Back to the old emulator:
      set it to "standalone" (stock Amiberry) or "retroarch" (PUAE, uae4arm) again.
   Afterwards ports/turbouae-core and the installer may be deleted (they are needed again
   only to reinstall). The core's WHDLoad booter is taken from ports/turbouae/ when the
   installer runs, so keep that folder until then.

UPDATING FROM 0.1.0: copy the "ports" folder over the old one - your conf/Workbench.uae, hdf/,
kickstarts/ and work/ stay as they are (0.2.0 no longer ships a ready Workbench.uae). 0.1.0
said "Kickstart 3.1 (A1200 or A4000)" for kick31.rom: if yours is the A1200 one, the JIT
machine will not boot - put the A4000 Kickstart 3.1 there instead.

The supplied Workbench machine (conf/Workbench.uae): A1200/AGA, 68020 + JIT, 2 MB chip RAM,
64 MB Zorro III fast RAM, 16 MB RTG (uaegfx / Picasso96) card. If your hardfile has
Picasso96, Work:run switches the Workbench to a 640x480 RTG screen on the first boot.


PORTS VERSION - CONTROLS
------------------------
In emulation (read straight from the built-in pad):
    left stick        Amiga mouse
    A / B             left / right mouse button
    X                 double click (open the icon under the pointer)
    d-pad + A         joystick in port 2 (fire = A; note A also clicks the left mouse button)
    Select + X        settings GUI
    Select + Y        on-screen keyboard (see below)
    Select + Start    quit to the Ports menu
Settings GUI:
    d-pad             move between controls          A        activate
    left stick        move the pointer               B        click
    Resume / Start    back to the Amiga - settings are SAVED automatically on leaving the GUI
WHDLoad games: settings GUI -> Quickstart -> WHDLoad, pick the .lha.
Pad buttons as Amiga keys: settings GUI -> Custom controls (saved with the configuration).


RETROARCH CORE
--------------
What it starts:
    .uae                         that configuration, exactly as written
    .adf .adz .dms .zip          floppy in DF0: on an A500 (an A1200 if the name contains "AGA")
    .hdf .hdz                    hard disk image as DH0: on the "A1200 Turbo" machine
    .lha                         WHDLoad game
    .cue .iso .chd .ccd .mds .nrg   CD32
    .m3u                         several floppies (DF0:-DF3: and RetroArch's disk control) or
                                 several hard disk images
"Game.uae" next to "Game.adf" / "Game.hdf" is read on top of the machine.

Controls (RetroArch's own hotkeys stay as they are, e.g. Select+X = RetroArch menu):
    L2                TurboUAE settings screen (again: back to the Amiga)
    R2                on-screen Amiga keyboard
    left stick        mouse (pad mode "Mouse + joystick"), A / B mouse buttons, X double click
    d-pad + A         joystick in port 2
Core options (Quick Menu -> Core Options), "Auto" = what the configuration / machine / WHDLoad
database says:
    System       model, CPU, FPU, JIT, JIT cache and FPU, CPU speed and idle, all memory, RTG
    Chipset      OCS/ECS/AGA, PAL/NTSC, collisions, immediate blitter, fast copper
    Video        16/32 bit output, resolution, line mode, height, centring, frame skip, LEDs
    Audio        sound, stereo separation, interpolation, filter, floppy sound
    Media        floppy speed and drives, WHDLoad button wait / splash, bsdsocket
    Input        pad mode (mouse + joystick / joystick / CD32 pad), pad 2, autofire, stick
                 mouse speed, the settings / keyboard buttons
    Pad buttons as keys   d-pad / left stick / right stick as cursor keys; an Amiga key on
                 any of A, B, X, Y, L1, R1, L2, R2, L3, R3, Select, Start
Per-game settings: Core Options -> Manage Core Options -> Save Game Options.
Not supported: RetroArch save states, rewind, run-ahead, fast-forward (use the Savestates
panel of the settings screen), .ipf floppies.


ON-SCREEN KEYBOARD
------------------
    d-pad             choose a key (hold to repeat)   A  press   B  hide
    X                 Backspace (shortcut)             Y  Return (shortcut)
    Shift, Ctrl, Alt and both Amiga keys ("A") are sticky: press one, it lights up and stays
    down until the next ordinary key - e.g. Shift then A types "A", left-Amiga then Q = Amiga-Q.
    Esc, F1-F10, Del, Help and the cursor keys (arrows, bottom right) are on it too.


TUNING
------
CPU Speed "Fastest" + JIT is the default. "CPU Idle" lets the CPU give time back to the
chipset and sound when the Amiga waits - raise it if sound breaks up, lower it for more CPU
power. 3D games (Breathless, Alien Breed 3D II) may run better with a 68040 + FPU and the JIT
FPU. RTG (Picasso96) screens are much cheaper to emulate than AGA ones.


KNOWN PROBLEMS
--------------
- With the JIT the Amiga boots only from the A4000 Kickstart 3.1, and A1200 chipset mode and
  Zorro II RAM each hang it at boot. WHDLoad games avoid them automatically; your own
  configurations should use them only with the JIT off.
- Sound can crackle in some menus (Xtreme Racing) and some games; a band of the 3D view can
  flicker every few seconds in Breathless.
- Core: "Quit" in the settings screen leaves you in RetroArch's menu.


WHAT WAS CHANGED AGAINST AMIBERRY v3.3
--------------------------------------
- JIT: Amiberry's access-fault handler was silently replaced by SDL's own handler on ArkOS -
  every probe of missing memory crashed the emulator. It is put back on top.
- JIT: unbacked parts of the Amiga address space are made inaccessible, as in WinUAE (the JIT
  read and wrote host memory there; Kickstart "found" RAM that does not exist).
- JIT: resolved access faults no longer count as errors ("Too many access violations").
- JIT in a shared library (the core): compiled code reaches the cycle counter at a fixed
  small distance from the register block; it is now placed there on purpose.
- Amiga memory mapped without MAP_FIXED (it overwrote the program heap 1 start in 8).
- Display drawn in its own thread in the Ports version (50+ fps instead of 44).
- WHDLoad: the machine described above, Kickstart 3.1 to boot from.
- Pad as mouse, on-screen keyboard, hotkeys, GUI pointer, GUI saves on exit, CPU Idle saved.
- Core: an SDL shim (source/native/libretro/lr_sdlshim.cpp) turns the emulator's window,
  sound and pad into RetroArch's frame buffer, audio and input; the emulator runs in its own
  thread next to RetroArch.
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
    libretro API header (libretro.h) - the RetroArch / libretro team - MIT licence
    WHDLoad booter files and game database (whdboot/) as shipped with Amiberry v3.3;
        WHDLoad itself - Bert Jahn and the WHDLoad team, freely distributable
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
    source/build/                          build scripts and the patches applied to it:
                                           build_ports.sh (Ports version), build_lr.sh (core),
                                           package_core.sh, package_release.sh
    source/native/                         new source files; native/libretro/ is the core
    source/port/core/                      the core's installer and .info file
    source/amiga-tools/                    the Amiga-side tools (rtgmode, dhrybench, ...)
Built with Ubuntu's gcc-9 AArch64 cross compiler against an Ubuntu 19.10 arm64 sysroot (see
the comments in build_ports.sh).

NO WARRANTY - see LICENSE.
