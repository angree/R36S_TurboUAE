# Turns the (already TurboUAE-patched) Amiberry v3.3 tree into the libretro core's source:
# copies native/libretro/* into src/osdep and makes the few source changes the SDL shim cannot
# do from outside. Everything else (window, renderer, audio, pad, events) is redirected by the
# force-included lr_sdlshim.h.   Usage: lr_patch.py <amiberry source dir>
import os, re, shutil, sys
d = sys.argv[1]
here = os.path.dirname(os.path.abspath(__file__))
nat = os.path.join(here, '..', '..', 'native', 'libretro')
for f in os.listdir(nat):
    shutil.copy(os.path.join(nat, f), d + '/src/osdep/' + f)

def patch(rel, fn):
    p = d + '/' + rel
    s = open(p, encoding='utf-8', errors='surrogateescape').read()
    t = fn(s)
    if t != s:
        open(p, 'w', encoding='utf-8', errors='surrogateescape', newline='\n').write(t)

def once(s, old, new, what):
    assert old in s, 'anchor not found: ' + what
    return s.replace(old, new, 1)

# --- Makefile: our objects, extra link flags, the shim for every C++ file (guisan too)
def mk(s):
    if 'lr_core.o' in s: return s
    s = once(s, '\tsrc/osdep/r36s_hook.o \\\n', '\tsrc/osdep/r36s_hook.o \\\n\tsrc/osdep/lr_core.o \\\n\tsrc/osdep/lr_sdlshim.o \\\n', 'Makefile objs')
    s = once(s, 'export CXXFLAGS += $(CFLAGS) -std=gnu++14', 'export CXXFLAGS += $(CFLAGS) -std=gnu++14 $(LR_CXXFLAGS)\nLDFLAGS += $(EXTRA_LDFLAGS)', 'Makefile flags')
    return s
patch('Makefile', mk)

# --- the emulator's main() becomes a function run in the core's emulator thread
def amiberry(s):
    if 'lr_emu_main' in s: return s
    s = once(s, 'int main(int argc, char* argv[])\n{', 'void lr_get_base(char* out, int size);\nvoid lr_after_settings();\nint lr_emu_main(int argc, char* argv[])\n{', 'main')
    s = once(s, '\tgetcwd(start_path_data, MAX_DPATH);', '\tlr_get_base(start_path_data, MAX_DPATH);', 'getcwd')
    s = once(s, '\tload_amiberry_settings();\n\trp9_init();', '\tload_amiberry_settings();\n\tlr_after_settings();\n\trp9_init();', 'settings')
    # SIGTERM belongs to the frontend
    s = once(s, '\tif (sigaction(SIGTERM, &action, nullptr) < 0)', '\tif (0)', 'sigterm')
    # no "sudo poweroff" from inside a frontend
    s = once(s, '\tif (host_poweroff)\n\t\ttarget_shutdown();', '\t(void)host_poweroff;', 'poweroff')
    return s
patch('src/osdep/amiberry.cpp', amiberry)

# --- Kickstart ROMs are found by content in the frontend's system directory as well
def gui(s):
    if 'lr_scan_rom_file' in s: return s
    s = once(s, 'void RescanROMs()\n{', '''void lr_scan_extra_roms();
static int lr_scan_cb(struct zfile* f, void* dummy)
{
	auto* const rd = scan_single_rom_2(f);
	if (rd)
		addrom(rd, zfile_getname(f));
	return 0;
}
void lr_scan_rom_file(const char* path)
{
	zfile_zopen(path, lr_scan_cb, 0);
}

void RescanROMs()
{''', 'RescanROMs')
    s = once(s, '\t\tscan_rom(tmppath);\n\t}\n', '\t\tscan_rom(tmppath);\n\t}\n\tlr_scan_extra_roms();\n', 'scan loop')
    return s
patch('src/osdep/amiberry_gui.cpp', gui)

# --- machine setup from content + core options
def main(s):
    if 'lr_configure' in s: return s
    s = once(s, '\tfixup_prefs(&currprefs, false);\n\n\tparse_cmdline(argc, argv);\n}', '\tfixup_prefs(&currprefs, false);\n\n\tparse_cmdline(argc, argv);\n\tlr_configure(&currprefs);\n}', 'parse_cmdline_and_init_file')
    s = once(s, 'static void parse_cmdline_and_init_file(int argc, TCHAR** argv)\n{', 'void lr_configure(struct uae_prefs* p);\nstatic void parse_cmdline_and_init_file(int argc, TCHAR** argv)\n{', 'decl')
    return s
patch('src/main.cpp', main)

# --- data files live in <system>/turbouae, the process' working directory is the frontend's
def filesys(s):
    if 'lr_base_path' in s: return s
    s = once(s, 'string prefix_with_application_directory_path(string currentpath)\n{', 'std::string lr_base_path(const std::string& rel);\nstring prefix_with_application_directory_path(string currentpath)\n{\n\treturn lr_base_path(currentpath);', 'prefix')
    return s
patch('src/osdep/amiberry_filesys.cpp', filesys)

def ingame(s):
    if 'lr_base_path' in s: return s
    s = once(s, 'new gcn::SDLTrueTypeFont("data/AmigaTopaz.ttf", 15)', 'new gcn::SDLTrueTypeFont(lr_base_path("data/AmigaTopaz.ttf"), 15)', 'ingame font')
    i = s.index('#include')
    return s[:i] + '#include <string>\nstd::string lr_base_path(const std::string& rel);\n' + s[i:]
patch('src/osdep/gui/InGameMessage.cpp', ingame)

def click(s):
    if 'lr_base_path' in s: return s
    s, n = re.subn(r'strdup\("(data/floppy_sounds/[^"]+)"\)', r'strdup(lr_base_path("\1").c_str())', s)
    assert n == 5, n
    i = s.index('#include')
    return s[:i] + '#include <string>\nstd::string lr_base_path(const std::string& rel);\n' + s[i:]
patch('src/driveclick.cpp', click)

# --- the Amiga screen is drawn straight in the frontend's pixel format (no conversion pass)
def gfx(s):
    if 'lr_video_sdl_format' in s: return s
    old = '\t\t//display_depth = 16;\n\t\t//pixel_format = SDL_PIXELFORMAT_RGB565;\n\t\tdisplay_depth = 32;\n\t\tpixel_format = SDL_PIXELFORMAT_RGBA32;\n'
    return once(s, old, '\t\tdisplay_depth = lr_video_bpp() * 8;\n\t\tpixel_format = lr_video_sdl_format();\n', 'native pixel format')
patch('src/osdep/amiberry_gfx.cpp', gfx)

# --- sound is pushed into the shim's ring instead of pulled by an SDL audio thread
patch('src/sounddep/sound.cpp', lambda s: s.replace('static int sound_pull = 1;', 'static int sound_pull = 0;'))

# --- position independent data access in the NEON helpers (a shared object may not have
#     absolute addresses in its code)
def helper(s):
    if ':got:' in s or ':lo12:' in s: return s
    labels = set(re.findall(r'^([A-Za-z_][A-Za-z0-9_]*):', s, re.M))
    def fix(m):
        ind, reg, sym = m.group(1), m.group(2), m.group(3)
        if sym in labels:
            return '%sadrp      %s, %s\n%sadd       %s, %s, :lo12:%s' % (ind, reg, sym, ind, reg, reg, sym)
        return '%sadrp      %s, :got:%s\n%sldr       %s, [%s, #:got_lo12:%s]' % (ind, reg, sym, ind, reg, reg, sym)
    s, n = re.subn(r'^([ \t]*)ldr[ \t]+(x[0-9]+),[ \t]*=([A-Za-z_][A-Za-z0-9_]*)[ \t]*$', fix, s, flags=re.M)
    assert n == 10, n
    return s
patch('src/osdep/aarch64_helper.s', helper)

# --- JIT: compiled code reaches the cycle counter ("pissoff") as regs + small offset in a
#     single load instruction (12 bit scaled immediate). That only holds while the linker
#     happens to put the two variables next to each other, which it does in the non-PIC
#     executable (0x1c8 apart) and does not in the -fPIC shared object (0xe0de0 apart): the
#     blocks then count down some unrelated memory, never yield, and the Amiga hangs in the
#     first wait loop of Kickstart. Both are now defined together, a fixed 0x400 apart.
def newcpu(s):
    if 'lr_regs_block' in s: return s
    s = once(s, 'uae_s32 pissoff = 0;\n', '', 'pissoff def')
    s = once(s, '\nstruct regstruct regs;\n', '''
// lr_regs_block: regs and pissoff at a fixed distance (see build/patches/lr_patch.py)
static_assert(sizeof(struct regstruct) <= 0x400, "regstruct grew, enlarge the block below");
__asm__(".pushsection .bss\\n.balign 16\\n"
	".globl regs\\n.type regs, %object\\n.size regs, 0x400\\nregs:\\n.space 0x400\\n"
	".globl pissoff\\n.type pissoff, %object\\n.size pissoff, 16\\npissoff:\\n.space 16\\n"
	".popsection\\n");
''', 'regs def')
    return s
patch('src/newcpu.cpp', newcpu)

# --- WHDLoad wants the Kickstart files under fixed names in whdboot/save-data/Kickstarts.
#     Amiberry makes symbolic links there, which the FAT/exFAT roms partition cannot hold:
#     fall back to a copy, and keep a copy that is already there.
def whd(s):
    if 'lr_copy_file' in s: return s
    s = once(s, '\tmy_unlink(kick_long);\n', '\tif (!lr_is_regular_file(kick_long))\n\t\tmy_unlink(kick_long);\n', 'unlink')
    s = once(s, '\t\t\tsymlink(p->romfile, kick_long);', '\t\t\tif (symlink(p->romfile, kick_long) != 0)\n\t\t\t\tlr_copy_file(p->romfile, kick_long);', 'symlink')
    s = once(s, 'void make_rom_symlink(', 'bool lr_is_regular_file(const char* path);\nbool lr_copy_file(const char* from, const char* to);\nvoid make_rom_symlink(', 'decl')
    return s
patch('src/osdep/amiberry_whdbooter.cpp', whd)

patch('src/osdep/target.h', lambda s: s.replace('"TurboUAE 0.1.0 - based on', '"TurboUAE 0.2.0 (libretro) - based on'))
print('libretro patches applied to', d)
