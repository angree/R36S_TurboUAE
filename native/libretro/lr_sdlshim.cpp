// TurboUAE libretro core: implementation of the SDL shim (see lr_sdlshim.h).
#include "lr_sdlshim.h"
#include <pthread.h>
#include <cstdlib>
#include <cstring>
#include <ctime>

// ------------------------------------------------------------------------------------
// video: software renderer with three buffers (back = being drawn by the emulator thread,
// ready = last presented, front = being shown by the frontend thread)
// ------------------------------------------------------------------------------------
namespace {

struct Tex {
	Uint32 format;
	int w, h, bpp;
	Uint8* own; int own_pitch;          // pixels owned by the texture
	const Uint8* ref; int ref_pitch;    // or: the caller's buffer of the last full update
	SDL_BlendMode blend;
	bool has_alpha;
};

struct Buf { Uint8* pix; int cap; int w, h, pitch, lw, lh; };

Buf bufs[3];
int back = 0, ready = 1, front = 2;
bool ready_new, quitting;
pthread_mutex_t vmx = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond_frame = PTHREAD_COND_INITIALIZER, cond_taken = PTHREAD_COND_INITIALIZER;

int out_bpp = 2;                        // 2 = RGB565, 4 = XRGB8888
int bw = 640, bh = 480;                 // size of the frame being drawn
int logical_w, logical_h;
Uint8 dr, dg, db, da = 255;
SDL_BlendMode dblend = SDL_BLENDMODE_NONE;
bool clear_pending;
int fake_window, fake_renderer;
volatile unsigned present_count;

void deadline(struct timespec* ts, int ms)
{
	clock_gettime(CLOCK_REALTIME, ts);
	ts->tv_nsec += (long)(ms % 1000) * 1000000L;
	ts->tv_sec += ms / 1000 + ts->tv_nsec / 1000000000L;
	ts->tv_nsec %= 1000000000L;
}

long long last_poll_ms;                 // when the frontend last asked for a frame
long long mono_ms()
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void ensure_back(int w, int h)
{
	Buf& b = bufs[back];
	const int need = w * h * out_bpp;
	if (b.cap < need) {
		free(b.pix);
		b.pix = static_cast<Uint8*>(calloc(1, need));
		b.cap = need;
	}
	b.w = bw = w; b.h = bh = h; b.pitch = w * out_bpp;
}

inline Uint32 pack(int r, int g, int b)
{
	return out_bpp == 2 ? (Uint32)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)) : (Uint32)(0xff000000u | (r << 16) | (g << 8) | b);
}

inline void unpack_out(const Uint8* p, int& r, int& g, int& b)
{
	if (out_bpp == 2) {
		const Uint32 v = *reinterpret_cast<const Uint16*>(p);
		r = (v >> 11) << 3; g = ((v >> 5) & 63) << 2; b = (v & 31) << 3;
	} else {
		const Uint32 v = *reinterpret_cast<const Uint32*>(p);
		r = (v >> 16) & 255; g = (v >> 8) & 255; b = v & 255;
	}
}

inline void put(Uint8* p, Uint32 v)
{
	if (out_bpp == 2) *reinterpret_cast<Uint16*>(p) = (Uint16)v; else *reinterpret_cast<Uint32*>(p) = v;
}

// layout classes of the source formats the emulator uses
enum { F_565, F_ARGB, F_ABGR, F_RGBA, F_BGRA };
int fclass(Uint32 f)
{
	switch (f) {
	case SDL_PIXELFORMAT_RGB565: return F_565;
	case SDL_PIXELFORMAT_ABGR8888: case SDL_PIXELFORMAT_BGR888: return F_ABGR;
	case SDL_PIXELFORMAT_RGBA8888: case SDL_PIXELFORMAT_RGBX8888: return F_RGBA;
	case SDL_PIXELFORMAT_BGRA8888: case SDL_PIXELFORMAT_BGRX8888: return F_BGRA;
	default: return F_ARGB;
	}
}
bool falpha(Uint32 f)
{
	return f == SDL_PIXELFORMAT_ARGB8888 || f == SDL_PIXELFORMAT_ABGR8888 || f == SDL_PIXELFORMAT_RGBA8888 || f == SDL_PIXELFORMAT_BGRA8888;
}

inline void unpack_src(int cls, const Uint8* p, int& r, int& g, int& b, int& a)
{
	if (cls == F_565) {
		const Uint32 v = *reinterpret_cast<const Uint16*>(p);
		r = (v >> 11) << 3; g = ((v >> 5) & 63) << 2; b = (v & 31) << 3; a = 255;
		return;
	}
	const Uint32 v = *reinterpret_cast<const Uint32*>(p);
	switch (cls) {
	case F_ABGR: r = v & 255; g = (v >> 8) & 255; b = (v >> 16) & 255; a = v >> 24; break;
	case F_RGBA: r = v >> 24; g = (v >> 16) & 255; b = (v >> 8) & 255; a = v & 255; break;
	case F_BGRA: b = v >> 24; g = (v >> 16) & 255; r = (v >> 8) & 255; a = v & 255; break;
	default: r = (v >> 16) & 255; g = (v >> 8) & 255; b = v & 255; a = v >> 24; break;
	}
}

// whole texture -> whole frame (the emulated screen, the GUI)
void copy_full(const Tex* t, const Uint8* src, int spitch)
{
	ensure_back(t->w, t->h);
	Buf& b = bufs[back];
	const int cls = fclass(t->format);
	const int w = t->w, h = t->h;
	if ((cls == F_565 && out_bpp == 2) || (cls == F_ARGB && out_bpp == 4)) {
		for (int y = 0; y < h; y++)
			memcpy(b.pix + y * b.pitch, src + y * spitch, w * out_bpp);
		return;
	}
	for (int y = 0; y < h; y++) {
		const Uint8* s = src + y * spitch;
		Uint8* d = b.pix + y * b.pitch;
		if (cls == F_565) {                               // 565 -> XRGB8888
			const Uint16* s16 = reinterpret_cast<const Uint16*>(s);
			Uint32* d32 = reinterpret_cast<Uint32*>(d);
			for (int x = 0; x < w; x++) {
				const Uint32 v = s16[x];
				const Uint32 r = (v >> 11) & 31, g = (v >> 5) & 63, bl = v & 31;
				d32[x] = 0xff000000u | ((r << 3 | r >> 2) << 16) | ((g << 2 | g >> 4) << 8) | (bl << 3 | bl >> 2);
			}
		} else if (out_bpp == 4) {                        // other 32 bit layouts -> XRGB8888
			const Uint32* s32 = reinterpret_cast<const Uint32*>(s);
			Uint32* d32 = reinterpret_cast<Uint32*>(d);
			if (cls == F_ABGR)
				for (int x = 0; x < w; x++) { const Uint32 v = s32[x]; d32[x] = 0xff000000u | ((v & 255) << 16) | (v & 0xff00) | ((v >> 16) & 255); }
			else if (cls == F_RGBA)
				for (int x = 0; x < w; x++) d32[x] = 0xff000000u | (s32[x] >> 8);
			else
				for (int x = 0; x < w; x++) { const Uint32 v = s32[x]; d32[x] = 0xff000000u | ((v >> 8) & 0xff00) << 8 | ((v >> 16) & 255) << 8 | (v >> 24); }
		} else {                                          // 32 bit -> 565
			const Uint32* s32 = reinterpret_cast<const Uint32*>(s);
			Uint16* d16 = reinterpret_cast<Uint16*>(d);
			if (cls == F_ABGR)
				for (int x = 0; x < w; x++) { const Uint32 v = s32[x]; d16[x] = (Uint16)(((v & 0xf8) << 8) | ((v >> 5) & 0x7e0) | ((v >> 19) & 0x1f)); }
			else if (cls == F_ARGB)
				for (int x = 0; x < w; x++) { const Uint32 v = s32[x]; d16[x] = (Uint16)(((v >> 8) & 0xf800) | ((v >> 5) & 0x7e0) | ((v >> 3) & 0x1f)); }
			else
				for (int x = 0; x < w; x++) { int r, g, bl, a; unpack_src(cls, s + x * 4, r, g, bl, a); d16[x] = (Uint16)pack(r, g, bl); }
		}
	}
}

// logical coordinates -> frame coordinates
inline int mapx(int x) { return logical_w > 0 ? x * bw / logical_w : x; }
inline int mapy(int y) { return logical_h > 0 ? y * bh / logical_h : y; }

void fill(int x0, int y0, int x1, int y1)       // frame coordinates, end exclusive
{
	Buf& b = bufs[back];
	if (!b.pix) return;
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > b.w) x1 = b.w;
	if (y1 > b.h) y1 = b.h;
	if (x0 >= x1 || y0 >= y1) return;
	const bool blend = dblend == SDL_BLENDMODE_BLEND && da < 255;
	const Uint32 c = pack(dr, dg, db);
	for (int y = y0; y < y1; y++) {
		Uint8* p = b.pix + y * b.pitch + x0 * out_bpp;
		for (int x = x0; x < x1; x++, p += out_bpp) {
			if (blend) {
				int r, g, bl;
				unpack_out(p, r, g, bl);
				put(p, pack((dr * da + r * (255 - da)) / 255, (dg * da + g * (255 - da)) / 255, (db * da + bl * (255 - da)) / 255));
			} else
				put(p, c);
		}
	}
}

void blit_scaled(const Tex* t, const Uint8* src, int spitch, const SDL_Rect* sr, const SDL_Rect* dst)
{
	Buf& b = bufs[back];
	if (!b.pix) return;
	const int sx0 = sr ? sr->x : 0, sy0 = sr ? sr->y : 0, sw = sr ? sr->w : t->w, sh = sr ? sr->h : t->h;
	const int dx0 = mapx(dst->x), dy0 = mapy(dst->y), dx1 = mapx(dst->x + dst->w), dy1 = mapy(dst->y + dst->h);
	if (dx1 <= dx0 || dy1 <= dy0 || sw <= 0 || sh <= 0) return;
	const int cls = fclass(t->format);
	const bool blend = t->blend == SDL_BLENDMODE_BLEND && t->has_alpha;
	for (int y = dy0 < 0 ? 0 : dy0; y < dy1 && y < b.h; y++) {
		const int sy = sy0 + (y - dy0) * sh / (dy1 - dy0);
		if (sy < 0 || sy >= t->h) continue;
		const Uint8* srow = src + sy * spitch;
		Uint8* p = b.pix + y * b.pitch;
		for (int x = dx0 < 0 ? 0 : dx0; x < dx1 && x < b.w; x++) {
			const int sx = sx0 + (x - dx0) * sw / (dx1 - dx0);
			if (sx < 0 || sx >= t->w) continue;
			int r, g, bl, a;
			unpack_src(cls, srow + sx * t->bpp, r, g, bl, a);
			Uint8* d = p + x * out_bpp;
			if (blend) {
				if (a == 0) continue;
				if (a < 255) {
					int r2, g2, b2;
					unpack_out(d, r2, g2, b2);
					r = (r * a + r2 * (255 - a)) / 255; g = (g * a + g2 * (255 - a)) / 255; bl = (bl * a + b2 * (255 - a)) / 255;
				}
			}
			put(d, pack(r, g, bl));
		}
	}
}

void do_clear()
{
	Buf& b = bufs[back];
	if (!b.pix) ensure_back(bw, bh);
	memset(bufs[back].pix, 0, bufs[back].h * bufs[back].pitch);
	clear_pending = false;
}

Tex* T(SDL_Texture* t) { return reinterpret_cast<Tex*>(t); }

} // namespace

unsigned lr_present_count() { return present_count; }
void lr_video_set_format(int bytes_per_pixel) { out_bpp = bytes_per_pixel == 4 ? 4 : 2; }
int lr_video_bpp(void) { return out_bpp; }
Uint32 lr_video_sdl_format(void) { return out_bpp == 2 ? SDL_PIXELFORMAT_RGB565 : SDL_PIXELFORMAT_ARGB8888; }

int lr_video_take(struct lr_frame* out, int wait_ms)
{
	int got = 0;
	pthread_mutex_lock(&vmx);
	last_poll_ms = mono_ms();
	if (!ready_new && wait_ms > 0 && !quitting) {
		struct timespec ts;
		deadline(&ts, wait_ms);
		pthread_cond_timedwait(&cond_frame, &vmx, &ts);
	}
	if (ready_new) {
		const int t = front; front = ready; ready = t;
		ready_new = false;
		pthread_cond_broadcast(&cond_taken);
		const Buf& b = bufs[front];
		out->pixels = b.pix; out->w = b.w; out->h = b.h; out->pitch = b.pitch; out->lw = b.lw; out->lh = b.lh;
		got = 1;
	}
	pthread_mutex_unlock(&vmx);
	return got;
}

void lr_wake_all(int q)
{
	pthread_mutex_lock(&vmx);
	quitting = q != 0;
	pthread_cond_broadcast(&cond_taken);
	pthread_cond_broadcast(&cond_frame);
	pthread_mutex_unlock(&vmx);
}

int lr_SDL_Init(Uint32) { return 0; }
void lr_SDL_Quit(void) {}
void lr_SDL_VideoQuit(void) {}

SDL_Window* lr_SDL_CreateWindow(const char*, int, int, int, int, Uint32) { return reinterpret_cast<SDL_Window*>(&fake_window); }
void lr_SDL_DestroyWindow(SDL_Window*) {}
Uint32 lr_SDL_GetWindowFlags(SDL_Window*) { return SDL_WINDOW_FULLSCREEN_DESKTOP; }
Uint32 lr_SDL_GetWindowID(SDL_Window*) { return 1; }
void lr_SDL_GetWindowSize(SDL_Window*, int* w, int* h) { if (w) *w = bw; if (h) *h = bh; }
void lr_SDL_SetWindowSize(SDL_Window*, int, int) {}
int lr_SDL_SetWindowFullscreen(SDL_Window*, Uint32) { return 0; }
void lr_SDL_SetWindowIcon(SDL_Window*, SDL_Surface*) {}
void lr_SDL_RaiseWindow(SDL_Window*) {}
int lr_SDL_GetCurrentDisplayMode(int, SDL_DisplayMode* mode)
{
	memset(mode, 0, sizeof *mode);
	mode->format = lr_video_sdl_format();
	mode->w = 640; mode->h = 480; mode->refresh_rate = 60;
	return 0;
}
const char* lr_SDL_GetCurrentVideoDriver(void) { return "libretro"; }

SDL_Renderer* lr_SDL_CreateRenderer(SDL_Window*, int, Uint32) { return reinterpret_cast<SDL_Renderer*>(&fake_renderer); }
void lr_SDL_DestroyRenderer(SDL_Renderer*) {}
int lr_SDL_GetRendererOutputSize(SDL_Renderer*, int* w, int* h) { if (w) *w = bw; if (h) *h = bh; return 0; }
int lr_SDL_RenderSetLogicalSize(SDL_Renderer*, int w, int h) { logical_w = w; logical_h = h; return 0; }
void lr_SDL_RenderGetLogicalSize(SDL_Renderer*, int* w, int* h) { if (w) *w = logical_w; if (h) *h = logical_h; }
int lr_SDL_RenderSetScale(SDL_Renderer*, float, float) { return 0; }
int lr_SDL_RenderSetViewport(SDL_Renderer*, const SDL_Rect*) { return 0; }
int lr_SDL_RenderSetClipRect(SDL_Renderer*, const SDL_Rect*) { return 0; }
int lr_SDL_SetRenderDrawColor(SDL_Renderer*, Uint8 r, Uint8 g, Uint8 b, Uint8 a) { dr = r; dg = g; db = b; da = a; return 0; }
int lr_SDL_GetRenderDrawColor(SDL_Renderer*, Uint8* r, Uint8* g, Uint8* b, Uint8* a)
{
	if (r) *r = dr; if (g) *g = dg; if (b) *b = db; if (a) *a = da;
	return 0;
}
int lr_SDL_SetRenderDrawBlendMode(SDL_Renderer*, SDL_BlendMode mode) { dblend = mode; return 0; }
int lr_SDL_SetRenderTarget(SDL_Renderer*, SDL_Texture*) { return 0; }
int lr_SDL_RenderClear(SDL_Renderer*) { clear_pending = true; return 0; }

int lr_SDL_RenderCopy(SDL_Renderer*, SDL_Texture* tex, const SDL_Rect* src, const SDL_Rect* dst)
{
	Tex* t = T(tex);
	if (!t) return -1;
	const Uint8* pix = t->ref ? t->ref : t->own;
	const int pitch = t->ref ? t->ref_pitch : t->own_pitch;
	if (!pix) return -1;
	const int lw = logical_w > 0 ? logical_w : bw, lh = logical_h > 0 ? logical_h : bh;
	const bool full = !src && (!dst || (dst->x <= 0 && dst->y <= 0 && dst->w >= lw && dst->h >= lh));
	if (full && !(t->blend == SDL_BLENDMODE_BLEND && t->has_alpha)) {
		copy_full(t, pix, pitch);
		clear_pending = false;
		return 0;
	}
	if (clear_pending) do_clear();
	SDL_Rect whole = { 0, 0, lw, lh };
	blit_scaled(t, pix, pitch, src, dst ? dst : &whole);
	return 0;
}
int lr_SDL_RenderCopyEx(SDL_Renderer* r, SDL_Texture* t, const SDL_Rect* src, const SDL_Rect* dst, double, const SDL_Point*, SDL_RendererFlip)
{
	return lr_SDL_RenderCopy(r, t, src, dst);
}

int lr_SDL_RenderFillRect(SDL_Renderer*, const SDL_Rect* rect)
{
	if (clear_pending) do_clear();
	if (!rect) { fill(0, 0, bw, bh); return 0; }
	fill(mapx(rect->x), mapy(rect->y), mapx(rect->x + rect->w), mapy(rect->y + rect->h));
	return 0;
}
int lr_SDL_RenderDrawRect(SDL_Renderer* r, const SDL_Rect* rect)
{
	if (!rect) return 0;
	if (clear_pending) do_clear();
	const int x0 = mapx(rect->x), y0 = mapy(rect->y), x1 = mapx(rect->x + rect->w), y1 = mapy(rect->y + rect->h);
	fill(x0, y0, x1, y0 + 1); fill(x0, y1 - 1, x1, y1);
	fill(x0, y0, x0 + 1, y1); fill(x1 - 1, y0, x1, y1);
	return 0;
}
int lr_SDL_RenderDrawPoint(SDL_Renderer*, int x, int y)
{
	if (clear_pending) do_clear();
	fill(mapx(x), mapy(y), mapx(x) + 1, mapy(y) + 1);
	return 0;
}
int lr_SDL_RenderDrawLine(SDL_Renderer* r, int x1, int y1, int x2, int y2)
{
	if (clear_pending) do_clear();
	int ax = mapx(x1), ay = mapy(y1);
	const int bx = mapx(x2), by = mapy(y2);
	const int dx = abs(bx - ax), dy = -abs(by - ay), sx = ax < bx ? 1 : -1, sy = ay < by ? 1 : -1;
	int err = dx + dy;
	for (int n = 0; n < 8192; n++) {
		fill(ax, ay, ax + 1, ay + 1);
		if (ax == bx && ay == by) break;
		const int e2 = 2 * err;
		if (e2 >= dy) { err += dy; ax += sx; }
		if (e2 <= dx) { err += dx; ay += sy; }
	}
	return 0;
}

void lr_SDL_RenderPresent(SDL_Renderer*)
{
	if (clear_pending) do_clear();
	Buf& b = bufs[back];
	if (!b.pix) return;
	b.lw = logical_w > 0 ? logical_w : b.w;
	b.lh = logical_h > 0 ? logical_h : b.h;
	pthread_mutex_lock(&vmx);
	// The emulator is the clock ("fastest possible" CPU fills whatever time a frame leaves, and
	// sound is produced in real time), so it does not wait for the frontend: a frame that was
	// not fetched yet is replaced by this one. Only when the frontend has stopped asking for
	// frames (its menu, pause) does the emulator wait here - which is what pauses it.
	if (ready_new && !quitting) {
		struct timespec ts;
		deadline(&ts, 2);
		pthread_cond_timedwait(&cond_taken, &vmx, &ts);
		while (ready_new && !quitting && mono_ms() - last_poll_ms > 300) {
			deadline(&ts, 100);
			pthread_cond_timedwait(&cond_taken, &vmx, &ts);
		}
	}
	present_count++;
	const int t = back; back = ready; ready = t;
	ready_new = true;
	pthread_cond_broadcast(&cond_frame);
	pthread_mutex_unlock(&vmx);
}

int lr_SDL_RenderReadPixels(SDL_Renderer*, const SDL_Rect*, Uint32 format, void* pixels, int pitch)
{
	const Buf& b = bufs[back];
	if (!b.pix || fclass(format) != F_ARGB) return -1;
	for (int y = 0; y < b.h; y++) {
		Uint32* d = reinterpret_cast<Uint32*>(static_cast<Uint8*>(pixels) + y * pitch);
		for (int x = 0; x < b.w; x++) {
			int r, g, bl;
			unpack_out(b.pix + y * b.pitch + x * out_bpp, r, g, bl);
			d[x] = 0xff000000u | (r << 16) | (g << 8) | bl;
		}
	}
	return 0;
}

SDL_Texture* lr_SDL_CreateTexture(SDL_Renderer*, Uint32 format, int, int w, int h)
{
	Tex* t = static_cast<Tex*>(calloc(1, sizeof(Tex)));
	t->format = format; t->w = w; t->h = h;
	t->bpp = SDL_BYTESPERPIXEL(format);
	if (t->bpp != 2 && t->bpp != 4) t->bpp = 4;
	t->has_alpha = falpha(format);
	t->blend = SDL_BLENDMODE_NONE;
	return reinterpret_cast<SDL_Texture*>(t);
}

SDL_Texture* lr_SDL_CreateTextureFromSurface(SDL_Renderer* r, SDL_Surface* s)
{
	if (!s) return nullptr;
	SDL_Surface* c = SDL_ConvertSurfaceFormat(s, SDL_PIXELFORMAT_ARGB8888, 0);
	if (!c) return nullptr;
	Tex* t = T(lr_SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, 0, c->w, c->h));
	t->own_pitch = c->w * 4;
	t->own = static_cast<Uint8*>(malloc(t->own_pitch * c->h));
	for (int y = 0; y < c->h; y++)
		memcpy(t->own + y * t->own_pitch, static_cast<Uint8*>(c->pixels) + y * c->pitch, t->own_pitch);
	// as SDL does: a surface with an alpha channel gives a blended texture
	t->has_alpha = s->format->Amask != 0;
	t->blend = t->has_alpha ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE;
	SDL_FreeSurface(c);
	return reinterpret_cast<SDL_Texture*>(t);
}

void lr_SDL_DestroyTexture(SDL_Texture* tex)
{
	Tex* t = T(tex);
	if (!t) return;
	free(t->own);
	free(t);
}

int lr_SDL_UpdateTexture(SDL_Texture* tex, const SDL_Rect* rect, const void* pixels, int pitch)
{
	Tex* t = T(tex);
	if (!t || !pixels) return -1;
	if (!rect) {                // whole texture: keep a reference, it is copied when drawn
		t->ref = static_cast<const Uint8*>(pixels);
		t->ref_pitch = pitch;
		return 0;
	}
	if (!t->own) { t->own_pitch = t->w * t->bpp; t->own = static_cast<Uint8*>(calloc(1, t->own_pitch * t->h)); }
	t->ref = nullptr;
	for (int y = 0; y < rect->h && rect->y + y < t->h; y++)
		memcpy(t->own + (rect->y + y) * t->own_pitch + rect->x * t->bpp, static_cast<const Uint8*>(pixels) + y * pitch, rect->w * t->bpp);
	return 0;
}

int lr_SDL_LockTexture(SDL_Texture* tex, const SDL_Rect*, void** pixels, int* pitch)
{
	Tex* t = T(tex);
	if (!t) return -1;
	if (!t->own) { t->own_pitch = t->w * t->bpp; t->own = static_cast<Uint8*>(calloc(1, t->own_pitch * t->h)); }
	t->ref = nullptr;
	*pixels = t->own; *pitch = t->own_pitch;
	return 0;
}
void lr_SDL_UnlockTexture(SDL_Texture*) {}
int lr_SDL_QueryTexture(SDL_Texture* tex, Uint32* format, int* access, int* w, int* h)
{
	Tex* t = T(tex);
	if (!t) return -1;
	if (format) *format = t->format;
	if (access) *access = SDL_TEXTUREACCESS_STREAMING;
	if (w) *w = t->w;
	if (h) *h = t->h;
	return 0;
}
int lr_SDL_SetTextureBlendMode(SDL_Texture* tex, SDL_BlendMode mode) { if (T(tex)) T(tex)->blend = mode; return 0; }

// ------------------------------------------------------------------------------------
// events, keyboard, mouse, pads
// ------------------------------------------------------------------------------------
namespace {

#define EVQ 512
SDL_Event evq[EVQ];
int ev_head, ev_tail;
pthread_mutex_t emx = PTHREAD_MUTEX_INITIALIZER;
Uint8 keystate[SDL_NUM_SCANCODES];
int mouse_x = 400, mouse_y = 300;
Uint32 mouse_buttons;

volatile unsigned pad_buttons[2];
volatile unsigned pad_fake;
volatile Sint16 pad_axes[2][4];
unsigned pad_prev[2];
volatile int pads_connected = 1;
volatile unsigned hide_mask;      // pad 1 buttons the emulator's own joystick code must not see
volatile int hide_stick;          // pad 1 left stick is the mouse: not a joystick axis
int fake_cursor;

void enqueue(const SDL_Event* e)
{
	pthread_mutex_lock(&emx);
	const int n = (ev_tail + 1) % EVQ;
	if (n != ev_head) { evq[ev_tail] = *e; ev_tail = n; }
	pthread_mutex_unlock(&emx);
}

// pad state -> SDL joystick button events (the settings GUI navigates on those)
void pump()
{
	for (int p = 0; p < 2; p++) {
		const unsigned now = pad_buttons[p], diff = now ^ pad_prev[p];
		if (!diff) continue;
		for (int b = 0; b < LR_NBUTTONS; b++)
			if (diff & (1u << b)) {
				SDL_Event e;
				memset(&e, 0, sizeof e);
				e.type = (now >> b) & 1 ? SDL_JOYBUTTONDOWN : SDL_JOYBUTTONUP;
				e.jbutton.which = p;
				e.jbutton.button = (Uint8)b;
				e.jbutton.state = (now >> b) & 1 ? SDL_PRESSED : SDL_RELEASED;
				enqueue(&e);
			}
		pad_prev[p] = now;
	}
}

} // namespace

SDL_Keycode lr_keycode(SDL_Scancode sc)
{
	// what SDL's default (US) keymap returns - SDL's own table is only filled by SDL_VideoInit
	if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z) return 'a' + (sc - SDL_SCANCODE_A);
	if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9) return '1' + (sc - SDL_SCANCODE_1);
	switch (sc) {
	case SDL_SCANCODE_0: return '0';
	case SDL_SCANCODE_RETURN: return SDLK_RETURN;
	case SDL_SCANCODE_ESCAPE: return SDLK_ESCAPE;
	case SDL_SCANCODE_BACKSPACE: return SDLK_BACKSPACE;
	case SDL_SCANCODE_TAB: return SDLK_TAB;
	case SDL_SCANCODE_SPACE: return SDLK_SPACE;
	case SDL_SCANCODE_MINUS: return '-';
	case SDL_SCANCODE_EQUALS: return '=';
	case SDL_SCANCODE_LEFTBRACKET: return '[';
	case SDL_SCANCODE_RIGHTBRACKET: return ']';
	case SDL_SCANCODE_BACKSLASH: return '\\';
	case SDL_SCANCODE_NONUSHASH: return '#';
	case SDL_SCANCODE_SEMICOLON: return ';';
	case SDL_SCANCODE_APOSTROPHE: return '\'';
	case SDL_SCANCODE_GRAVE: return '`';
	case SDL_SCANCODE_COMMA: return ',';
	case SDL_SCANCODE_PERIOD: return '.';
	case SDL_SCANCODE_SLASH: return '/';
	case SDL_SCANCODE_DELETE: return SDLK_DELETE;
	case SDL_SCANCODE_UNKNOWN: return SDLK_UNKNOWN;
	default: return SDL_SCANCODE_TO_KEYCODE(sc);
	}
}

SDL_Keycode lr_SDL_GetKeyFromScancode(SDL_Scancode sc) { return lr_keycode(sc); }

SDL_Keycode lr_SDL_GetKeyFromName(const char* name)
{
	if (!name || !*name) return SDLK_UNKNOWN;
	if (!name[1]) {
		const char c = name[0];
		return c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c;
	}
	return lr_keycode(SDL_GetScancodeFromName(name));
}

int lr_SDL_PollEvent(SDL_Event* e)
{
	pump();
	int got = 0;
	pthread_mutex_lock(&emx);
	if (ev_head != ev_tail) {
		if (e) { *e = evq[ev_head]; ev_head = (ev_head + 1) % EVQ; }
		got = 1;
	}
	pthread_mutex_unlock(&emx);
	return got;
}

int lr_SDL_PushEvent(SDL_Event* e)
{
	if (e->type == SDL_KEYDOWN || e->type == SDL_KEYUP) {
		const int sc = e->key.keysym.scancode;
		if (sc > 0 && sc < SDL_NUM_SCANCODES) keystate[sc] = e->type == SDL_KEYDOWN;
		if (!e->key.keysym.sym) e->key.keysym.sym = lr_keycode(e->key.keysym.scancode);
		Uint16 mod = 0;
		if (keystate[SDL_SCANCODE_LSHIFT]) mod |= KMOD_LSHIFT;
		if (keystate[SDL_SCANCODE_RSHIFT]) mod |= KMOD_RSHIFT;
		if (keystate[SDL_SCANCODE_LCTRL]) mod |= KMOD_LCTRL;
		if (keystate[SDL_SCANCODE_LALT]) mod |= KMOD_LALT;
		if (keystate[SDL_SCANCODE_RALT]) mod |= KMOD_RALT;
		e->key.keysym.mod = mod;
	}
	else if (e->type == SDL_MOUSEMOTION) { mouse_x = e->motion.x; mouse_y = e->motion.y; }
	enqueue(e);
	return 1;
}

void lr_key_event(int scancode, int down)
{
	SDL_Event e;
	memset(&e, 0, sizeof e);
	e.type = down ? SDL_KEYDOWN : SDL_KEYUP;
	e.key.state = down ? SDL_PRESSED : SDL_RELEASED;
	e.key.keysym.scancode = static_cast<SDL_Scancode>(scancode);
	e.key.keysym.sym = lr_keycode(e.key.keysym.scancode);
	e.key.windowID = 1;
	lr_SDL_PushEvent(&e);
}

void lr_mouse_event(int dx, int dy, int buttons)
{
	SDL_Event e;
	if (dx || dy) {
		memset(&e, 0, sizeof e);
		e.type = SDL_MOUSEMOTION;
		e.motion.windowID = 1;
		int x = mouse_x + dx, y = mouse_y + dy;
		if (x < 0) x = 0;
		if (y < 0) y = 0;
		if (x >= bw) x = bw - 1;
		if (y >= bh) y = bh - 1;
		e.motion.x = x; e.motion.y = y; e.motion.xrel = dx; e.motion.yrel = dy;
		lr_SDL_PushEvent(&e);
	}
	static const Uint8 sdl_btn[3] = { SDL_BUTTON_LEFT, SDL_BUTTON_RIGHT, SDL_BUTTON_MIDDLE };
	for (int b = 0; b < 3; b++)
		if ((buttons ^ mouse_buttons) & (1u << b)) {
			const bool down = (buttons >> b) & 1;
			memset(&e, 0, sizeof e);
			e.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
			e.button.windowID = 1;
			e.button.button = sdl_btn[b];
			e.button.state = down ? SDL_PRESSED : SDL_RELEASED;
			e.button.clicks = 1;
			e.button.x = mouse_x; e.button.y = mouse_y;
			lr_SDL_PushEvent(&e);
		}
	mouse_buttons = buttons;
}

void lr_pad_set(int pad, unsigned buttons, const Sint16 axes[4])
{
	if (pad < 0 || pad > 1) return;
	pad_buttons[pad] = buttons | (pad == 0 ? pad_fake : 0);
	for (int i = 0; i < 4; i++) pad_axes[pad][i] = axes[i];
}
unsigned lr_raw_buttons(int pad) { return pad >= 0 && pad < 2 ? pad_buttons[pad] : 0; }
Sint16 lr_raw_axis(int pad, int axis) { return pad >= 0 && pad < 2 && axis >= 0 && axis < 4 ? pad_axes[pad][axis] : 0; }
// test hook (remote control file): a pad 1 button held down by software
void lr_fake_button(int button, int down)
{
	if (button < 0 || button >= LR_NBUTTONS) return;
	if (down) pad_fake |= 1u << button; else pad_fake &= ~(1u << button);
}
void lr_emu_view(unsigned hide_buttons, int stick_hidden) { hide_mask = hide_buttons; hide_stick = stick_hidden; }
void lr_pads_connected(int n) { pads_connected = n < 1 ? 1 : (n > 2 ? 2 : n); }

SDL_Cursor* lr_SDL_CreateColorCursor(SDL_Surface*, int, int) { return reinterpret_cast<SDL_Cursor*>(&fake_cursor); }
SDL_Cursor* lr_SDL_CreateSystemCursor(SDL_SystemCursor) { return reinterpret_cast<SDL_Cursor*>(&fake_cursor); }
void lr_SDL_FreeCursor(SDL_Cursor*) {}
void lr_SDL_SetCursor(SDL_Cursor*) {}
int lr_SDL_ShowCursor(int) { return 0; }
void lr_SDL_WarpMouseInWindow(SDL_Window*, int x, int y)
{
	SDL_Event e;
	memset(&e, 0, sizeof e);
	e.type = SDL_MOUSEMOTION;
	e.motion.windowID = 1;
	e.motion.x = x; e.motion.y = y;
	lr_SDL_PushEvent(&e);
}
int lr_SDL_SetRelativeMouseMode(SDL_bool) { return 0; }
Uint32 lr_SDL_GetMouseState(int* x, int* y) { if (x) *x = mouse_x; if (y) *y = mouse_y; return 0; }
const Uint8* lr_SDL_GetKeyboardState(int* numkeys) { if (numkeys) *numkeys = SDL_NUM_SCANCODES; return keystate; }
SDL_bool lr_SDL_HasClipboardText(void) { return SDL_FALSE; }
char* lr_SDL_GetClipboardText(void) { return SDL_strdup(""); }
int lr_SDL_SetClipboardText(const char*) { return 0; }

int lr_SDL_NumJoysticks(void) { return pads_connected; }
const char* lr_SDL_JoystickNameForIndex(int index) { return index == 0 ? "TurboUAE RetroPad" : "TurboUAE RetroPad 2"; }
SDL_Joystick* lr_SDL_JoystickOpen(int index) { return index >= 0 && index < 2 ? reinterpret_cast<SDL_Joystick*>(static_cast<intptr_t>(index + 1)) : nullptr; }
void lr_SDL_JoystickClose(SDL_Joystick*) {}
int lr_SDL_JoystickNumAxes(SDL_Joystick*) { return 4; }
int lr_SDL_JoystickNumButtons(SDL_Joystick*) { return LR_NBUTTONS; }
Sint16 lr_SDL_JoystickGetAxis(SDL_Joystick* j, int axis)
{
	const intptr_t p = reinterpret_cast<intptr_t>(j) - 1;
	if (p == 0 && hide_stick && axis < 2) return 0;
	return p >= 0 && p < 2 && axis >= 0 && axis < 4 ? pad_axes[p][axis] : 0;
}
Uint8 lr_SDL_JoystickGetButton(SDL_Joystick* j, int button)
{
	const intptr_t p = reinterpret_cast<intptr_t>(j) - 1;
	if (p < 0 || p > 1 || button < 0 || button >= LR_NBUTTONS) return 0;
	const unsigned b = p == 0 ? pad_buttons[0] & ~hide_mask : pad_buttons[p];
	return (b >> button) & 1;
}
Uint8 lr_SDL_JoystickGetHat(SDL_Joystick*, int) { return SDL_HAT_CENTERED; }

// ------------------------------------------------------------------------------------
// audio: device 1 = Paula, device 2 = CD audio; 16 bit stereo rings mixed by retro_run()
// ------------------------------------------------------------------------------------
namespace {

struct AQ { Sint16* data; int cap, head, count, channels, freq; bool open; };
AQ aq[3];
pthread_mutex_t amx = PTHREAD_MUTEX_INITIALIZER;

void aq_get(AQ& q, Sint16* out, int frames)      // caller holds amx and checked count
{
	for (int i = 0; i < frames; i++) {
		out[i * 2] = q.data[q.head * 2];
		out[i * 2 + 1] = q.data[q.head * 2 + 1];
		q.head = (q.head + 1) % q.cap;
	}
	q.count -= frames;
}

} // namespace

int lr_audio_rate(void) { return 44100; }

SDL_AudioDeviceID lr_SDL_OpenAudioDevice(const char*, int, const SDL_AudioSpec* want, SDL_AudioSpec* have, int)
{
	pthread_mutex_lock(&amx);
	int id = 0;
	for (int i = 1; i <= 2 && !id; i++) if (!aq[i].open) id = i;
	if (id) {
		AQ& q = aq[id];
		q.cap = want->freq * (id == 1 ? 1 : 4);
		q.data = static_cast<Sint16*>(realloc(q.data, q.cap * 2 * sizeof(Sint16)));
		q.head = q.count = 0;
		q.channels = want->channels == 1 ? 1 : 2;
		q.freq = want->freq;
		q.open = true;
	}
	pthread_mutex_unlock(&amx);
	if (have) { *have = *want; have->size = want->samples * want->channels * 2; }
	return id;
}

void lr_SDL_CloseAudioDevice(SDL_AudioDeviceID dev)
{
	if (dev < 1 || dev > 2) return;
	pthread_mutex_lock(&amx);
	aq[dev].open = false;
	aq[dev].count = 0;
	pthread_mutex_unlock(&amx);
}
void lr_SDL_PauseAudioDevice(SDL_AudioDeviceID, int) {}
void lr_SDL_LockAudioDevice(SDL_AudioDeviceID) {}
void lr_SDL_UnlockAudioDevice(SDL_AudioDeviceID) {}

int lr_SDL_QueueAudio(SDL_AudioDeviceID dev, const void* data, Uint32 len)
{
	if (dev < 1 || dev > 2) return -1;
	pthread_mutex_lock(&amx);
	AQ& q = aq[dev];
	if (q.open && q.data) {
		const Sint16* s = static_cast<const Sint16*>(data);
		const int frames = len / (2 * q.channels);
		for (int i = 0; i < frames; i++) {
			if (q.count == q.cap) { q.head = (q.head + 1) % q.cap; q.count--; }     // full: oldest goes
			const int w = (q.head + q.count) % q.cap;
			q.data[w * 2] = s[i * q.channels];
			q.data[w * 2 + 1] = s[i * q.channels + (q.channels == 2 ? 1 : 0)];
			q.count++;
		}
	}
	pthread_mutex_unlock(&amx);
	return 0;
}

Uint32 lr_SDL_GetQueuedAudioSize(SDL_AudioDeviceID dev)
{
	if (dev < 1 || dev > 2) return 0;
	pthread_mutex_lock(&amx);
	const Uint32 n = aq[dev].open ? aq[dev].count * 2 * aq[dev].channels : 0;
	pthread_mutex_unlock(&amx);
	return n;
}

int lr_audio_take(Sint16* out, int max_frames)
{
	pthread_mutex_lock(&amx);
	AQ& pa = aq[1];
	AQ& cd = aq[2];
	int n = pa.open ? pa.count : (cd.open ? cd.count : 0);
	if (n > max_frames) n = max_frames;
	if (n > 0) {
		if (pa.open) aq_get(pa, out, n); else memset(out, 0, n * 2 * sizeof(Sint16));
		if (cd.open && cd.count > 0) {
			const int m = cd.count < n ? cd.count : n;
			for (int i = 0; i < m * 2; i++) {
				int v = out[i] + cd.data[cd.head * 2 + (i & 1)];
				if (i & 1) cd.head = (cd.head + 1) % cd.cap;
				out[i] = (Sint16)(v > 32767 ? 32767 : (v < -32768 ? -32768 : v));
			}
			cd.count -= m;
		}
	}
	pthread_mutex_unlock(&amx);
	return n;
}
