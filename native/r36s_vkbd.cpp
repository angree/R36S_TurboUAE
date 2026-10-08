// On-screen Amiga keyboard for the R36S (no physical keyboard). Drawn over the emulated
// screen right before it is presented; keys are sent as SDL key events, which Amiberry turns
// into Amiga keys exactly as if a USB keyboard were plugged in.
//
//   Select+Y   show / hide         d-pad    move           A   press the key
//   B          hide                 Y        Return (quick)  X   Backspace (quick)
// Shift, Ctrl, Alt and both Amiga keys are sticky: pressed once they stay down (lit) until
// the next ordinary key has been typed, so Shift+A or Amiga+Q are two presses in a row.
#include <SDL.h>
#include <SDL_ttf.h>
#include <cstring>

#include "sysdeps.h"
#include "options.h"

extern SDL_Renderer* renderer;
extern SDL_Window* sdl_window;

namespace {

struct Key { const char* label; SDL_Scancode sc; int w; };   // w in quarter key widths
#define K(l, s) { l, SDL_SCANCODE_##s, 4 }
#define KW(l, s, w) { l, SDL_SCANCODE_##s, w }
#define END { nullptr, SDL_SCANCODE_UNKNOWN, 0 }

const Key row0[] = { KW("Esc", ESCAPE, 5), K("F1", F1), K("F2", F2), K("F3", F3), K("F4", F4), K("F5", F5),
	K("F6", F6), K("F7", F7), K("F8", F8), K("F9", F9), K("F10", F10), KW("Del", DELETE, 5), KW("Help", END, 6), END };
const Key row1[] = { K("`", GRAVE), K("1", 1), K("2", 2), K("3", 3), K("4", 4), K("5", 5), K("6", 6), K("7", 7),
	K("8", 8), K("9", 9), K("0", 0), K("-", MINUS), K("=", EQUALS), K("\\", BACKSLASH), KW("<-", BACKSPACE, 6), END };
const Key row2[] = { KW("Tab", TAB, 6), K("Q", Q), K("W", W), K("E", E), K("R", R), K("T", T), K("Y", Y), K("U", U),
	K("I", I), K("O", O), K("P", P), K("[", LEFTBRACKET), K("]", RIGHTBRACKET), KW("Ret", RETURN, 8), END };
const Key row3[] = { KW("Ctrl", LCTRL, 5), KW("Caps", CAPSLOCK, 5), K("A", A), K("S", S), K("D", D), K("F", F),
	K("G", G), K("H", H), K("J", J), K("K", K), K("L", L), K(";", SEMICOLON), K("'", APOSTROPHE), KW("Ret", RETURN, 6), END };
const Key row4[] = { KW("Shift", LSHIFT, 8), K("Z", Z), K("X", X), K("C", C), K("V", V), K("B", B), K("N", N),
	K("M", M), K(",", COMMA), K(".", PERIOD), K("/", SLASH), KW("Shift", RSHIFT, 8), K("Up", UP), END };
const Key row5[] = { KW("Alt", LALT, 5), KW("A", LGUI, 5), KW("Space", SPACE, 28), KW("A", RGUI, 5), KW("Alt", RALT, 5),
	K("Lt", LEFT), K("Dn", DOWN), K("Rt", RIGHT), END };
const Key* const rows[] = { row0, row1, row2, row3, row4, row5 };
const int NROWS = 6;

bool shown;
int cur_row = 2, cur_col = 1;
bool sticky[SDL_NUM_SCANCODES];
TTF_Font* font;
bool font_tried;
SDL_Texture* label_tex[NROWS][20];
int pending_up[8], npending;              // scancodes to release on the next frame

int row_len(int r) { int n = 0; while (rows[r][n].label) n++; return n; }

bool is_modifier(SDL_Scancode sc)
{
	return sc == SDL_SCANCODE_LSHIFT || sc == SDL_SCANCODE_RSHIFT || sc == SDL_SCANCODE_LCTRL ||
		sc == SDL_SCANCODE_LALT || sc == SDL_SCANCODE_RALT || sc == SDL_SCANCODE_LGUI || sc == SDL_SCANCODE_RGUI;
}

void send(SDL_Scancode sc, bool down)
{
	SDL_Event e{};
	e.type = down ? SDL_KEYDOWN : SDL_KEYUP;
	e.key.state = down ? SDL_PRESSED : SDL_RELEASED;
	e.key.keysym.scancode = sc;
	e.key.keysym.sym = SDL_GetKeyFromScancode(sc);
	e.key.windowID = sdl_window ? SDL_GetWindowID(sdl_window) : 0;
	SDL_PushEvent(&e);
}

void press(SDL_Scancode sc)
{
	if (is_modifier(sc)) {
		sticky[sc] = !sticky[sc];
		send(sc, sticky[sc]);
		return;
	}
	send(sc, true);
	if (npending < 8) pending_up[npending++] = sc;
}

// x of each key in a row, in quarter units; returns the row's total width
int layout(int r, int* xs)
{
	int x = 0, i = 0;
	for (; rows[r][i].label; i++) { xs[i] = x; x += rows[r][i].w + 1; }
	return x;
}

int nearest_col(int from_row, int from_col, int to_row)
{
	int xa[24], xb[24];
	layout(from_row, xa);
	layout(to_row, xb);
	const int centre = xa[from_col] + rows[from_row][from_col].w / 2;
	int best = 0, bd = 1 << 30;
	for (int i = 0; rows[to_row][i].label; i++) {
		const int d = abs(xb[i] + rows[to_row][i].w / 2 - centre);
		if (d < bd) { bd = d; best = i; }
	}
	return best;
}

void load_font(int px)
{
	font_tried = true;
	if (!TTF_WasInit() && TTF_Init() != 0)
		return;
	font = TTF_OpenFont("data/AmigaTopaz.ttf", px);
	if (!font)
		write_log("r36s vkbd: no font (%s)\n", TTF_GetError());
}

SDL_Texture* label(int r, int i)
{
	if (!label_tex[r][i] && font) {
		SDL_Color white = { 255, 255, 255, 255 };
		SDL_Surface* s = TTF_RenderText_Blended(font, rows[r][i].label, white);
		if (s) { label_tex[r][i] = SDL_CreateTextureFromSurface(renderer, s); SDL_FreeSurface(s); }
	}
	return label_tex[r][i];
}

void draw()
{
	int lw = 0, lh = 0;
	SDL_RenderGetLogicalSize(renderer, &lw, &lh);
	if (lw <= 0) SDL_GetRendererOutputSize(renderer, &lw, &lh);
	const int units = 15 * 5;                          // widest row is about 75 quarter units
	const int unit = lw / (units + 2);
	const int keyh = lh * 7 / 100;
	const int gap = keyh / 6;
	const int top = lh - NROWS * (keyh + gap) - gap;
	if (!font_tried) load_font(keyh * 6 / 10);

	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 170);
	SDL_Rect bg = { 0, top - gap, lw, lh - top + gap };
	SDL_RenderFillRect(renderer, &bg);

	for (int r = 0; r < NROWS; r++) {
		int xs[24];
		const int total = layout(r, xs);
		const int x0 = (lw - total * unit) / 2;
		const int y = top + r * (keyh + gap);
		for (int i = 0; rows[r][i].label; i++) {
			SDL_Rect k = { x0 + xs[i] * unit, y, rows[r][i].w * unit, keyh };
			const bool sel = r == cur_row && i == cur_col;
			const bool lit = sticky[rows[r][i].sc];
			if (sel) SDL_SetRenderDrawColor(renderer, 230, 120, 20, 240);
			else if (lit) SDL_SetRenderDrawColor(renderer, 40, 110, 200, 230);
			else SDL_SetRenderDrawColor(renderer, 70, 70, 80, 220);
			SDL_RenderFillRect(renderer, &k);
			SDL_SetRenderDrawColor(renderer, 200, 200, 210, 255);
			SDL_RenderDrawRect(renderer, &k);
			if (SDL_Texture* t = label(r, i)) {
				int tw, th;
				SDL_QueryTexture(t, nullptr, nullptr, &tw, &th);
				if (tw > k.w - 2) { th = th * (k.w - 2) / tw; tw = k.w - 2; }
				SDL_Rect d = { k.x + (k.w - tw) / 2, k.y + (k.h - th) / 2, tw, th };
				SDL_RenderCopy(renderer, t, nullptr, &d);
			}
		}
	}
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

} // namespace

bool r36s_vkbd_shown() { return shown; }

void r36s_vkbd_toggle()
{
	shown = !shown;
	if (!shown)                                   // nothing may stay held down
		for (int i = 0; i < SDL_NUM_SCANCODES; i++)
			if (sticky[i]) { sticky[i] = false; send(static_cast<SDL_Scancode>(i), false); }
}

// called once per emulated frame with the pad's buttons (1 = held): handles navigation and
// typing, then draws. Buttons: up, down, left, right, a, b, x, y
void r36s_vkbd_frame(int up, int down, int left, int right, int a, int b, int x, int y)
{
	static int prev[8], hold[8];
	const int now[8] = { up, down, left, right, a, b, x, y };
	int hit[8];
	for (int i = 0; i < 8; i++) {                 // edge, plus auto-repeat for the d-pad
		hit[i] = now[i] && !prev[i];
		hold[i] = now[i] ? hold[i] + 1 : 0;
		if (i < 4 && hold[i] > 18 && (hold[i] % 4) == 0) hit[i] = 1;
		prev[i] = now[i];
	}
	for (int i = 0; i < npending; i++)            // release last frame's key
		send(static_cast<SDL_Scancode>(pending_up[i]), false);
	if (npending) {
		npending = 0;
		for (int i = 0; i < SDL_NUM_SCANCODES; i++)     // a typed key ends the sticky modifiers
			if (sticky[i]) { sticky[i] = false; send(static_cast<SDL_Scancode>(i), false); }
	}
	if (!shown)
		return;

	if (hit[0] && cur_row > 0) { cur_col = nearest_col(cur_row, cur_col, cur_row - 1); cur_row--; }
	if (hit[1] && cur_row < NROWS - 1) { cur_col = nearest_col(cur_row, cur_col, cur_row + 1); cur_row++; }
	if (hit[2]) cur_col = cur_col > 0 ? cur_col - 1 : row_len(cur_row) - 1;
	if (hit[3]) cur_col = cur_col < row_len(cur_row) - 1 ? cur_col + 1 : 0;
	if (hit[4]) press(rows[cur_row][cur_col].sc);
	if (hit[5]) { r36s_vkbd_toggle(); return; }
	if (hit[6]) press(SDL_SCANCODE_BACKSPACE);
	if (hit[7]) press(SDL_SCANCODE_RETURN);
	draw();
}
