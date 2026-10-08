// R36S additions to Amiberry v3.3, called once per presented frame (emulation and GUI).
//
// 1. The built-in pad drives the Amiga mouse: left stick = pointer, A = left button,
//    B = right button. (Neither Amiberry's stick-as-mouse option nor gptokeyb gave a usable
//    pointer on ArkOS, so this reads the pad itself and feeds the mouse port directly.)
// 2. Select+Start leaves Amiberry, Select+X opens the settings GUI, Select+Y the on-screen
//    keyboard (r36s_vkbd.cpp) - read straight from the
//    pad, so they work whatever the controller mapping files say.
// 3. Remote control for testing without anyone at the console: if the file named by
//    $R36S_CTL (default /tmp/r36s.ctl) exists, its lines are queued and run one per frame:
//      shot <file.bmp>       screenshot of exactly what is on the panel
//      key <SDL key name>    key press and release (e.g. "key F12", "key Return")
//      mouse <dx> <dy>       move the Amiga mouse / GUI pointer by a delta
//      click [left|right]    mouse click at the current position
//      stick <x> <y>         pretend the left stick is held at x,y (-32768..32767), "stick 0 0" ends it
//      button <a|b|x|y|select|start> <0|1>   pretend a pad button is held / released
//      wait <frames>         do nothing for that many frames
//      gui                   open the settings GUI
//      quit                  leave Amiberry
//    Every executed command is written to the Amiberry log as "r36s: ...".
#include <SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <signal.h>
#include <sys/mman.h>

#include "sysdeps.h"
#include "options.h"
#include "inputdevice.h"
#include "keyboard.h"
#include "uae.h"
#include "memory.h"
#include "newcpu.h"
#include "custom.h"
#include "gui.h"
#include "r36s_font35.h"

extern SDL_Renderer* renderer;
extern SDL_Window* sdl_window;
extern SDL_Texture* gui_texture;
extern bool gui_running;
extern char last_loaded_config[];
extern struct uae_prefs changed_prefs;

// Leaving the settings GUI saves them. The GUI's own Save button sits where the pad's
// navigation does not reach, so without this no change (RTG, memory, ...) survived a restart.
static void save_settings()
{
	char path[1024];
	const char* name = last_loaded_config[0] ? last_loaded_config : "Workbench";
	const char* dot = strrchr(name, '.');
	snprintf(path, sizeof path, "conf/%.*s.uae", dot ? (int)(dot - name) : (int)strlen(name), name);
	const int ok = cfgfile_save(&changed_prefs, path, 0);
	write_log("r36s: settings saved to %s (%s)\n", path, ok ? "ok" : "FAILED");
}
static void r36s_quit(bool in_gui) { uae_quit(); if (in_gui) gui_running = false; }
static int shot_in_gui;

// GO-Super Gamepad / R36S raw joystick numbering (what SDL_Joystick sees on ArkOS)
enum { PAD_B = 0, PAD_A = 1, PAD_X = 2, PAD_Y = 3, PAD_UP = 8, PAD_DOWN = 9, PAD_LEFT = 10, PAD_RIGHT = 11, PAD_SELECT = 12, PAD_START = 13 };

bool r36s_vkbd_shown();
void r36s_vkbd_toggle();
void r36s_vkbd_frame(int up, int down, int left, int right, int a, int b, int x, int y);

static int r36s_clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

static SDL_Joystick* pad;
// command queue: a plain ring of lines (a static std::deque here crashed on the console -
// its storage was not set up when the first frame arrived)
#define QLEN 128
static char qbuf[QLEN][256];
static int qhead, qtail;
static int q_empty() { return qhead == qtail; }
static void q_push_back(const char* l) { if ((qtail + 1) % QLEN != qhead) { snprintf(qbuf[qtail], 256, "%s", l); qtail = (qtail + 1) % QLEN; } }
static void q_push_front(const char* l) { int h = (qhead + QLEN - 1) % QLEN; if (h != qtail) { qhead = h; snprintf(qbuf[qhead], 256, "%s", l); } }
static void q_pop(char* out) { snprintf(out, 256, "%s", qbuf[qhead]); qhead = (qhead + 1) % QLEN; }
static int wait_frames;
static int frame;
static int fake_x, fake_y, fake_on;
static int fake_btn[16];

// probe <frames>: where is the Amiga CPU? Samples the 68k PC once per frame, then logs the
// most frequent PCs with the code bytes there and the registers - for finding what a program
// spins on (SysInfo with JIT).
static int probe_left;
// fpslog <n>: log Amiberry's own frame-rate counter (as on the status line) every 50 frames,
// n times - for comparing settings by numbers
static int fpslog_left;
static uae_u32 probe_pc[64];
static int probe_hits[64], probe_n;
static void probe_sample()
{
	const uae_u32 pc = m68k_getpc();
	for (int i = 0; i < probe_n; i++)
		if (probe_pc[i] == pc) { probe_hits[i]++; return; }
	if (probe_n < 64) { probe_pc[probe_n] = pc; probe_hits[probe_n++] = 1; }
}
static void probe_report()
{
	write_log("r36s probe: %d distinct PCs, intena=%04x intreq=%04x spcflags=%08x\n", probe_n, intena, intreq, regs.spcflags);
	for (int k = 0; k < 6 && k < probe_n; k++) {
		int best = 0;
		for (int i = 1; i < probe_n; i++) if (probe_hits[i] > probe_hits[best]) best = i;
		const uae_u32 pc = probe_pc[best];
		char hex[200]; int o = 0;
		for (int b = -16; b < 48 && o < 190; b += 2)
			o += snprintf(hex + o, sizeof hex - o, "%04x%s", get_word(pc + b) & 0xffff, b == -2 ? "|" : " ");
		write_log("r36s probe: pc=%08x hits=%d code[-16..+48]=%s\n", pc, probe_hits[best], hex);
		probe_hits[best] = -1;
	}
	write_log("r36s probe: D0-7 %08x %08x %08x %08x %08x %08x %08x %08x\n", regs.regs[0], regs.regs[1], regs.regs[2], regs.regs[3], regs.regs[4], regs.regs[5], regs.regs[6], regs.regs[7]);
	write_log("r36s probe: A0-7 %08x %08x %08x %08x %08x %08x %08x %08x\n", regs.regs[8], regs.regs[9], regs.regs[10], regs.regs[11], regs.regs[12], regs.regs[13], regs.regs[14], regs.regs[15]);
	probe_n = 0;
}
static int gui_x = 320, gui_y = 240;            // GUI pointer, in window (panel) pixels

static int pointer_shown;
static void draw_gui_pointer(int w, int h, bool present);
void r36s_gui_overlay();

static void save_shot(const char* path)
{
	int w = 0, h = 0;
	if (SDL_GetRendererOutputSize(renderer, &w, &h) != 0 || w <= 0)
		return;
	SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
	if (!s)
		return;
	// the GUI is drawn only when something changes, so its last frame is long gone from the
	// back buffer: draw the GUI texture again (without presenting) before reading
	if (shot_in_gui && gui_texture) {
		SDL_RenderClear(renderer);
		SDL_RenderCopy(renderer, gui_texture, nullptr, nullptr);
		r36s_gui_overlay();
		if (pointer_shown) {
			int w = 640, h = 480;
			if (sdl_window) SDL_GetWindowSize(sdl_window, &w, &h);
			draw_gui_pointer(w, h, false);
		}
	}
	if (SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch) == 0)
		SDL_SaveBMP(s, path);
	else
		write_log("r36s: RenderReadPixels failed: %s\n", SDL_GetError());
	SDL_FreeSurface(s);
}

static void push_key(SDL_Scancode sc, bool down)
{
	SDL_Event e{};
	e.type = down ? SDL_KEYDOWN : SDL_KEYUP;
	e.key.state = down ? SDL_PRESSED : SDL_RELEASED;
	e.key.keysym.scancode = sc;
	e.key.keysym.sym = SDL_GetKeyFromScancode(sc);
	e.key.windowID = sdl_window ? SDL_GetWindowID(sdl_window) : 0;
	SDL_PushEvent(&e);
}

// GUI title overlay: "TurboUAE" top left, "port by Grzegorz Korycki" top right, in the 3x5
// Foxel35 font (CC0), white with a black shadow one font pixel right and down. Drawn in panel
// pixels (logical scaling switched off for it) so the tiny font stays crisp.
static void text35(int x, int y, int scale, const char* t, Uint8 c)
{
	SDL_SetRenderDrawColor(renderer, c, c, c, 255);
	for (; *t; t++, x += 4 * scale) {
		const int ch = (unsigned char)*t;
		if (ch < 32 || ch > 126) continue;
		const unsigned char* g = r36s_font35[ch - 32];
		for (int row = 0; row < 5; row++)
			for (int col = 0; col < 3; col++)
				if (g[row] & (4 >> col)) {
					SDL_Rect r = { x + col * scale, y + row * scale, scale, scale };
					SDL_RenderFillRect(renderer, &r);
				}
	}
}
static void text35_shadow(int x, int y, int scale, const char* t)
{
	text35(x + scale, y + scale, scale, t, 0);
	text35(x, y, scale, t, 255);
}
void r36s_gui_overlay()
{
	if (!renderer) return;
	int lw = 0, lh = 0, w = 640, h = 480;
	SDL_RenderGetLogicalSize(renderer, &lw, &lh);
	SDL_GetRendererOutputSize(renderer, &w, &h);
	SDL_RenderSetLogicalSize(renderer, 0, 0);
	SDL_RenderSetScale(renderer, 1.0f, 1.0f);
	SDL_RenderSetViewport(renderer, nullptr);
	const int sc = 2;
	const char* left = "TurboUAE 0.1.0";
	const char* right = "port by Grzegorz Korycki";
	text35_shadow(2, 2, sc, left);
	text35_shadow(w - 2 - (int)strlen(right) * 4 * sc, 2, sc, right);
	if (lw > 0) SDL_RenderSetLogicalSize(renderer, lw, lh);
}

// The GUI's own pointer is an SDL cursor, which KMSDRM on ArkOS does not show. So when the
// stick moves it, the last GUI frame is drawn again with a small arrow on top.
static void draw_gui_pointer(int w, int h, bool present)
{
	if (!gui_texture || !renderer)
		return;
	int lw = 0, lh = 0;
	SDL_RenderGetLogicalSize(renderer, &lw, &lh);
	if (lw <= 0) { lw = w; lh = h; }
	const int x = gui_x * lw / w, y = gui_y * lh / h;
	if (present) {
		SDL_RenderClear(renderer);
		SDL_RenderCopy(renderer, gui_texture, nullptr, nullptr);
	}
	pointer_shown = 1;
	r36s_gui_overlay();
	for (int i = 0; i < 16; i++) {                 // arrow: black outline, white body
		SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
		SDL_Rect o = { x - 1, y + i - 1, i * 2 / 3 + 3, 3 };
		SDL_RenderFillRect(renderer, &o);
	}
	for (int i = 0; i < 14; i++) {
		SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
		SDL_Rect r = { x, y + i, i * 2 / 3 + 1, 1 };
		SDL_RenderFillRect(renderer, &r);
	}
	if (present)
		SDL_RenderPresent(renderer);
}

static void mouse_move(int dx, int dy, bool in_gui)
{
	if (in_gui) {
		// SDL turns window coordinates into the GUI's logical 800x600 itself (renderer
		// event watch), and warping also moves the visible pointer.
		int w = 640, h = 480;
		if (sdl_window) SDL_GetWindowSize(sdl_window, &w, &h);
		gui_x = r36s_clamp(gui_x + dx, 0, w - 1);
		gui_y = r36s_clamp(gui_y + dy, 0, h - 1);
		if (sdl_window) SDL_WarpMouseInWindow(sdl_window, gui_x, gui_y);
		draw_gui_pointer(w, h, true);
	} else {
		setmousestate(0, 0, dx, 0);
		setmousestate(0, 1, dy, 0);
	}
}

static void mouse_button(int button, bool down, bool in_gui)
{
	if (in_gui) {
		SDL_Event e{};
		e.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
		e.button.button = button == 0 ? SDL_BUTTON_LEFT : SDL_BUTTON_RIGHT;
		e.button.state = down ? SDL_PRESSED : SDL_RELEASED;
		e.button.x = gui_x;
		e.button.y = gui_y;
		e.button.clicks = 1;
		e.button.windowID = sdl_window ? SDL_GetWindowID(sdl_window) : 0;
		SDL_PushEvent(&e);
	} else {
		setmousebuttonstate(0, button, down ? 1 : 0);
	}
}

static int button_index(const char* n)
{
	if (!strcmp(n, "a")) return PAD_A;
	if (!strcmp(n, "b")) return PAD_B;
	if (!strcmp(n, "x")) return PAD_X;
	if (!strcmp(n, "y")) return PAD_Y;
	if (!strcmp(n, "select")) return PAD_SELECT;
	if (!strcmp(n, "start")) return PAD_START;
	if (!strcmp(n, "up")) return PAD_UP;
	if (!strcmp(n, "down")) return PAD_DOWN;
	if (!strcmp(n, "left")) return PAD_LEFT;
	if (!strcmp(n, "right")) return PAD_RIGHT;
	return -1;
}

static void run_command(const char* line, bool in_gui)
{
	char a[256] = "", b[64] = "";
	int x = 0, y = 0;
	write_log("r36s: %s (%s)\n", line, in_gui ? "gui" : "emulation");
	if (sscanf(line, "shot %255s", a) == 1) {
		shot_in_gui = in_gui;
		save_shot(a);
	}
	else if (sscanf(line, "key %255[^\n]", a) == 1) {
		const SDL_Scancode sc = SDL_GetScancodeFromName(a);
		if (sc != SDL_SCANCODE_UNKNOWN) {
			push_key(sc, true);
			{ char t[32]; snprintf(t, sizeof t, "keyup %d", (int)sc); q_push_front(t); }
		}
	}
	else if (sscanf(line, "keyup %d", &x) == 1)
		push_key(static_cast<SDL_Scancode>(x), false);
	else if (sscanf(line, "mouse %d %d", &x, &y) == 2)
		mouse_move(x, y, in_gui);
	else if (strncmp(line, "clickup", 7) == 0)
		mouse_button(strstr(line, "right") ? 1 : 0, false, in_gui);
	else if (strncmp(line, "click", 5) == 0) {
		const int btn = strstr(line, "right") ? 1 : 0;
		mouse_button(btn, true, in_gui);
		q_push_front(btn ? "clickup right" : "clickup left");
		q_push_front("wait 3");
	}
	else if (sscanf(line, "stick %d %d", &x, &y) == 2) {
		fake_x = x; fake_y = y; fake_on = x || y;
	}
	else if (sscanf(line, "button %63s %d", b, &x) == 2) {
		const int i = button_index(b);
		if (i >= 0) fake_btn[i] = x;
	}
	else if (sscanf(line, "fpslog %d", &x) == 1)
		fpslog_left = x;
	else if (sscanf(line, "probe %d", &x) == 1) {
		probe_n = 0; probe_left = x;
	}
	else if (sscanf(line, "wait %d", &x) == 1)
		wait_frames = x;
	else if (!strcmp(line, "gui"))
		inputdevice_add_inputcode(AKS_ENTERGUI, 1, nullptr);
	else if (!strcmp(line, "quit"))
		r36s_quit(in_gui);
}

static void read_control_file()
{
	const char* path = getenv("R36S_CTL");
	if (!path) path = "/tmp/r36s.ctl";
	FILE* f = fopen(path, "r");
	if (!f)
		return;
	char buf[512];
	while (fgets(buf, sizeof buf, f)) {
		buf[strcspn(buf, "\r\n")] = 0;
		if (buf[0] && buf[0] != '#')
			q_push_back(buf);
	}
	fclose(f);
	unlink(path);
}

static int pad_button(int i)
{
	if (fake_btn[i]) return 1;
	return pad ? SDL_JoystickGetButton(pad, i) : 0;
}


// SDL 2.30 on ArkOS (its evdev keyboard code) installs its own SIGSEGV/SIGBUS/SIGILL handlers
// after Amiberry set up its own. SDL's handler restores the console and re-raises, so
// Amiberry's handler then sees the fault inside raise() instead of in JIT code, gives up
// ("Error not in JIT code") and the emulator dies. That handler is what lets the JIT survive
// a program probing for memory that is not there (SysInfo, xSysInfo, many games and demos):
// it redoes the access through the slow, checked path. So put Amiberry's handlers back on top.
extern void signal_segv(int signum, siginfo_t* info, void* ptr);
extern void signal_buserror(int signum, siginfo_t* info, void* ptr);
static void keep_jit_fault_handlers()
{
	struct sigaction cur{};
	sigaction(SIGSEGV, nullptr, &cur);
	if ((cur.sa_flags & SA_SIGINFO) && cur.sa_sigaction == signal_segv)
		return;
	struct sigaction a{};
	a.sa_sigaction = signal_segv;
	a.sa_flags = SA_SIGINFO;
	sigaction(SIGSEGV, &a, nullptr);
	sigaction(SIGILL, &a, nullptr);
	a.sa_sigaction = signal_buserror;
	sigaction(SIGBUS, &a, nullptr);
	write_log("r36s: SIGSEGV/SIGBUS/SIGILL handlers given back to Amiberry (were replaced by SDL)\n");
}


// JIT memory holes. The AArch64 JIT compiles an access straight into the host mapping of the
// Amiga address space (natmem) when the instruction hit plain RAM while it was profiled. When
// the same instruction later points into a hole (no memory, I/O, a mirror), the host access
// must fault: Amiberry's fault handler then performs it through the memory bank and has the
// block recompiled with checked access. WinUAE leaves holes unmapped for exactly that; Amiberry
// v3.3 maps all 16 MB read/write, so nothing faults and the JIT reads and writes host memory
// that the Amiga does not have. Kickstart's memory probing then "finds" 1.25 MB of RAM at
// $C80000-$DBFFFF, exec hands it out, and programs crash in it (xSysInfo on the RTG screen).
// Here every 64 KB page of the 24-bit space that is not backed directly by its own bank's
// memory is made inaccessible; this is redone whenever the memory map changes (autoconfig).
static unsigned char page_direct[256];
static int holes_known;
static void protect_memory_holes()
{
	if (!currprefs.cachesize || !regs.natmem_offset)
		return;
	int changed = 0, holes = 0;
	for (int p = 0; p < 256; p++) {
		const uae_u32 addr = (uae_u32)p << 16;
		addrbank* ab = &get_mem_bank(addr);
		uae_u8* host = regs.natmem_offset + addr;
		// $F00000-$FFFFFF (UAE boot ROM, Kickstart) is left alone: the JIT reads 68k code
		// ahead past the end of the 64 KB boot ROM while compiling, which is harmless.
		const bool direct = p >= 0xf0 ||
			(ab->baseaddr && host >= ab->baseaddr && host < ab->baseaddr + ab->allocated_size);
		if (!direct) holes++;
		if (holes_known && page_direct[p] == (direct ? 1 : 2))
			continue;
		page_direct[p] = direct ? 1 : 2;
		mprotect(host, 65536, direct ? (PROT_READ | PROT_WRITE) : PROT_NONE);
		changed++;
	}
	if (changed && (!holes_known || changed < 256))
		write_log("r36s: JIT memory holes: %d of 256 pages inaccessible (%d changed)\n", holes, changed);
	holes_known = 1;
}

// called by map_banks() (patched): the memory map just changed
void r36s_memory_map_changed()
{
	protect_memory_holes();
}

// called at the start of memory_clear() (patched): it clears each bank's whole allocation,
// which can be larger than what is mapped (slow memory: 2 MB allocated, 1.5 MB visible), so
// everything is made accessible first; map_banks()/the next check protect the holes again.
void r36s_memory_unprotect_all()
{
	if (!regs.natmem_offset)
		return;
	mprotect(regs.natmem_offset, 1 << 24, PROT_READ | PROT_WRITE);
	holes_known = 0;
}

void r36s_frame(int in_gui)
{
	static int prev[16];
	static int combo_latched;
	static int was_gui;
	frame++;
	if ((frame % 50) == 1)
		keep_jit_fault_handlers();
	if (!in_gui && (frame % 25) == 2)
		protect_memory_holes();
	// resolved JIT faults are normal; only a burst of them means something runs away
	extern int r36s_fault_burst;
	if ((frame % 250) == 0)
		r36s_fault_burst = 0;
	if (was_gui && !in_gui)
		save_settings();
	was_gui = in_gui;
	if (fpslog_left > 0 && !in_gui && (frame % 50) == 0) {
		fpslog_left--;
		write_log("r36s fps=%d idle=%d\n", gui_data.fps, gui_data.idle);
	}
	if (probe_left > 0 && !in_gui) {
		probe_sample();
		if (--probe_left == 0) probe_report();
	}

	if (!pad && SDL_NumJoysticks() > 0)
		pad = SDL_JoystickOpen(0);

	// hotkeys, straight from the pad
	const int sel = pad_button(PAD_SELECT);
	if (sel && pad_button(PAD_START)) {
		if (!combo_latched) { combo_latched = 1; write_log("r36s: Select+Start, quitting\n"); r36s_quit(in_gui != 0); }
	}
	else if (sel && pad_button(PAD_X) && !in_gui) {
		if (!combo_latched) { combo_latched = 1; inputdevice_add_inputcode(AKS_ENTERGUI, 1, nullptr); }
	}
	else if (sel && pad_button(PAD_Y) && !in_gui) {
		if (!combo_latched) { combo_latched = 1; r36s_vkbd_toggle(); }
	}
	else
		combo_latched = 0;

	// on-screen keyboard: while it is shown the pad's buttons type, so A/B are not the mouse
	if (!in_gui) {
		const bool kb = r36s_vkbd_shown();
		r36s_vkbd_frame(kb && !sel && pad_button(PAD_UP), kb && !sel && pad_button(PAD_DOWN),
			kb && !sel && pad_button(PAD_LEFT), kb && !sel && pad_button(PAD_RIGHT),
			kb && !sel && pad_button(PAD_A), kb && !sel && pad_button(PAD_B),
			kb && !sel && pad_button(PAD_X), kb && !sel && pad_button(PAD_Y));
		if (r36s_vkbd_shown()) { prev[PAD_A] = pad_button(PAD_A); prev[PAD_B] = pad_button(PAD_B); }
	}

	// left stick -> mouse, A/B -> mouse buttons (not while Select is held: those are hotkeys)
	int ax = fake_on ? fake_x : (pad ? SDL_JoystickGetAxis(pad, 0) : 0);
	int ay = fake_on ? fake_y : (pad ? SDL_JoystickGetAxis(pad, 1) : 0);
	const int dead = 6000;
	ax = abs(ax) < dead ? 0 : (ax > 0 ? ax - dead : ax + dead);
	ay = abs(ay) < dead ? 0 : (ay > 0 ? ay - dead : ay + dead);
	if (ax || ay) {
		// quadratic response: fine control near the centre, fast across the screen
		const int dx = static_cast<int>(static_cast<long long>(ax) * abs(ax) / (26768LL * 26768LL / 14));
		const int dy = static_cast<int>(static_cast<long long>(ay) * abs(ay) / (26768LL * 26768LL / 14));
		if (dx || dy)
			mouse_move(dx, dy, in_gui != 0);
	}
	if (!sel && !(r36s_vkbd_shown() && !in_gui)) {
		const int bl = pad_button(PAD_A), br = pad_button(PAD_B);
		// in the GUI, A stays the GUI's own "activate" button; only B clicks there
		if (!in_gui && bl != prev[PAD_A]) mouse_button(0, bl != 0, false);
		// X = double click (opening icons with A alone needs two quick presses)
		const int bx = pad_button(PAD_X);
		if (!in_gui && bx && !prev[PAD_X])
			q_push_front("click"), q_push_front("wait 6"), q_push_front("click");
		prev[PAD_X] = bx;
		if (br != prev[PAD_B]) mouse_button(in_gui ? 0 : 1, br != 0, in_gui != 0);
		prev[PAD_A] = bl;
		prev[PAD_B] = br;
	}

	// remote control - last, so a screenshot includes the on-screen keyboard
	if ((frame & 15) == 0)
		read_control_file();
	if (wait_frames > 0)
		wait_frames--;
	else if (!q_empty()) {
		char cmd[256];
		q_pop(cmd);
		run_command(cmd, in_gui != 0);
	}

}
