// TurboUAE libretro core: the core options. "auto" always means: leave what the loaded
// configuration (.uae), the chosen machine or the WHDLoad/CD database says.
#ifndef LR_OPTIONS_H
#define LR_OPTIONS_H
#include "libretro.h"

#define AUTO { "auto", "Auto (from machine / config)" }
#define ONOFF AUTO, { "on", "On" }, { "off", "Off" }
#define PAD_BUTTONS { "disabled", "Disabled" }, { "l2", "L2" }, { "r2", "R2" }, { "l1", "L1" }, { "r1", "R1" }, \
	{ "l3", "L3 (left stick click)" }, { "r3", "R3 (right stick click)" }, { "select", "Select" }, { "start", "Start" }, \
	{ "x", "X" }, { "y", "Y" }

#define KEYS { "none", "Pad function (no key)" }, { "space", "Space" }, { "return", "Return" }, { "esc", "Esc" }, { "up", "Cursor up" }, { "down", "Cursor down" }, \
	{ "left", "Cursor left" }, { "right", "Cursor right" }, { "f1", "F1" }, { "f2", "F2" }, { "f3", "F3" }, { "f4", "F4" }, \
	{ "f5", "F5" }, { "f6", "F6" }, { "f7", "F7" }, { "f8", "F8" }, { "f9", "F9" }, { "f10", "F10" }, \
	{ "lshift", "Left Shift" }, { "rshift", "Right Shift" }, { "ctrl", "Ctrl" }, { "lalt", "Left Alt" }, { "ralt", "Right Alt" }, { "lamiga", "Left Amiga" }, \
	{ "ramiga", "Right Amiga" }, { "help", "Help" }, { "del", "Del" }, { "bs", "Backspace" }, { "tab", "Tab" }, { "0", "0" }, \
	{ "1", "1" }, { "2", "2" }, { "3", "3" }, { "4", "4" }, { "5", "5" }, { "6", "6" }, \
	{ "7", "7" }, { "8", "8" }, { "9", "9" }, { "a", "A" }, { "b", "B" }, { "c", "C" }, \
	{ "d", "D" }, { "e", "E" }, { "f", "F" }, { "g", "G" }, { "h", "H" }, { "i", "I" }, \
	{ "j", "J" }, { "k", "K" }, { "l", "L" }, { "m", "M" }, { "n", "N" }, { "o", "O" }, \
	{ "p", "P" }, { "q", "Q" }, { "r", "R" }, { "s", "S" }, { "t", "T" }, { "u", "U" }, \
	{ "v", "V" }, { "w", "W" }, { "x", "X" }, { "y", "Y" }, { "z", "Z" }, { "kpenter", "Keypad Enter" }, \
	{ "kp0", "Keypad 0" }, { "kpplus", "Keypad +" }, { "kpminus", "Keypad -" }

static struct retro_core_option_v2_category lr_option_cats[] = {
	{ "system", "System", "Amiga model, CPU, JIT and memory." },
	{ "chipset", "Chipset", "Chipset type and accuracy / speed trade-offs." },
	{ "video", "Video", "Resolution and picture area of the emulated screen." },
	{ "audio", "Audio", "Paula sound output." },
	{ "media", "Media", "Floppy drives, WHDLoad and networking." },
	{ "input", "Input", "What the pad does and which buttons open the TurboUAE menu and keyboard." },
	{ "keys", "Pad buttons as keys", "Any pad button can press an Amiga key instead of (or, for the d-pad, as well as) its joystick function - in every pad mode, CD32 too, so fire + key combinations work. Apply at once." },
	{ nullptr, nullptr, nullptr }
};

static struct retro_core_option_v2_definition lr_option_defs[] = {
	// ---- system
	{ "turbouae_model", "Amiga model", nullptr,
	  "Machine used for floppy and hard disk images and when the core is started without content. A .uae file is always used as it is; WHDLoad (.lha) and CD images pick their machine themselves. 'Auto' = A500 for floppies (A1200 if the name says AGA), A1200 Turbo for hard disk images. Restart required.",
	  nullptr, "system",
	  { AUTO, { "A500", "A500 (68000, OCS, 512K + 512K)" }, { "A500PLUS", "A500+ (68000, ECS, 1 MB)" },
	    { "A600", "A600 (68000, ECS, 2 MB)" }, { "A1200", "A1200 (68020, AGA, 2 MB)" },
	    { "A1200T", "A1200 Turbo (68020 JIT, 64 MB Z3, RTG 16 MB)" }, { "A4000", "A4000 (68030/040, AGA)" },
	    { "CD32", "CD32" }, { nullptr, nullptr } }, "auto" },
	{ "turbouae_gui_start", "Open TurboUAE settings at start", nullptr,
	  "Shows the full TurboUAE settings screen (all Amiberry panels: drives, ROMs, RAM, display, custom controls, save states...) before the Amiga starts.",
	  nullptr, "system", { { "off", "Off" }, { "on", "On" }, { nullptr, nullptr } }, "off" },
	{ "turbouae_cpu", "CPU model", nullptr, "Restart required.", nullptr, "system",
	  { AUTO, { "68000", "68000" }, { "68010", "68010" }, { "68020", "68020" }, { "68030", "68030" }, { "68040", "68040" },
	    { nullptr, nullptr } }, "auto" },
	{ "turbouae_fpu", "FPU", nullptr, "Restart required.", nullptr, "system",
	  { AUTO, { "none", "None" }, { "68881", "68881" }, { "68882", "68882" }, { "internal", "CPU internal (68040)" },
	    { nullptr, nullptr } }, "auto" },
	{ "turbouae_jit", "JIT compiler", nullptr,
	  "Translates 68k code to native ARM64 code - many times faster than the interpreter. Needs a 68020 or better (it is selected automatically). Restart required.",
	  nullptr, "system", { ONOFF, { nullptr, nullptr } }, "auto" },
	{ "turbouae_jit_cache", "JIT cache size", nullptr, "Used when the JIT option above is 'On'.", nullptr, "system",
	  { { "8192", "8 MB" }, { "1024", "1 MB" }, { "2048", "2 MB" }, { "4096", "4 MB" }, { "16384", "16 MB" },
	    { nullptr, nullptr } }, "8192" },
	{ "turbouae_jit_fpu", "JIT FPU", nullptr, "Compile FPU instructions too.", nullptr, "system",
	  { ONOFF, { nullptr, nullptr } }, "auto" },
	{ "turbouae_cpu_speed", "CPU speed", nullptr,
	  "'Fastest' runs the CPU as fast as the host allows, 'Real' approximates the original machine.", nullptr, "system",
	  { AUTO, { "max", "Fastest possible" }, { "real", "Real (cycle approximate)" }, { nullptr, nullptr } }, "auto" },
	{ "turbouae_cpu_idle", "CPU idle", nullptr,
	  "With 'Fastest' speed: how much host time the CPU gives back to chipset and sound when the Amiga is waiting. Raise it if sound breaks up, lower it for more CPU power.",
	  nullptr, "system",
	  { AUTO, { "0", "Off" }, { "1", "1 (least)" }, { "2", "2" }, { "3", "3" }, { "4", "4" }, { "5", "5" }, { "6", "6" },
	    { "7", "7" }, { "8", "8" }, { "9", "9" }, { "10", "10 (most)" }, { nullptr, nullptr } }, "auto" },
	{ "turbouae_cpu_compatible", "More compatible CPU", nullptr, "Slower, needed by a few old programs. Not with JIT.",
	  nullptr, "system", { ONOFF, { nullptr, nullptr } }, "auto" },
	{ "turbouae_chipmem", "Chip RAM", nullptr, "Restart required.", nullptr, "system",
	  { AUTO, { "1", "512 KB" }, { "2", "1 MB" }, { "4", "2 MB" }, { "8", "4 MB" }, { "16", "8 MB" }, { nullptr, nullptr } }, "auto" },
	{ "turbouae_slowmem", "Slow RAM", nullptr, "Restart required.", nullptr, "system",
	  { AUTO, { "0", "None" }, { "2", "512 KB" }, { "4", "1 MB" }, { "6", "1.5 MB" }, { nullptr, nullptr } }, "auto" },
	{ "turbouae_fastmem", "Zorro II Fast RAM", nullptr, "Restart required.", nullptr, "system",
	  { AUTO, { "0", "None" }, { "1", "1 MB" }, { "2", "2 MB" }, { "4", "4 MB" }, { "8", "8 MB" }, { nullptr, nullptr } }, "auto" },
	{ "turbouae_z3mem", "Zorro III Fast RAM", nullptr, "Needs a 68020 or better with 32 bit addressing (set automatically). Restart required.",
	  nullptr, "system",
	  { AUTO, { "0", "None" }, { "16", "16 MB" }, { "32", "32 MB" }, { "64", "64 MB" }, { "128", "128 MB" }, { "256", "256 MB" },
	    { nullptr, nullptr } }, "auto" },
	{ "turbouae_rtgmem", "RTG graphics card (Picasso96)", nullptr,
	  "Memory of the uaegfx card. RTG screens are much cheaper to emulate than AGA ones. Restart required.", nullptr, "system",
	  { AUTO, { "0", "None" }, { "4", "4 MB" }, { "8", "8 MB" }, { "16", "16 MB" }, { "32", "32 MB" }, { nullptr, nullptr } }, "auto" },

	// ---- chipset
	{ "turbouae_chipset", "Chipset", nullptr, "Restart required.", nullptr, "chipset",
	  { AUTO, { "ocs", "OCS" }, { "ecs", "ECS" }, { "aga", "AGA" }, { nullptr, nullptr } }, "auto" },
	{ "turbouae_video_standard", "Video standard", nullptr, nullptr, nullptr, "chipset",
	  { AUTO, { "pal", "PAL (50 Hz)" }, { "ntsc", "NTSC (60 Hz)" }, { nullptr, nullptr } }, "auto" },
	{ "turbouae_collision", "Collision level", nullptr, "'Sprites and playfields' is needed by some games, 'None' is fastest.", nullptr, "chipset",
	  { AUTO, { "none", "None" }, { "sprites", "Sprites only" }, { "playfields", "Sprites and playfields" }, { "full", "Full" },
	    { nullptr, nullptr } }, "sprites" },
	{ "turbouae_immediate_blits", "Immediate blitter", nullptr, "Faster, a few games need it off.", nullptr, "chipset",
	  { ONOFF, { nullptr, nullptr } }, "on" },
	{ "turbouae_fast_copper", "Fast copper", nullptr, "Faster, less accurate.", nullptr, "chipset",
	  { ONOFF, { nullptr, nullptr } }, "on" },

	// ---- video
	{ "turbouae_color_depth", "Output colour depth", nullptr,
	  "16 bit is fastest. 32 bit shows AGA and 24 bit RTG screens without colour loss. Restart required.", nullptr, "video",
	  { { "16", "16 bit (RGB565)" }, { "32", "32 bit" }, { nullptr, nullptr } }, "16" },
	{ "turbouae_resolution", "Horizontal resolution", nullptr, nullptr, nullptr, "video",
	  { AUTO, { "lores", "Low (320)" }, { "hires", "High (640)" }, { "superhires", "Super high (1280)" }, { nullptr, nullptr } }, "auto" },
	{ "turbouae_linemode", "Line mode", nullptr, "'Double' is needed for interlaced screens, 'Single' is faster.", nullptr, "video",
	  { AUTO, { "none", "Single" }, { "double", "Double" }, { nullptr, nullptr } }, "auto" },
	{ "turbouae_height", "Visible lines", nullptr, "Height of the picture in Amiga lines (PAL has 256 + overscan, NTSC 200).", nullptr, "video",
	  { AUTO, { "200", "200" }, { "216", "216" }, { "240", "240" }, { "256", "256" }, { "262", "262" }, { "270", "270" },
	    { "284", "284" }, { nullptr, nullptr } }, "auto" },
	{ "turbouae_auto_height", "Auto height", nullptr, "Follows the height of what the program displays.", nullptr, "video",
	  { ONOFF, { nullptr, nullptr } }, "auto" },
	{ "turbouae_vcenter", "Vertical centring", nullptr, nullptr, nullptr, "video",
	  { AUTO, { "none", "Off" }, { "simple", "Simple" }, { "smart", "Smart" }, { nullptr, nullptr } }, "auto" },
	{ "turbouae_frameskip", "Frame skip", nullptr, nullptr, nullptr, "video",
	  { AUTO, { "1", "Off (every frame)" }, { "2", "Every second frame" }, { nullptr, nullptr } }, "auto" },
	{ "turbouae_leds", "Status line (LEDs, FPS)", nullptr, nullptr, nullptr, "video",
	  { ONOFF, { nullptr, nullptr } }, "auto" },

	// ---- audio
	{ "turbouae_sound", "Sound", nullptr, nullptr, nullptr, "audio",
	  { AUTO, { "normal", "On" }, { "interrupts", "Emulated, muted (faster)" }, { "none", "Off" }, { nullptr, nullptr } }, "auto" },
	{ "turbouae_stereo_sep", "Stereo separation", nullptr, nullptr, nullptr, "audio",
	  { AUTO, { "10", "100%" }, { "7", "70%" }, { "5", "50%" }, { "3", "30%" }, { "0", "Mono mix" }, { nullptr, nullptr } }, "auto" },
	{ "turbouae_interpolation", "Interpolation", nullptr, nullptr, nullptr, "audio",
	  { AUTO, { "none", "None (fastest)" }, { "anti", "Anti" }, { "sinc", "Sinc" }, { "rh", "RH" }, { "crux", "Crux" },
	    { nullptr, nullptr } }, "auto" },
	{ "turbouae_filter", "Audio filter", nullptr, "'Emulated' follows the power LED like a real Amiga.", nullptr, "audio",
	  { AUTO, { "off", "Always off" }, { "emulated", "Emulated" }, { "on", "Always on" }, { nullptr, nullptr } }, "auto" },
	{ "turbouae_floppy_sound", "Floppy drive sound", nullptr, nullptr, nullptr, "audio",
	  { AUTO, { "off", "Off" }, { "on", "On" }, { nullptr, nullptr } }, "auto" },

	// ---- media
	{ "turbouae_floppy_speed", "Floppy speed", nullptr, "Turbo loads much faster; a few copy-protected disks need 100%.", nullptr, "media",
	  { AUTO, { "100", "100% (real)" }, { "200", "200%" }, { "400", "400%" }, { "800", "800%" }, { "0", "Turbo" },
	    { nullptr, nullptr } }, "auto" },
	{ "turbouae_floppy_drives", "Floppy drives", nullptr, "Disks of a multi-disk game (.m3u) are put into the available drives. Restart required.", nullptr, "media",
	  { AUTO, { "1", "1" }, { "2", "2" }, { "3", "3" }, { "4", "4" }, { nullptr, nullptr } }, "auto" },
	{ "turbouae_whd_buttonwait", "WHDLoad: wait for button", nullptr, "Wait for fire while the WHDLoad game loads (to read its loading screens).", nullptr, "media",
	  { ONOFF, { nullptr, nullptr } }, "auto" },
	{ "turbouae_whd_splash", "WHDLoad: show splash", nullptr, nullptr, nullptr, "media",
	  { ONOFF, { nullptr, nullptr } }, "auto" },
	{ "turbouae_bsdsocket", "bsdsocket.library (network)", nullptr, "Restart required.", nullptr, "media",
	  { ONOFF, { nullptr, nullptr } }, "auto" },

	// ---- input
	{ "turbouae_pad_mode", "Pad 1 controls", nullptr,
	  "'Mouse + joystick': left stick = mouse, A/B = mouse buttons, X = double click, d-pad + A = joystick in port 2 (Workbench and most games). 'Joystick': d-pad and left stick = joystick, no mouse (action games). 'CD32 pad': as joystick, all buttons as a CD32 pad.",
	  nullptr, "input",
	  { { "mouse_joy", "Mouse + joystick" }, { "joystick", "Joystick" }, { "cd32", "CD32 pad" }, { nullptr, nullptr } }, "mouse_joy" },
	{ "turbouae_pad2", "Pad 2", nullptr, "A second pad as joystick in port 1 (replaces the mouse there). Restart required.", nullptr, "input",
	  { { "off", "Not used" }, { "joystick", "Joystick in port 1" }, { nullptr, nullptr } }, "off" },
	{ "turbouae_autofire", "Autofire (port 2)", nullptr, nullptr, nullptr, "input",
	  { AUTO, { "none", "Off" }, { "normal", "While fire is held" }, { "toggle", "Toggle" }, { nullptr, nullptr } }, "auto" },
	{ "turbouae_mouse_speed", "Stick mouse speed", nullptr,
	  "How fast the left stick moves the Amiga mouse pointer (and the pointer of the TurboUAE settings screen). Applies at once.", nullptr, "input",
	  { { "50", "50%" }, { "15", "15%" }, { "20", "20%" }, { "25", "25%" }, { "30", "30%" }, { "40", "40%" }, { "60", "60%" },
	    { "75", "75%" }, { "100", "100%" }, { "125", "125%" }, { "150", "150%" }, { "200", "200%" }, { "300", "300%" },
	    { nullptr, nullptr } }, "50" },
	{ "turbouae_btn_gui", "Button: TurboUAE settings", nullptr,
	  "Opens the full TurboUAE settings screen while the Amiga runs (d-pad moves, A activates, left stick moves a pointer, B clicks; changes of a .uae machine are saved on leaving).",
	  nullptr, "input", { PAD_BUTTONS, { nullptr, nullptr } }, "l2" },
	{ "turbouae_btn_vkbd", "Button: on-screen keyboard", nullptr,
	  "Shows the Amiga keyboard: d-pad chooses, A presses, B hides, X = Backspace, Y = Return. Shift, Ctrl, Alt and Amiga are sticky.",
	  nullptr, "input", { PAD_BUTTONS, { nullptr, nullptr } }, "r2" },

	// ---- pad buttons as Amiga keys
	{ "turbouae_dpad_keys", "D-pad as cursor keys", nullptr,
	  "'Cursor keys': the d-pad presses the Amiga's cursor keys instead of moving the joystick (menus that want the keyboard). 'Both': cursor keys and joystick at the same time.",
	  nullptr, "keys", { { "off", "Off (joystick)" }, { "cursor", "Cursor keys" }, { "both", "Cursor keys + joystick" }, { nullptr, nullptr } }, "off" },
	{ "turbouae_lstick_keys", "Left stick as cursor keys", nullptr,
	  "'Cursor keys': the left stick presses the Amiga's cursor keys and is then neither mouse nor joystick.",
	  nullptr, "keys", { { "off", "Off" }, { "cursor", "Cursor keys" }, { nullptr, nullptr } }, "off" },
	{ "turbouae_rstick_keys", "Right stick as cursor keys", nullptr,
	  "'Cursor keys': the right stick presses the Amiga's cursor keys.",
	  nullptr, "keys", { { "off", "Off" }, { "cursor", "Cursor keys" }, { nullptr, nullptr } }, "off" },
	{ "turbouae_key_a", "Key on A", nullptr, nullptr, nullptr, "keys",
	  { KEYS, { nullptr, nullptr } }, "none" },
	{ "turbouae_key_b", "Key on B", nullptr, nullptr, nullptr, "keys",
	  { KEYS, { nullptr, nullptr } }, "none" },
	{ "turbouae_key_x", "Key on X", nullptr, nullptr, nullptr, "keys",
	  { KEYS, { nullptr, nullptr } }, "none" },
	{ "turbouae_key_y", "Key on Y", nullptr, nullptr, nullptr, "keys",
	  { KEYS, { nullptr, nullptr } }, "none" },
	{ "turbouae_key_l1", "Key on L1", nullptr, nullptr, nullptr, "keys",
	  { KEYS, { nullptr, nullptr } }, "none" },
	{ "turbouae_key_r1", "Key on R1", nullptr, nullptr, nullptr, "keys",
	  { KEYS, { nullptr, nullptr } }, "none" },
	{ "turbouae_key_l2", "Key on L2", nullptr, "Ignored while this button opens the settings screen or the on-screen keyboard (Input).", nullptr, "keys",
	  { KEYS, { nullptr, nullptr } }, "none" },
	{ "turbouae_key_r2", "Key on R2", nullptr, "Ignored while this button opens the settings screen or the on-screen keyboard (Input).", nullptr, "keys",
	  { KEYS, { nullptr, nullptr } }, "none" },
	{ "turbouae_key_l3", "Key on L3 (left stick click)", nullptr, nullptr, nullptr, "keys",
	  { KEYS, { nullptr, nullptr } }, "none" },
	{ "turbouae_key_r3", "Key on R3 (right stick click)", nullptr, nullptr, nullptr, "keys",
	  { KEYS, { nullptr, nullptr } }, "none" },
	{ "turbouae_key_select", "Key on Select", nullptr, nullptr, nullptr, "keys",
	  { KEYS, { nullptr, nullptr } }, "none" },
	{ "turbouae_key_start", "Key on Start", nullptr, nullptr, nullptr, "keys",
	  { KEYS, { nullptr, nullptr } }, "none" },

	{ nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, { { nullptr, nullptr } }, nullptr }
};

static struct retro_core_options_v2 lr_options_v2 = { lr_option_cats, lr_option_defs };
#endif
