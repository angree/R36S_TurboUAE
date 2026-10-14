// TurboUAE libretro core: the libretro API on top of the emulator.
//
// The emulator (Amiberry v3.3 with the TurboUAE fixes) is not built to return after every
// frame, and its "fastest possible" CPU mode paces itself against the wall clock. So it runs
// unchanged in its own thread; lr_sdlshim.cpp turns its SDL calls into a frame buffer, an
// audio ring and virtual pads, and retro_run() moves those between the frontend and the
// emulator: it waits for the next finished frame, hands it to the frontend with the audio
// produced meanwhile, and writes the RetroPad state for the emulator to read. The emulator
// is never more than one frame ahead; when the frontend stops calling retro_run() (menu,
// pause) it blocks in "present" and is thereby paused.
//
// Machine setup, in this order:
//   1. content: .uae = that configuration; .lha = WHDLoad (Amiberry's booter and database);
//      CD image = CD32; floppy / hard disk image or .m3u list = the machine of the "Amiga
//      model" option; no content = <base>/conf/default.uae, the Ports version's
//      Workbench.uae, or the settings screen
//   2. core options that are not "Auto"
// <base> is <system dir>/turbouae (data files, WHDLoad files, configurations, logs).
// Kickstart ROMs are looked for by content, not by name, in <base>/kickstarts, the system
// directory and the Ports version's kickstarts folder; without one the built-in AROS ROM
// is used.
#include "libretro.h"
#include "lr_options.h"

#include <pthread.h>
#include <signal.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "sysdeps.h"
#include "options.h"
#include "uae.h"
#include "inputdevice.h"
#include "disk.h"
#include "rommgr.h"
#include "custom.h"
#include "memory.h"
#include "newcpu.h"
#include "filesys.h"
#include "gui.h"
#include "target.h"

#ifndef TURBOUAE_VERSION
#define TURBOUAE_VERSION "0.2.0"
#endif

extern int lr_emu_main(int argc, char* argv[]);          // the emulator's main() (patched name)
extern void lr_scan_rom_file(const char* path);          // amiberry_gui.cpp (patched in)
extern bool gui_running;
extern char last_loaded_config[];

// read by r36s_hook.cpp
int lr_pad_mode;                 // 0 mouse + joystick, 1 joystick, 2 CD32 pad
int lr_mouse_speed = 50;         // percent
int lr_btn_gui = LR_L2, lr_btn_vkbd = LR_R2;   // pad buttons (LR_*), -1 = none
int lr_key_map[LR_NBUTTONS];    // pad 1 button -> Amiga key (SDL scancode), 0 = pad function
int lr_dpad_keys;               // d-pad: 0 joystick, 1 cursor keys, 2 both
int lr_lstick_keys, lr_rstick_keys;   // the sticks as cursor keys

static retro_environment_t env_cb;
static retro_video_refresh_t video_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_audio_sample_t audio_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;
static retro_log_printf_t log_cb;

static char sys_dir[1024], base_dir[1024];
static std::string content_path, content_ext, loaded_uae;
static std::vector<std::string> disks;          // floppy images of this content, in order
static unsigned disk_index;
static bool disk_ejected;
bool r36s_jit_kickstart(struct uae_prefs* p, const char* const* dirs, int ndirs);   // r36s_hook.cpp
static bool want_gui_at_start;
static bool content_whdload;     // the WHDLoad booter built the machine
static bool have_bitmasks, can_dupe = true;

static pthread_t emu_tid;
static volatile int emu_started, emu_done;
static struct sigaction old_segv, old_ill, old_bus;

static int geom_w, geom_h;
static float geom_aspect, reported_fps = 50.0f;
static const void* last_pixels;
static int last_w, last_h, last_pitch;
static Uint32 blank_frame[320 * 240];
static const void* f_last_seen;
static unsigned audio_total;
static int au_max_gap, au_max_block, au_restarts;
unsigned lr_present_count();

static void lrlog(const char* fmt, ...)
{
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	if (log_cb) log_cb(RETRO_LOG_INFO, "[TurboUAE] %s", buf);
	else fprintf(stderr, "[TurboUAE] %s", buf);
}

static bool is_dir(const char* p) { struct stat st; return stat(p, &st) == 0 && S_ISDIR(st.st_mode); }
static bool is_file(const char* p) { struct stat st; return stat(p, &st) == 0 && S_ISREG(st.st_mode); }
static std::string lower_ext(const std::string& path)
{
	const size_t dot = path.rfind('.');
	if (dot == std::string::npos || path.find('/', dot) != std::string::npos) return "";
	std::string e = path.substr(dot + 1);
	for (auto& c : e) c = (char)tolower((unsigned char)c);
	return e;
}

// ------------------------------------------------------------------------------------
// core options: read on the frontend thread into a snapshot, applied on the emulator thread
// ------------------------------------------------------------------------------------
#define MAXOPT 64
static char optv[MAXOPT][48];
static pthread_mutex_t omx = PTHREAD_MUTEX_INITIALIZER;
static volatile int opt_dirty;

static void read_options()
{
	pthread_mutex_lock(&omx);
	for (int i = 0; i < MAXOPT && lr_option_defs[i].key; i++) {
		struct retro_variable var = { lr_option_defs[i].key, nullptr };
		const char* v = lr_option_defs[i].default_value;
		if (env_cb && env_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) v = var.value;
		snprintf(optv[i], sizeof optv[i], "%s", v);
	}
	pthread_mutex_unlock(&omx);
}

static std::string opt(const char* key)
{
	std::string r = "auto";
	pthread_mutex_lock(&omx);
	for (int i = 0; i < MAXOPT && lr_option_defs[i].key; i++)
		if (!strcmp(lr_option_defs[i].key, key)) { r = optv[i][0] ? optv[i] : lr_option_defs[i].default_value; break; }
	pthread_mutex_unlock(&omx);
	return r;
}

static void cfg(struct uae_prefs* p, const char* fmt, ...)
{
	char line[MAX_DPATH + 256];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(line, sizeof line, fmt, ap);
	va_end(ap);
	cfgfile_parse_line(p, line, 0);
}

// value of a "Key on ..." option -> SDL scancode (0 = the button keeps its pad function)
static int key_scancode(const std::string& n)
{
	static const struct { const char* n; SDL_Scancode sc; } t[] = {
		{ "space", SDL_SCANCODE_SPACE }, { "return", SDL_SCANCODE_RETURN }, { "esc", SDL_SCANCODE_ESCAPE },
		{ "up", SDL_SCANCODE_UP }, { "down", SDL_SCANCODE_DOWN }, { "left", SDL_SCANCODE_LEFT }, { "right", SDL_SCANCODE_RIGHT },
		{ "lshift", SDL_SCANCODE_LSHIFT }, { "rshift", SDL_SCANCODE_RSHIFT }, { "ctrl", SDL_SCANCODE_LCTRL },
		{ "lalt", SDL_SCANCODE_LALT }, { "ralt", SDL_SCANCODE_RALT }, { "lamiga", SDL_SCANCODE_LGUI }, { "ramiga", SDL_SCANCODE_RGUI },
		{ "help", SDL_SCANCODE_END }, { "del", SDL_SCANCODE_DELETE }, { "bs", SDL_SCANCODE_BACKSPACE }, { "tab", SDL_SCANCODE_TAB },
		{ "kpenter", SDL_SCANCODE_KP_ENTER }, { "kp0", SDL_SCANCODE_KP_0 }, { "kpplus", SDL_SCANCODE_KP_PLUS },
		{ "kpminus", SDL_SCANCODE_KP_MINUS } };
	for (const auto& e : t) if (n == e.n) return e.sc;
	if (n.size() == 1 && n[0] >= 'a' && n[0] <= 'z') return SDL_SCANCODE_A + (n[0] - 'a');
	if (n.size() == 1 && n[0] == '0') return SDL_SCANCODE_0;
	if (n.size() == 1 && n[0] >= '1' && n[0] <= '9') return SDL_SCANCODE_1 + (n[0] - '1');
	if (n.size() >= 2 && n[0] == 'f' && n[1] >= '1' && n[1] <= '9') {
		const int f = atoi(n.c_str() + 1);
		if (f >= 1 && f <= 10) return SDL_SCANCODE_F1 + (f - 1);
	}
	return 0;
}

static int button_from_name(const std::string& n)
{
	static const struct { const char* n; int b; } t[] = { { "l2", LR_L2 }, { "r2", LR_R2 }, { "l1", LR_L1 }, { "r1", LR_R1 },
		{ "l3", LR_L3 }, { "r3", LR_R3 }, { "select", LR_SELECT }, { "start", LR_START }, { "x", LR_X }, { "y", LR_Y } };
	for (const auto& e : t) if (n == e.n) return e.b;
	return -1;
}

// startup = before the machine is built (everything may change); otherwise only what the
// emulator can take over while running
static void apply_options(struct uae_prefs* p, bool startup)
{
	std::string v;
#define OPT(k) ((v = opt(k)) != "auto")
#define YES (v == "on" ? "true" : "false")
	if (startup) {
		if (OPT("turbouae_cpu")) {
			cfg(p, "cpu_model=%s", v.c_str());
			p->address_space_24 = atoi(v.c_str()) < 68030;
			if (atoi(v.c_str()) < 68020) p->cachesize = 0;
		}
		if (OPT("turbouae_fpu")) {
			if (v == "none") p->fpu_model = 0;
			else if (v == "internal") p->fpu_model = p->cpu_model >= 68040 ? p->cpu_model : 68882;
			else p->fpu_model = atoi(v.c_str());
		}
		if (OPT("turbouae_jit")) {
			if (v == "on") cfg(p, "cachesize=%s", opt("turbouae_jit_cache").c_str());
			else p->cachesize = 0;
		}
		if (OPT("turbouae_chipmem")) cfg(p, "chipmem_size=%s", v.c_str());
		if (OPT("turbouae_slowmem")) cfg(p, "bogomem_size=%s", v.c_str());
		if (OPT("turbouae_fastmem")) cfg(p, "fastmem_size=%s", v.c_str());
		if (OPT("turbouae_z3mem")) cfg(p, "z3mem_size=%s", v.c_str());
		// WHDLoad games never use a graphics card, and its Zorro III board only gets in the way
		if (!content_whdload && OPT("turbouae_rtgmem")) {
			cfg(p, "gfxcard_size=%s", v.c_str());
			if (v != "0") cfg(p, "gfxcard_type=ZorroIII");
		}
		if (OPT("turbouae_chipset"))
			p->chipset_mask = v == "aga" ? (CSMASK_ECS_AGNUS | CSMASK_ECS_DENISE | CSMASK_AGA) : (v == "ecs" ? (CSMASK_ECS_AGNUS | CSMASK_ECS_DENISE) : 0);
		if (OPT("turbouae_floppy_drives")) {
			const int n = atoi(v.c_str());
			for (int i = 0; i < 4; i++) p->floppyslots[i].dfxtype = i < n ? DRV_35_DD : DRV_NONE;
			p->nr_floppies = n;
		}
		if (OPT("turbouae_bsdsocket")) cfg(p, "bsdsocket_emu=%s", YES);
		if (opt("turbouae_pad2") == "joystick") p->jports[0].id = JSEM_JOYS + 1;
		// what the choices above need to work at all
		if (p->cachesize > 0 && p->cpu_model < 68020) p->cpu_model = 68020;
		if (p->z3fastmem[0].size || p->rtgboards[0].rtgmem_size || p->cachesize > 0) {
			if (p->cpu_model < 68020) p->cpu_model = 68020;
			p->address_space_24 = false;
		}
		if (p->cachesize > 0) {     // or the emulator's own checks switch the JIT off again
			p->cpu_compatible = false;
			p->cpu_cycle_exact = p->cpu_memory_cycle_exact = p->blitter_cycle_exact = false;
		}
	}
	if (OPT("turbouae_jit_fpu")) cfg(p, "compfpu=%s", YES);
	if (OPT("turbouae_cpu_speed")) cfg(p, "cpu_speed=%s", v.c_str());
	if (OPT("turbouae_cpu_idle")) {
		const int n = atoi(v.c_str());
		p->cpu_idle = n <= 0 ? 0 : (12 - n) * 15;
		if (!startup) currprefs.cpu_idle = p->cpu_idle;
	}
	if (OPT("turbouae_cpu_compatible") && !(p->cachesize > 0)) cfg(p, "cpu_compatible=%s", YES);
	if (OPT("turbouae_video_standard")) cfg(p, "ntsc=%s", v == "ntsc" ? "true" : "false");
	if (OPT("turbouae_collision")) cfg(p, "collision_level=%s", v.c_str());
	if (OPT("turbouae_immediate_blits")) cfg(p, "immediate_blits=%s", YES);
	if (OPT("turbouae_fast_copper")) cfg(p, "fast_copper=%s", YES);
	if (OPT("turbouae_resolution")) cfg(p, "gfx_resolution=%s", v.c_str());
	if (OPT("turbouae_linemode")) cfg(p, "gfx_linemode=%s", v.c_str());
	if (OPT("turbouae_height")) p->gfx_monitor.gfx_size_win.height = atoi(v.c_str()) * 2;
	if (OPT("turbouae_auto_height")) p->gfx_auto_height = v == "on";
	if (OPT("turbouae_vcenter")) cfg(p, "gfx_center_vertical=%s", v.c_str());
	if (OPT("turbouae_frameskip")) cfg(p, "gfx_framerate=%s", v.c_str());
	if (OPT("turbouae_leds")) cfg(p, "show_leds=%s", YES);
	if (OPT("turbouae_sound")) cfg(p, "sound_output=%s", v.c_str());
	if (OPT("turbouae_stereo_sep")) cfg(p, "sound_stereo_separation=%s", v.c_str());
	if (OPT("turbouae_interpolation")) cfg(p, "sound_interpol=%s", v.c_str());
	if (OPT("turbouae_filter")) cfg(p, "sound_filter=%s", v.c_str());
	if (OPT("turbouae_floppy_sound"))
		for (int i = 0; i < 4; i++) p->floppyslots[i].dfxclick = v == "on" ? 1 : 0;
	if (OPT("turbouae_floppy_speed")) cfg(p, "floppy_speed=%s", v.c_str());
	if (OPT("turbouae_whd_buttonwait")) p->whdbootprefs.buttonwait = v == "on";
	if (OPT("turbouae_whd_splash")) p->whdbootprefs.showsplash = v == "on";
	if (OPT("turbouae_autofire")) p->jports[1].autofire = v == "normal" ? 1 : (v == "toggle" ? 2 : 0);

	static bool cd32_by_option;
	v = opt("turbouae_pad_mode");
	lr_pad_mode = v == "joystick" ? 1 : (v == "cd32" ? 2 : 0);
	if (lr_pad_mode == 2) { p->jports[1].mode = JSEM_MODE_JOYSTICK_CD32; cd32_by_option = true; }
	else if (cd32_by_option) { p->jports[1].mode = 0; cd32_by_option = false; }
	lr_mouse_speed = atoi(opt("turbouae_mouse_speed").c_str());
	if (lr_mouse_speed < 10) lr_mouse_speed = 50;
	lr_btn_gui = button_from_name(opt("turbouae_btn_gui"));
	lr_btn_vkbd = button_from_name(opt("turbouae_btn_vkbd"));
	static const struct { const char* name; int button; } keyopt[] = {
		{ "turbouae_key_a", LR_A }, { "turbouae_key_b", LR_B }, { "turbouae_key_x", LR_X }, { "turbouae_key_y", LR_Y },
		{ "turbouae_key_l1", LR_L1 }, { "turbouae_key_r1", LR_R1 }, { "turbouae_key_l2", LR_L2 }, { "turbouae_key_r2", LR_R2 },
		{ "turbouae_key_l3", LR_L3 }, { "turbouae_key_r3", LR_R3 }, { "turbouae_key_select", LR_SELECT },
		{ "turbouae_key_start", LR_START } };
	for (int& k : lr_key_map) k = 0;
	for (const auto& k : keyopt) lr_key_map[k.button] = key_scancode(opt(k.name));
	v = opt("turbouae_dpad_keys");
	lr_dpad_keys = v == "cursor" ? 1 : (v == "both" ? 2 : 0);
	lr_lstick_keys = opt("turbouae_lstick_keys") == "cursor";
	lr_rstick_keys = opt("turbouae_rstick_keys") == "cursor";
#undef OPT
#undef YES
}

bool lr_is_regular_file(const char* path)
{
	struct stat st;
	return lstat(path, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0;
}

bool lr_copy_file(const char* from, const char* to)
{
	FILE* in = fopen(from, "rb");
	if (!in) return false;
	FILE* out = fopen(to, "wb");
	if (!out) { fclose(in); return false; }
	char buf[65536];
	size_t n;
	bool ok = true;
	while ((n = fread(buf, 1, sizeof buf, in)) > 0)
		if (fwrite(buf, 1, n, out) != n) { ok = false; break; }
	fclose(in);
	if (fclose(out) != 0) ok = false;
	if (!ok) unlink(to);
	return ok;
}

// ------------------------------------------------------------------------------------
// functions called by the (patched) emulator, on its thread
// ------------------------------------------------------------------------------------
void lr_get_base(char* out, int size) { snprintf(out, size, "%s", base_dir); }

std::string lr_base_path(const std::string& rel)
{
	if (rel.empty() || rel[0] == '/') return rel;
	return std::string(base_dir) + "/" + rel;
}

// after Amiberry read its own settings file
void lr_after_settings()
{
	char p[1100];
	snprintf(p, sizeof p, "%s/kickstarts/", base_dir);
	set_rom_path(p);
	snprintf(p, sizeof p, "%s/conf/", base_dir);
	set_configuration_path(p);
	snprintf(p, sizeof p, "%s/turbouae.log", base_dir);
	set_logfile_path(p);
	amiberry_options.use_sdl2_render_thread = false;     // "present" is ours and synchronous
	amiberry_options.write_logfile = true;
	amiberry_options.quickstart_start = false;
	amiberry_options.default_fullscreen = true;
	// How the virtual pads' buttons are numbered, for Amiberry's own pad code (navigation in the
	// settings screen, joystick, custom controls): one file per pad name in controllers/,
	// rewritten at every start.
	// Without it Amiberry assumes a pad whose d-pad is a hat, and the d-pad does nothing.
	// "b" (the settings screen's "activate" and the joystick's fire) is the pad's A button,
	// because B is the pointer's click there.
	snprintf(p, sizeof p, "%s/controllers/", base_dir);
	mkdir(p, 0775);
	set_controllers_path(p);
	static const char* const pad_names[] = { "TurboUAE RetroPad", "TurboUAE RetroPad 2" };
	static const struct { const char* name; int button; } map[] = {
		{ "b", LR_A }, { "a", LR_B }, { "x", LR_X }, { "y", LR_Y }, { "l", LR_L1 }, { "r", LR_R1 },
		{ "l2", LR_L2 }, { "r2", LR_R2 }, { "up", LR_UP }, { "down", LR_DOWN }, { "left", LR_LEFT },
		{ "right", LR_RIGHT }, { "select", LR_SELECT }, { "start", LR_START }, { "l3", LR_L3 }, { "r3", LR_R3 } };
	for (const char* name : pad_names) {
		snprintf(p, sizeof p, "%s/controllers/%s.cfg", base_dir, name);
		FILE* f = fopen(p, "w");
		if (!f) continue;
		for (const auto& m : map)
			fprintf(f, "input_%s_btn = \"%d\"\n", m.name, m.button);
		fprintf(f, "input_l_x_plus_axis = \"+0\"\ninput_l_y_plus_axis = \"+1\"\n");
		fprintf(f, "input_r_x_plus_axis = \"+2\"\ninput_r_y_plus_axis = \"+3\"\n");
		fclose(f);
	}
}

// Kickstarts: any file of a plausible ROM size in these folders is identified by its contents
static void scan_rom_dir(const char* dir)
{
	DIR* d = opendir(dir);
	if (!d) return;
	char keydir[1100];
	snprintf(keydir, sizeof keydir, "%s/", dir);
	load_keyring(&changed_prefs, keydir);             // rom.key for Cloanto's encrypted ROMs
	struct dirent* e;
	int n = 0;
	while ((e = readdir(d)) != nullptr) {
		char path[1400];
		snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
		struct stat st;
		if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;
		const long s = (long)st.st_size;
		if (s == 262144 || s == 524288 || s == 1048576 || s == 262155 || s == 524299 || s == 8192 || s == 131072) {
			lr_scan_rom_file(path);
			n++;
		}
	}
	closedir(d);
	write_log("libretro: ROM scan of %s: %d candidate files\n", dir, n);
}

void lr_scan_extra_roms()
{
	char p[1100];
	snprintf(p, sizeof p, "%s/kickstarts", base_dir);
	scan_rom_dir(p);
	scan_rom_dir(sys_dir);
	snprintf(p, sizeof p, "%s/amiga", sys_dir);
	scan_rom_dir(p);
	scan_rom_dir("/roms/ports/turbouae/kickstarts");
}

static void add_hardfile(struct uae_prefs* p, const char* path, int unit)
{
	struct uaedev_config_info ci;
	uci_set_defaults(&ci, false);
	snprintf(ci.devname, sizeof ci.devname, "DH%d", unit);
	snprintf(ci.rootdir, sizeof ci.rootdir, "%s", path);
	ci.type = UAEDEV_HDF;
	ci.controller_type = HD_CONTROLLER_TYPE_UAE;
	ci.controller_type_unit = 0;
	ci.controller_unit = unit;
	ci.unit_feature_level = 1;
	ci.readonly = access(path, W_OK) != 0;
	ci.sectors = 32; ci.surfaces = 1; ci.reserved = 2; ci.blocksize = 512;
	ci.bootpri = unit == 0 ? 0 : -128;
	ci.physical_geometry = hardfile_testrdb(ci.rootdir);
	if (ci.physical_geometry) { ci.sectors = ci.surfaces = ci.reserved = 0; }
	add_filesys_config(p, -1, &ci);
	write_log("libretro: hard disk image %s as DH%d (%s)\n", path, unit, ci.physical_geometry ? "RDB" : "plain partition");
}

static void model_prefs(struct uae_prefs* p, const std::string& model)
{
	if (model == "A500") built_in_prefs(p, 0, 0, 0, 0);
	else if (model == "A500PLUS") built_in_prefs(p, 1, 0, 0, 0);
	else if (model == "A600") built_in_prefs(p, 2, 0, 0, 0);
	else if (model == "A1200") built_in_prefs(p, 4, 0, 0, 0);
	else if (model == "A4000") built_in_prefs(p, 6, 0, 0, 0);
	else if (model == "CD32") built_in_prefs(p, 8, 0, 0, 0);
	else {      // A1200T: the machine of the Ports version
		built_in_prefs(p, 4, 0, 0, 0);
		cfg(p, "cpu_model=68020");
		cfg(p, "cpu_24bit_addressing=false");
		cfg(p, "cpu_compatible=false");
		cfg(p, "cachesize=8192");
		cfg(p, "cpu_speed=max");
		cfg(p, "chipmem_size=4");
		cfg(p, "z3mem_size=64");
		cfg(p, "gfxcard_size=16");
		cfg(p, "gfxcard_type=ZorroIII");
		cfg(p, "immediate_blits=true");
		cfg(p, "fast_copper=true");
		p->cpu_idle = 105;
		// the stock A1200 preset is cycle-exact, which silently switches the JIT off again
		p->cpu_cycle_exact = p->cpu_memory_cycle_exact = p->blitter_cycle_exact = false;
		p->cachesize = 8192;
		p->m68k_speed = -1;
	}
	// screen as in the Ports version: 640 wide, the 256 PAL lines; AGA machines line-doubled
	// (their programs use interlaced screens, which single-line mode folds into each other)
	p->gfx_monitor.gfx_size_win.width = 640;
	p->gfx_monitor.gfx_size_win.height = 512;
	cfg(p, "gfx_resolution=hires");
	cfg(p, "gfx_linemode=%s", (p->chipset_mask & CSMASK_AGA) ? "double" : "none");
	p->jports[0].id = JSEM_MICE;
	p->jports[1].id = JSEM_JOYS;
}

static bool is_floppy_ext(const std::string& e) { return e == "adf" || e == "adz" || e == "dms" || e == "fdi" || e == "zip" || e == "gz"; }
static bool is_hd_ext(const std::string& e) { return e == "hdf" || e == "hdz" || e == "vhd"; }
static bool is_cd_ext(const std::string& e) { return e == "cue" || e == "iso" || e == "ccd" || e == "mds" || e == "nrg" || e == "chd"; }

static bool load_uae(struct uae_prefs* p, const std::string& path)
{
	if (!target_cfgfile_load(p, path.c_str(), CONFIG_TYPE_ALL, 0)) {
		write_log("libretro: could not load configuration %s\n", path.c_str());
		return false;
	}
	loaded_uae = path;
	write_log("libretro: configuration %s\n", path.c_str());
	return true;
}

static void load_content(struct uae_prefs* p)
{
	std::vector<std::string> files;
	std::string ext = content_ext;
	if (ext == "m3u") {
		FILE* f = fopen(content_path.c_str(), "r");
		const std::string dir = content_path.substr(0, content_path.rfind('/') + 1);
		char line[1024];
		while (f && fgets(line, sizeof line, f)) {
			line[strcspn(line, "\r\n")] = 0;
			if (!line[0] || line[0] == '#') continue;
			files.push_back(line[0] == '/' ? std::string(line) : dir + line);
		}
		if (f) fclose(f);
		ext = files.empty() ? "" : lower_ext(files[0]);
	}
	else if (!content_path.empty())
		files.push_back(content_path);

	// WHDLoad options are read while the booter builds its machine
	std::string v;
	if ((v = opt("turbouae_whd_buttonwait")) != "auto") p->whdbootprefs.buttonwait = v == "on";
	if ((v = opt("turbouae_whd_splash")) != "auto") p->whdbootprefs.showsplash = v == "on";

	if (files.empty()) {
		const std::string cands[] = { lr_base_path("conf/default.uae"), lr_base_path("conf/Workbench.uae"), "/roms/ports/turbouae/conf/Workbench.uae" };
		for (const auto& c : cands)
			if (is_file(c.c_str()) && load_uae(p, c)) return;
		write_log("libretro: no content and no default configuration - opening the settings\n");
		model_prefs(p, opt("turbouae_model") == "auto" ? "A1200T" : opt("turbouae_model"));
		want_gui_at_start = true;
		return;
	}
	if (ext == "uae") { load_uae(p, files[0]); return; }
	if (ext == "lha" || ext == "lzh") {
		whdload_auto_prefs(p, const_cast<char*>(files[0].c_str()));
		content_whdload = true;
		// The booter's A1200 takes Kickstart 3.0 when it is there; with the JIT that one ends in
		// a reset loop (3.1 runs), and WHDLoad itself loads its own Kickstart anyway.
		if (p->cpu_model >= 68020) {
			static const int ks31_first[] = { 15, 11, 31, -1 };
			if (configure_rom(p, ks31_first, 0))
				write_log("libretro: WHDLoad machine boots from %s\n", p->romfile);
		}
		return;
	}
	if (is_cd_ext(ext)) {
		cd_auto_prefs(p, const_cast<char*>(files[0].c_str()));
		return;
	}

	std::string model = opt("turbouae_model");
	if (model == "auto") {
		if (is_hd_ext(ext)) model = "A1200T";
		else {
			std::string n = files[0];
			for (auto& c : n) c = (char)tolower((unsigned char)c);
			model = n.find("aga") != std::string::npos || n.find("a1200") != std::string::npos ? "A1200" : "A500";
		}
	}
	write_log("libretro: machine %s for %s\n", model.c_str(), files[0].c_str());
	model_prefs(p, model);

	int hd = 0, nfloppy = 0;
	disks.clear();
	for (const auto& f : files) {
		const std::string e = lower_ext(f);
		if (is_hd_ext(e)) add_hardfile(p, f.c_str(), hd++);
		else if (is_floppy_ext(e) || e == "ipf") disks.push_back(f);
	}
	// a multi-disk game gets as many drives as it has disks (up to four), disks in order
	if (disks.size() > 1 && opt("turbouae_floppy_drives") == "auto") {
		p->nr_floppies = disks.size() > 4 ? 4 : (int)disks.size();
		for (int i = 0; i < 4; i++) p->floppyslots[i].dfxtype = i < p->nr_floppies ? DRV_35_DD : DRV_NONE;
	}
	for (size_t i = 0; i < disks.size(); i++) {
		if (i < MAX_SPARE_DRIVES) snprintf(p->dfxlist[i], MAX_DPATH, "%s", disks[i].c_str());
		if ((int)i < p->nr_floppies && i < 4) { snprintf(p->floppyslots[i].df, MAX_DPATH, "%s", disks[i].c_str()); nfloppy++; }
	}
	// "<image>.uae" next to the image: extra settings for just this game
	std::string side = files[0];
	const size_t dot = side.rfind('.');
	if (dot != std::string::npos) side = side.substr(0, dot);
	side += ".uae";
	if (is_file(side.c_str())) {
		int type = CONFIG_TYPE_HARDWARE | CONFIG_TYPE_HOST;
		cfgfile_load(p, side.c_str(), &type, 0, 0);
		write_log("libretro: per-game settings %s\n", side.c_str());
	}
	write_log("libretro: %d hard disk image(s), %d floppy image(s) (%d in drives)\n", hd, (int)disks.size(), nfloppy);
}

// called once the command line has been handled: builds the machine
void lr_configure(struct uae_prefs* p)
{
	static bool content_done;
	if (!content_done) {
		content_done = true;
		load_content(p);
	}
	apply_options(p, true);
	if (content_whdload && p->cpu_model >= 68020) {
		// Room for PRELOAD (Xtreme Racing: "not enough memory" with the booter's 4 MB):
		// 64 MB of Zorro III instead of the booter's Zorro II board
		if (opt("turbouae_z3mem") == "auto") p->z3fastmem[0].size = 64 << 20;
		if (opt("turbouae_fastmem") == "auto") p->fastmem[0].size = 0;
		p->address_space_24 = false;
		if (p->cachesize > 0) {
			// With the JIT the booter's machine never comes up: A1200 chipset mode, Zorro II
			// RAM and the A1200 Kickstarts each hang it at boot (measured one at a time on the
			// Workbench machine). This is the Workbench machine's setup, booted from the A4000
			// Kickstart 3.1 (see r36s_jit_kickstart); WHDLoad loads its own Kickstart for the
			// game anyway. Without that Kickstart the JIT would only hang: off then.
			const std::string kd = lr_base_path("kickstarts");
			const char* dirs[] = { kd.c_str(), sys_dir, "/roms/ports/turbouae/kickstarts" };
			if (r36s_jit_kickstart(p, dirs, 3)) {
				const unsigned mask = p->chipset_mask;
				p->cs_compatible = CP_A4000;
				built_in_chipset_prefs(p);
				p->chipset_mask = mask;
				write_log("libretro: WHDLoad machine for the JIT, boots from %s\n", p->romfile);
			} else {
				p->cachesize = 0;
				write_log("libretro: no Kickstart 3.1 (A4000) found - WHDLoad game without the JIT\n");
			}
		}
	}
	// the frontend owns sound, screen and the quit / menu hotkeys
	cfg(p, "sound_frequency=%d", lr_audio_rate());
	p->sound_maxbsiz = 2048;
	if (getenv("TURBOUAE_SNDBUF")) p->sound_maxbsiz = atoi(getenv("TURBOUAE_SNDBUF"));      // experiments
	p->use_retroarch_quit = p->use_retroarch_menu = p->use_retroarch_reset = p->use_retroarch_statebuttons = false;
	p->gfx_apmode[0].gfx_vsync = p->gfx_apmode[1].gfx_vsync = 0;
	p->start_gui = want_gui_at_start || opt("turbouae_gui_start") == "on";
	want_gui_at_start = false;
	opt_dirty = 0;
}

// once per emulator frame / GUI pass (from r36s_frame)
void lr_emu_frame(int in_gui)
{
	if (opt_dirty && !in_gui) {
		opt_dirty = 0;
		apply_options(&changed_prefs, false);
		inputdevice_config_change();
		set_config_changed();
		write_log("libretro: core options applied\n");
	}
}

// where leaving the settings screen saves to: only a real .uae the user started from
const char* lr_config_save_path() { return loaded_uae.empty() ? nullptr : loaded_uae.c_str(); }

static void* emu_thread(void*)
{
	static char a0[] = "turbouae";
	char* argv[] = { a0, nullptr };
	lr_emu_main(1, argv);
	emu_done = 1;
	lr_wake_all(1);
	return nullptr;
}

// ------------------------------------------------------------------------------------
// keyboard: RETROK -> SDL scancode (the emulator translates those to Amiga keys)
// ------------------------------------------------------------------------------------
static int scancode_from_retrok(unsigned k)
{
	if (k >= RETROK_a && k <= RETROK_z) return SDL_SCANCODE_A + (k - RETROK_a);
	if (k >= RETROK_1 && k <= RETROK_9) return SDL_SCANCODE_1 + (k - RETROK_1);
	if (k >= RETROK_F1 && k <= RETROK_F12) return SDL_SCANCODE_F1 + (k - RETROK_F1);
	if (k >= RETROK_KP1 && k <= RETROK_KP9) return SDL_SCANCODE_KP_1 + (k - RETROK_KP1);
	switch (k) {
	case RETROK_0: return SDL_SCANCODE_0;
	case RETROK_RETURN: return SDL_SCANCODE_RETURN;
	case RETROK_ESCAPE: return SDL_SCANCODE_ESCAPE;
	case RETROK_BACKSPACE: return SDL_SCANCODE_BACKSPACE;
	case RETROK_TAB: return SDL_SCANCODE_TAB;
	case RETROK_SPACE: return SDL_SCANCODE_SPACE;
	case RETROK_MINUS: return SDL_SCANCODE_MINUS;
	case RETROK_EQUALS: return SDL_SCANCODE_EQUALS;
	case RETROK_LEFTBRACKET: return SDL_SCANCODE_LEFTBRACKET;
	case RETROK_RIGHTBRACKET: return SDL_SCANCODE_RIGHTBRACKET;
	case RETROK_BACKSLASH: return SDL_SCANCODE_BACKSLASH;
	case RETROK_SEMICOLON: return SDL_SCANCODE_SEMICOLON;
	case RETROK_QUOTE: return SDL_SCANCODE_APOSTROPHE;
	case RETROK_BACKQUOTE: return SDL_SCANCODE_GRAVE;
	case RETROK_COMMA: return SDL_SCANCODE_COMMA;
	case RETROK_PERIOD: return SDL_SCANCODE_PERIOD;
	case RETROK_SLASH: return SDL_SCANCODE_SLASH;
	case RETROK_CAPSLOCK: return SDL_SCANCODE_CAPSLOCK;
	case RETROK_DELETE: return SDL_SCANCODE_DELETE;
	case RETROK_INSERT: return SDL_SCANCODE_INSERT;
	case RETROK_HOME: return SDL_SCANCODE_HOME;
	case RETROK_END: return SDL_SCANCODE_END;
	case RETROK_PAGEUP: return SDL_SCANCODE_PAGEUP;
	case RETROK_PAGEDOWN: return SDL_SCANCODE_PAGEDOWN;
	case RETROK_UP: return SDL_SCANCODE_UP;
	case RETROK_DOWN: return SDL_SCANCODE_DOWN;
	case RETROK_LEFT: return SDL_SCANCODE_LEFT;
	case RETROK_RIGHT: return SDL_SCANCODE_RIGHT;
	case RETROK_LSHIFT: return SDL_SCANCODE_LSHIFT;
	case RETROK_RSHIFT: return SDL_SCANCODE_RSHIFT;
	case RETROK_LCTRL: return SDL_SCANCODE_LCTRL;
	case RETROK_RCTRL: return SDL_SCANCODE_RCTRL;
	case RETROK_LALT: return SDL_SCANCODE_LALT;
	case RETROK_RALT: return SDL_SCANCODE_RALT;
	case RETROK_LSUPER: case RETROK_LMETA: return SDL_SCANCODE_LGUI;
	case RETROK_RSUPER: case RETROK_RMETA: return SDL_SCANCODE_RGUI;
	case RETROK_KP0: return SDL_SCANCODE_KP_0;
	case RETROK_KP_PERIOD: return SDL_SCANCODE_KP_PERIOD;
	case RETROK_KP_DIVIDE: return SDL_SCANCODE_KP_DIVIDE;
	case RETROK_KP_MULTIPLY: return SDL_SCANCODE_KP_MULTIPLY;
	case RETROK_KP_MINUS: return SDL_SCANCODE_KP_MINUS;
	case RETROK_KP_PLUS: return SDL_SCANCODE_KP_PLUS;
	case RETROK_KP_ENTER: return SDL_SCANCODE_KP_ENTER;
	case RETROK_HELP: return SDL_SCANCODE_HELP;
	case RETROK_PRINT: return SDL_SCANCODE_PRINTSCREEN;
	case RETROK_PAUSE: return SDL_SCANCODE_PAUSE;
	case RETROK_MENU: return SDL_SCANCODE_APPLICATION;
	default: return 0;
	}
}

static void RETRO_CALLCONV keyboard_cb(bool down, unsigned keycode, uint32_t, uint16_t)
{
	const int sc = scancode_from_retrok(keycode);
	if (sc && emu_started) lr_key_event(sc, down ? 1 : 0);
}

// ------------------------------------------------------------------------------------
// disk control (multi-disk games): swaps the image in DF0
// ------------------------------------------------------------------------------------
static void disk_apply()
{
	const char* name = disk_ejected || disk_index >= disks.size() ? "" : disks[disk_index].c_str();
	snprintf(changed_prefs.floppyslots[0].df, MAX_DPATH, "%s", name);     // picked up at the next vsync
}
static bool RETRO_CALLCONV dc_set_eject(bool ejected) { disk_ejected = ejected; disk_apply(); return true; }
static bool RETRO_CALLCONV dc_get_eject(void) { return disk_ejected; }
static unsigned RETRO_CALLCONV dc_get_index(void) { return disk_index; }
static bool RETRO_CALLCONV dc_set_index(unsigned i) { disk_index = i; if (!disk_ejected) disk_apply(); return true; }
static unsigned RETRO_CALLCONV dc_get_num(void) { return (unsigned)disks.size(); }
static bool RETRO_CALLCONV dc_replace(unsigned i, const struct retro_game_info* info)
{
	if (i >= disks.size()) return false;
	if (!info || !info->path) { disks.erase(disks.begin() + i); if (disk_index >= disks.size() && disk_index) disk_index--; }
	else disks[i] = info->path;
	return true;
}
static bool RETRO_CALLCONV dc_add(void) { disks.push_back(""); return true; }
static bool RETRO_CALLCONV dc_set_initial(unsigned, const char*) { return false; }
static bool RETRO_CALLCONV dc_get_path(unsigned i, char* path, size_t len)
{
	if (i >= disks.size()) return false;
	snprintf(path, len, "%s", disks[i].c_str());
	return true;
}
static bool RETRO_CALLCONV dc_get_label(unsigned i, char* label, size_t len)
{
	if (i >= disks.size()) return false;
	const size_t s = disks[i].rfind('/');
	snprintf(label, len, "%s", disks[i].c_str() + (s == std::string::npos ? 0 : s + 1));
	return true;
}

// ------------------------------------------------------------------------------------
// libretro API
// ------------------------------------------------------------------------------------
RETRO_API unsigned retro_api_version(void) { return RETRO_API_VERSION; }

RETRO_API void retro_set_environment(retro_environment_t cb)
{
	env_cb = cb;
	bool yes = true;
	cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &yes);

	unsigned version = 0;
	if (cb(RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION, &version) && version >= 2)
		cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2, &lr_options_v2);
	else {
		// old frontends: "description; default|other|other"
		static struct retro_variable vars[MAXOPT + 1];
		static std::string text[MAXOPT];
		int n = 0;
		for (; n < MAXOPT && lr_option_defs[n].key; n++) {
			const auto& d = lr_option_defs[n];
			text[n] = std::string(d.desc) + "; " + d.default_value;
			for (int i = 0; d.values[i].value; i++)
				if (strcmp(d.values[i].value, d.default_value)) text[n] += std::string("|") + d.values[i].value;
			vars[n].key = d.key;
			vars[n].value = text[n].c_str();
		}
		vars[n].key = vars[n].value = nullptr;
		cb(RETRO_ENVIRONMENT_SET_VARIABLES, vars);
	}

	static const struct retro_controller_description pads[] = { { "RetroPad", RETRO_DEVICE_JOYPAD }, { nullptr, 0 } };
	static const struct retro_controller_info ports[] = { { pads, 1 }, { pads, 1 }, { nullptr, 0 } };
	cb(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, (void*)ports);
}

RETRO_API void retro_set_video_refresh(retro_video_refresh_t cb) { video_cb = cb; }
RETRO_API void retro_set_audio_sample(retro_audio_sample_t cb) { audio_cb = cb; }
RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_batch_cb = cb; }
RETRO_API void retro_set_input_poll(retro_input_poll_t cb) { input_poll_cb = cb; }
RETRO_API void retro_set_input_state(retro_input_state_t cb) { input_state_cb = cb; }

RETRO_API void retro_init(void)
{
	struct retro_log_callback lg;
	if (env_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &lg)) log_cb = lg.log;
	have_bitmasks = env_cb(RETRO_ENVIRONMENT_GET_INPUT_BITMASKS, nullptr);
	bool dupe = false;
	can_dupe = env_cb(RETRO_ENVIRONMENT_GET_CAN_DUPE, &dupe) && dupe;
}

RETRO_API void retro_deinit(void) {}

RETRO_API void retro_get_system_info(struct retro_system_info* info)
{
	memset(info, 0, sizeof *info);
	info->library_name = "TurboUAE";
	info->library_version = TURBOUAE_VERSION;
	info->valid_extensions = "uae|adf|adz|dms|fdi|hdf|hdz|lha|cue|iso|ccd|mds|nrg|chd|m3u|zip";
	info->need_fullpath = true;
	info->block_extract = true;
}

RETRO_API void retro_get_system_av_info(struct retro_system_av_info* info)
{
	memset(info, 0, sizeof *info);
	info->geometry.base_width = 640;
	info->geometry.base_height = 480;
	info->geometry.max_width = 1920;
	info->geometry.max_height = 1200;
	info->geometry.aspect_ratio = 4.0f / 3.0f;
	info->timing.fps = reported_fps;
	info->timing.sample_rate = lr_audio_rate();
}

RETRO_API void retro_set_controller_port_device(unsigned, unsigned) {}

static bool ntsc_from_config(const std::string& path)
{
	FILE* f = fopen(path.c_str(), "r");
	if (!f) return false;
	char line[1024];
	bool ntsc = false;
	while (fgets(line, sizeof line, f))
		if (!strncmp(line, "ntsc=", 5)) ntsc = !strncmp(line + 5, "true", 4) || !strncmp(line + 5, "yes", 3);
	fclose(f);
	return ntsc;
}

RETRO_API bool retro_load_game(const struct retro_game_info* info)
{
	const char* dir = nullptr;
	if (!env_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &dir) || !dir) dir = ".";
	snprintf(sys_dir, sizeof sys_dir, "%s", dir);
	size_t n = strlen(sys_dir);
	while (n > 1 && sys_dir[n - 1] == '/') sys_dir[--n] = 0;
	snprintf(base_dir, sizeof base_dir, "%s/turbouae", sys_dir);
	static const char* subs[] = { "", "/conf", "/kickstarts", "/savestates", "/screenshots", "/whdboot" };
	for (const char* s : subs) { const std::string d = std::string(base_dir) + s; mkdir(d.c_str(), 0775); }
	if (!is_dir((std::string(base_dir) + "/data").c_str()))
		lrlog("%s/data is missing - the settings screen and on-screen keyboard need it (install the TurboUAE core package)\n", base_dir);

	content_path = info && info->path ? info->path : "";
	content_ext = lower_ext(content_path);
	loaded_uae.clear();
	disks.clear();
	disk_index = 0; disk_ejected = false;
	want_gui_at_start = false;
	emu_done = 0;

	read_options();
	lr_video_set_format(opt("turbouae_color_depth") == "32" ? 4 : 2);
	enum retro_pixel_format fmt = lr_video_bpp() == 4 ? RETRO_PIXEL_FORMAT_XRGB8888 : RETRO_PIXEL_FORMAT_RGB565;
	if (!env_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt)) {
		lrlog("pixel format not supported by the frontend\n");
		return false;
	}
	lr_pads_connected(opt("turbouae_pad2") == "joystick" ? 2 : 1);

	std::string vs = opt("turbouae_video_standard");
	bool ntsc = vs == "ntsc" || (vs == "auto" && content_ext == "uae" && ntsc_from_config(content_path));
	reported_fps = ntsc ? 59.94f : 50.0f;

	static struct retro_keyboard_callback kcb = { keyboard_cb };
	env_cb(RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK, &kcb);

	static struct retro_disk_control_ext_callback dc = { dc_set_eject, dc_get_eject, dc_get_index, dc_set_index, dc_get_num,
		dc_replace, dc_add, dc_set_initial, dc_get_path, dc_get_label };
	unsigned dcv = 0;
	if (env_cb(RETRO_ENVIRONMENT_GET_DISK_CONTROL_INTERFACE_VERSION, &dcv) && dcv >= 1)
		env_cb(RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE, &dc);
	else
		env_cb(RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE, &dc);      // same leading members

	static const struct retro_input_descriptor desc[] = {
#define PADDESC(p) \
		{ p, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP, "Joystick up" }, \
		{ p, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN, "Joystick down" }, \
		{ p, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT, "Joystick left" }, \
		{ p, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Joystick right" }, \
		{ p, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A, "Fire / left mouse button" }, \
		{ p, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B, "2nd button / right mouse button" }, \
		{ p, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X, "Double click / CD32 green" }, \
		{ p, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y, "CD32 yellow" }, \
		{ p, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L, "CD32 rewind" }, \
		{ p, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R, "CD32 forward" }, \
		{ p, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2, "TurboUAE settings (default)" }, \
		{ p, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2, "On-screen keyboard (default)" }, \
		{ p, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "CD32 play" }, \
		{ p, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X, "Mouse / joystick X" }, \
		{ p, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y, "Mouse / joystick Y" },
		PADDESC(0) PADDESC(1)
		{ 0, 0, 0, 0, nullptr }
	};
	env_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, (void*)desc);

	// the emulator installs its JIT fault handlers; remember the frontend's to put them back
	sigaction(SIGSEGV, nullptr, &old_segv);
	sigaction(SIGILL, nullptr, &old_ill);
	sigaction(SIGBUS, nullptr, &old_bus);

	lr_wake_all(0);
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, 8 * 1024 * 1024);
	if (pthread_create(&emu_tid, &attr, emu_thread, nullptr) != 0) {
		lrlog("could not start the emulator thread\n");
		return false;
	}
	pthread_attr_destroy(&attr);
	emu_started = 1;
	lrlog("TurboUAE %s started, base %s, content %s\n", TURBOUAE_VERSION, base_dir, content_path.empty() ? "(none)" : content_path.c_str());
	return true;
}

RETRO_API bool retro_load_game_special(unsigned, const struct retro_game_info*, size_t) { return false; }

RETRO_API void retro_unload_game(void)
{
	if (!emu_started) return;
	uae_quit();
	gui_running = false;
	lr_wake_all(1);
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	ts.tv_sec += 8;
	if (pthread_timedjoin_np(emu_tid, nullptr, &ts) != 0)
		lrlog("the emulator thread did not end in time\n");
	emu_started = 0;
	sigaction(SIGSEGV, &old_segv, nullptr);
	sigaction(SIGILL, &old_ill, nullptr);
	sigaction(SIGBUS, &old_bus, nullptr);
}

RETRO_API void retro_reset(void) { if (emu_started) uae_reset(1, 0); }

static void poll_input()
{
	static const int map[LR_NBUTTONS] = { RETRO_DEVICE_ID_JOYPAD_B, RETRO_DEVICE_ID_JOYPAD_A, RETRO_DEVICE_ID_JOYPAD_X,
		RETRO_DEVICE_ID_JOYPAD_Y, RETRO_DEVICE_ID_JOYPAD_L, RETRO_DEVICE_ID_JOYPAD_R, RETRO_DEVICE_ID_JOYPAD_L2,
		RETRO_DEVICE_ID_JOYPAD_R2, RETRO_DEVICE_ID_JOYPAD_UP, RETRO_DEVICE_ID_JOYPAD_DOWN, RETRO_DEVICE_ID_JOYPAD_LEFT,
		RETRO_DEVICE_ID_JOYPAD_RIGHT, RETRO_DEVICE_ID_JOYPAD_SELECT, RETRO_DEVICE_ID_JOYPAD_START, RETRO_DEVICE_ID_JOYPAD_L3,
		RETRO_DEVICE_ID_JOYPAD_R3 };
	input_poll_cb();
	for (int p = 0; p < 2; p++) {
		unsigned buttons = 0;
		if (have_bitmasks) {
			const int16_t m = input_state_cb(p, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_MASK);
			for (int b = 0; b < LR_NBUTTONS; b++) if (m & (1 << map[b])) buttons |= 1u << b;
		} else
			for (int b = 0; b < LR_NBUTTONS; b++) if (input_state_cb(p, RETRO_DEVICE_JOYPAD, 0, map[b])) buttons |= 1u << b;
		Sint16 axes[4];
		axes[0] = input_state_cb(p, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X);
		axes[1] = input_state_cb(p, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y);
		axes[2] = input_state_cb(p, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X);
		axes[3] = input_state_cb(p, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y);
		lr_pad_set(p, buttons, axes);
	}
	// a real mouse, where the frontend has one
	const int mx = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X);
	const int my = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_Y);
	const int mb = (input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_LEFT) ? 1 : 0)
		| (input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_RIGHT) ? 2 : 0)
		| (input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_MIDDLE) ? 4 : 0);
	lr_mouse_event(mx, my, mb);
}

static void push_audio()
{
	static Sint16 abuf[2048 * 2];
	static long long last_audio_ms;
	for (int guard = 0; guard < 8; guard++) {
		const int n = lr_audio_take(abuf, 2048);
		if (n <= 0) break;
		// The emulator makes sound exactly in real time, so the frontend's buffer never fills
		// by itself: it would sit empty and every small irregularity would be an audible gap.
		// Each time sound starts (again) it is therefore preceded by 70 ms of silence, which
		// stays in the frontend's buffer as the reserve.
		struct timespec ts;
		clock_gettime(CLOCK_MONOTONIC, &ts);
		const long long now = (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
		if (now - last_audio_ms > 150 && audio_batch_cb) {
			static const Sint16 silence[1029 * 2] = { 0 };
			for (int i = 0; i < 3; i++) audio_batch_cb(silence, 1029);
		}
		if (last_audio_ms && now - last_audio_ms > au_max_gap) au_max_gap = (int)(now - last_audio_ms);
		if (now - last_audio_ms > 150) au_restarts++;
		last_audio_ms = now;
		// TURBOUAE_AUDIODUMP=<file>: raw copy of what goes to the frontend (16 bit stereo)
		static FILE* dump; static bool dump_tried;
		if (!dump_tried) { dump_tried = true; const char* f = getenv("TURBOUAE_AUDIODUMP"); if (f) dump = fopen(f, "wb"); }
		if (dump) fwrite(abuf, 4, n, dump);
		if (audio_batch_cb) audio_batch_cb(abuf, n);
		clock_gettime(CLOCK_MONOTONIC, &ts);
		const int spent = (int)((long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000 - now);
		if (spent > au_max_block) au_max_block = spent;
		audio_total += n;
		if (n < 2048) break;
	}
}

RETRO_API void retro_run(void)
{
	bool updated = false;
	if (env_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated) {
		read_options();
		opt_dirty = 1;
	}
	poll_input();

	struct lr_frame f;
	// Wait for the emulator's next frame in short slices and pass sound on in between, so
	// sound keeps flowing evenly when a frame takes long. In the settings screen frames only
	// come when something changes: do not wait long there.
	bool got = false;
	for (int waited = 0, budget = gui_running ? 16 : 60; !emu_done && !got && waited < budget; waited += 4) {
		got = lr_video_take(&f, 4) != 0;
		if (!got) push_audio();
	}
	if (got) {
		float aspect = f.lh > 0 ? (float)f.lw / (float)f.lh : 4.0f / 3.0f;
		if (aspect > 1.15f && aspect < 1.45f) aspect = 4.0f / 3.0f;      // Amiga screens are 4:3 whatever the pixel grid
		if (f.w != geom_w || f.h != geom_h || aspect != geom_aspect) {
			struct retro_game_geometry g;
			g.base_width = f.w; g.base_height = f.h;
			g.max_width = 1920; g.max_height = 1200;
			g.aspect_ratio = aspect;
			env_cb(RETRO_ENVIRONMENT_SET_GEOMETRY, &g);
			geom_w = f.w; geom_h = f.h; geom_aspect = aspect;
		}
		last_pixels = f.pixels; last_w = f.w; last_h = f.h; last_pitch = f.pitch;
		video_cb(f.pixels, f.w, f.h, f.pitch);
	}
	else if (!last_pixels)
		video_cb(blank_frame, 320, 240, 320 * lr_video_bpp());
	else if (can_dupe)
		video_cb(nullptr, last_w, last_h, last_pitch);
	else
		video_cb(last_pixels, last_w, last_h, last_pitch);

	push_audio();

	// PAL <-> NTSC while running
	if (emu_started && !gui_running && vblank_hz > 40.0f && vblank_hz < 70.0f) {
		const float fps = vblank_hz > 55.0f ? 59.94f : 50.0f;
		if (fps != reported_fps) {
			reported_fps = fps;
			struct retro_system_av_info av;
			retro_get_system_av_info(&av);
			av.geometry.base_width = geom_w ? geom_w : 640;
			av.geometry.base_height = geom_h ? geom_h : 480;
			av.geometry.aspect_ratio = geom_aspect > 0 ? geom_aspect : 4.0f / 3.0f;
			env_cb(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &av);
		}
	}

	// progress line every 5 seconds of frontend time (frontend log, needs verbose logging)
	static unsigned runs, taken, aframes;
	runs++;
	if (last_pixels == f_last_seen) {} else { taken++; f_last_seen = last_pixels; }
	if ((runs % 250) == 0) {
		lrlog("run %u: frames %u, audio %u (max gap %d ms, max block %d ms, restarts %d), presents %u, pc %08x, quit %d, gui %d, done %d\n", runs, taken, audio_total, au_max_gap, au_max_block, au_restarts, lr_present_count(), (unsigned)m68k_getpc(), quit_program, gui_running ? 1 : 0, emu_done); au_max_gap = au_max_block = 0;
	}

	if (emu_done)
		env_cb(RETRO_ENVIRONMENT_SHUTDOWN, nullptr);
}

RETRO_API size_t retro_serialize_size(void) { return 0; }
RETRO_API bool retro_serialize(void*, size_t) { return false; }
RETRO_API bool retro_unserialize(const void*, size_t) { return false; }
RETRO_API void retro_cheat_reset(void) {}
RETRO_API void retro_cheat_set(unsigned, bool, const char*) {}
RETRO_API unsigned retro_get_region(void) { return reported_fps > 55.0f ? RETRO_REGION_NTSC : RETRO_REGION_PAL; }
RETRO_API void* retro_get_memory_data(unsigned) { return nullptr; }
RETRO_API size_t retro_get_memory_size(unsigned) { return 0; }
