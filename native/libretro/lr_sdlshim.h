// TurboUAE libretro core: SDL shim.
//
// The emulator (Amiberry v3.3) talks to SDL2 for its window, renderer, textures, audio
// device, joysticks and event queue. Inside RetroArch none of those may be touched - the
// frontend owns the screen, the sound card and the pad. This header is force-included
// (-include) into every C++ file of the core and renames exactly those SDL functions to
// lr_SDL_* versions implemented in lr_sdlshim.cpp:
//   window/renderer/texture  -> a small software renderer whose "present" hands the frame
//                               to retro_run() (video_refresh)
//   audio device             -> a ring buffer drained by retro_run() (audio_sample_batch)
//   joystick                 -> virtual pads fed from the RetroPad (R36S button numbering)
//   event queue              -> our own queue (keyboard callback, pad, on-screen keyboard)
// Everything else in SDL (surfaces, threads, timers, SDL_ttf, SDL_image) is used as is; none
// of it needs SDL_Init, which is never called.
#ifndef LR_SDLSHIM_H
#define LR_SDLSHIM_H
#ifdef __cplusplus
#include <SDL.h>

int lr_SDL_Init(Uint32 flags);
void lr_SDL_Quit(void);
void lr_SDL_VideoQuit(void);

SDL_Window* lr_SDL_CreateWindow(const char* title, int x, int y, int w, int h, Uint32 flags);
void lr_SDL_DestroyWindow(SDL_Window* w);
Uint32 lr_SDL_GetWindowFlags(SDL_Window* w);
Uint32 lr_SDL_GetWindowID(SDL_Window* w);
void lr_SDL_GetWindowSize(SDL_Window* win, int* w, int* h);
void lr_SDL_SetWindowSize(SDL_Window* win, int w, int h);
int lr_SDL_SetWindowFullscreen(SDL_Window* win, Uint32 flags);
void lr_SDL_SetWindowIcon(SDL_Window* win, SDL_Surface* icon);
void lr_SDL_RaiseWindow(SDL_Window* win);
int lr_SDL_GetCurrentDisplayMode(int index, SDL_DisplayMode* mode);
const char* lr_SDL_GetCurrentVideoDriver(void);

SDL_Renderer* lr_SDL_CreateRenderer(SDL_Window* win, int index, Uint32 flags);
void lr_SDL_DestroyRenderer(SDL_Renderer* r);
int lr_SDL_GetRendererOutputSize(SDL_Renderer* r, int* w, int* h);
int lr_SDL_RenderSetLogicalSize(SDL_Renderer* r, int w, int h);
void lr_SDL_RenderGetLogicalSize(SDL_Renderer* r, int* w, int* h);
int lr_SDL_RenderSetScale(SDL_Renderer* r, float sx, float sy);
int lr_SDL_RenderSetViewport(SDL_Renderer* r, const SDL_Rect* rect);
int lr_SDL_RenderSetClipRect(SDL_Renderer* r, const SDL_Rect* rect);
int lr_SDL_SetRenderDrawColor(SDL_Renderer* r, Uint8 cr, Uint8 cg, Uint8 cb, Uint8 ca);
int lr_SDL_GetRenderDrawColor(SDL_Renderer* r, Uint8* cr, Uint8* cg, Uint8* cb, Uint8* ca);
int lr_SDL_SetRenderDrawBlendMode(SDL_Renderer* r, SDL_BlendMode mode);
int lr_SDL_SetRenderTarget(SDL_Renderer* r, SDL_Texture* t);
int lr_SDL_RenderClear(SDL_Renderer* r);
int lr_SDL_RenderCopy(SDL_Renderer* r, SDL_Texture* t, const SDL_Rect* src, const SDL_Rect* dst);
int lr_SDL_RenderCopyEx(SDL_Renderer* r, SDL_Texture* t, const SDL_Rect* src, const SDL_Rect* dst,
	double angle, const SDL_Point* center, SDL_RendererFlip flip);
int lr_SDL_RenderFillRect(SDL_Renderer* r, const SDL_Rect* rect);
int lr_SDL_RenderDrawRect(SDL_Renderer* r, const SDL_Rect* rect);
int lr_SDL_RenderDrawLine(SDL_Renderer* r, int x1, int y1, int x2, int y2);
int lr_SDL_RenderDrawPoint(SDL_Renderer* r, int x, int y);
void lr_SDL_RenderPresent(SDL_Renderer* r);
int lr_SDL_RenderReadPixels(SDL_Renderer* r, const SDL_Rect* rect, Uint32 format, void* pixels, int pitch);

SDL_Texture* lr_SDL_CreateTexture(SDL_Renderer* r, Uint32 format, int access, int w, int h);
SDL_Texture* lr_SDL_CreateTextureFromSurface(SDL_Renderer* r, SDL_Surface* s);
void lr_SDL_DestroyTexture(SDL_Texture* t);
int lr_SDL_UpdateTexture(SDL_Texture* t, const SDL_Rect* rect, const void* pixels, int pitch);
int lr_SDL_LockTexture(SDL_Texture* t, const SDL_Rect* rect, void** pixels, int* pitch);
void lr_SDL_UnlockTexture(SDL_Texture* t);
int lr_SDL_QueryTexture(SDL_Texture* t, Uint32* format, int* access, int* w, int* h);
int lr_SDL_SetTextureBlendMode(SDL_Texture* t, SDL_BlendMode mode);

SDL_Cursor* lr_SDL_CreateColorCursor(SDL_Surface* s, int hx, int hy);
SDL_Cursor* lr_SDL_CreateSystemCursor(SDL_SystemCursor id);
void lr_SDL_FreeCursor(SDL_Cursor* c);
void lr_SDL_SetCursor(SDL_Cursor* c);
int lr_SDL_ShowCursor(int toggle);
void lr_SDL_WarpMouseInWindow(SDL_Window* win, int x, int y);
int lr_SDL_SetRelativeMouseMode(SDL_bool enabled);
Uint32 lr_SDL_GetMouseState(int* x, int* y);
const Uint8* lr_SDL_GetKeyboardState(int* numkeys);
SDL_bool lr_SDL_HasClipboardText(void);
char* lr_SDL_GetClipboardText(void);
int lr_SDL_SetClipboardText(const char* text);

int lr_SDL_NumJoysticks(void);
const char* lr_SDL_JoystickNameForIndex(int index);
SDL_Joystick* lr_SDL_JoystickOpen(int index);
void lr_SDL_JoystickClose(SDL_Joystick* j);
int lr_SDL_JoystickNumAxes(SDL_Joystick* j);
int lr_SDL_JoystickNumButtons(SDL_Joystick* j);
Sint16 lr_SDL_JoystickGetAxis(SDL_Joystick* j, int axis);
Uint8 lr_SDL_JoystickGetButton(SDL_Joystick* j, int button);
Uint8 lr_SDL_JoystickGetHat(SDL_Joystick* j, int hat);

SDL_Keycode lr_keycode(SDL_Scancode sc);
SDL_Keycode lr_SDL_GetKeyFromScancode(SDL_Scancode sc);
SDL_Keycode lr_SDL_GetKeyFromName(const char* name);
int lr_SDL_PollEvent(SDL_Event* e);
int lr_SDL_PushEvent(SDL_Event* e);

SDL_AudioDeviceID lr_SDL_OpenAudioDevice(const char* device, int iscapture, const SDL_AudioSpec* want,
	SDL_AudioSpec* have, int allowed_changes);
void lr_SDL_CloseAudioDevice(SDL_AudioDeviceID dev);
void lr_SDL_PauseAudioDevice(SDL_AudioDeviceID dev, int pause_on);
void lr_SDL_LockAudioDevice(SDL_AudioDeviceID dev);
void lr_SDL_UnlockAudioDevice(SDL_AudioDeviceID dev);
int lr_SDL_QueueAudio(SDL_AudioDeviceID dev, const void* data, Uint32 len);
Uint32 lr_SDL_GetQueuedAudioSize(SDL_AudioDeviceID dev);

#ifndef LR_SDLSHIM_IMPLEMENTATION
#define SDL_Init lr_SDL_Init
#define SDL_Quit lr_SDL_Quit
#define SDL_VideoQuit lr_SDL_VideoQuit
#define SDL_CreateWindow lr_SDL_CreateWindow
#define SDL_DestroyWindow lr_SDL_DestroyWindow
#define SDL_GetWindowFlags lr_SDL_GetWindowFlags
#define SDL_GetWindowID lr_SDL_GetWindowID
#define SDL_GetWindowSize lr_SDL_GetWindowSize
#define SDL_SetWindowSize lr_SDL_SetWindowSize
#define SDL_SetWindowFullscreen lr_SDL_SetWindowFullscreen
#define SDL_SetWindowIcon lr_SDL_SetWindowIcon
#define SDL_RaiseWindow lr_SDL_RaiseWindow
#define SDL_GetCurrentDisplayMode lr_SDL_GetCurrentDisplayMode
#define SDL_GetCurrentVideoDriver lr_SDL_GetCurrentVideoDriver
#define SDL_CreateRenderer lr_SDL_CreateRenderer
#define SDL_DestroyRenderer lr_SDL_DestroyRenderer
#define SDL_GetRendererOutputSize lr_SDL_GetRendererOutputSize
#define SDL_RenderSetLogicalSize lr_SDL_RenderSetLogicalSize
#define SDL_RenderGetLogicalSize lr_SDL_RenderGetLogicalSize
#define SDL_RenderSetScale lr_SDL_RenderSetScale
#define SDL_RenderSetViewport lr_SDL_RenderSetViewport
#define SDL_RenderSetClipRect lr_SDL_RenderSetClipRect
#define SDL_SetRenderDrawColor lr_SDL_SetRenderDrawColor
#define SDL_GetRenderDrawColor lr_SDL_GetRenderDrawColor
#define SDL_SetRenderDrawBlendMode lr_SDL_SetRenderDrawBlendMode
#define SDL_SetRenderTarget lr_SDL_SetRenderTarget
#define SDL_RenderClear lr_SDL_RenderClear
#define SDL_RenderCopy lr_SDL_RenderCopy
#define SDL_RenderCopyEx lr_SDL_RenderCopyEx
#define SDL_RenderFillRect lr_SDL_RenderFillRect
#define SDL_RenderDrawRect lr_SDL_RenderDrawRect
#define SDL_RenderDrawLine lr_SDL_RenderDrawLine
#define SDL_RenderDrawPoint lr_SDL_RenderDrawPoint
#define SDL_RenderPresent lr_SDL_RenderPresent
#define SDL_RenderReadPixels lr_SDL_RenderReadPixels
#define SDL_CreateTexture lr_SDL_CreateTexture
#define SDL_CreateTextureFromSurface lr_SDL_CreateTextureFromSurface
#define SDL_DestroyTexture lr_SDL_DestroyTexture
#define SDL_UpdateTexture lr_SDL_UpdateTexture
#define SDL_LockTexture lr_SDL_LockTexture
#define SDL_UnlockTexture lr_SDL_UnlockTexture
#define SDL_QueryTexture lr_SDL_QueryTexture
#define SDL_SetTextureBlendMode lr_SDL_SetTextureBlendMode
#define SDL_CreateColorCursor lr_SDL_CreateColorCursor
#define SDL_CreateSystemCursor lr_SDL_CreateSystemCursor
#define SDL_FreeCursor lr_SDL_FreeCursor
#define SDL_SetCursor lr_SDL_SetCursor
#define SDL_ShowCursor lr_SDL_ShowCursor
#define SDL_WarpMouseInWindow lr_SDL_WarpMouseInWindow
#define SDL_SetRelativeMouseMode lr_SDL_SetRelativeMouseMode
#define SDL_GetMouseState lr_SDL_GetMouseState
#define SDL_GetKeyboardState lr_SDL_GetKeyboardState
#define SDL_HasClipboardText lr_SDL_HasClipboardText
#define SDL_GetClipboardText lr_SDL_GetClipboardText
#define SDL_SetClipboardText lr_SDL_SetClipboardText
#define SDL_NumJoysticks lr_SDL_NumJoysticks
#define SDL_JoystickNameForIndex lr_SDL_JoystickNameForIndex
#define SDL_JoystickOpen lr_SDL_JoystickOpen
#define SDL_JoystickClose lr_SDL_JoystickClose
#define SDL_JoystickNumAxes lr_SDL_JoystickNumAxes
#define SDL_JoystickNumButtons lr_SDL_JoystickNumButtons
#define SDL_JoystickGetAxis lr_SDL_JoystickGetAxis
#define SDL_JoystickGetButton lr_SDL_JoystickGetButton
#define SDL_JoystickGetHat lr_SDL_JoystickGetHat
#define SDL_GetKeyFromScancode lr_SDL_GetKeyFromScancode
#define SDL_GetKeyFromName lr_SDL_GetKeyFromName
#define SDL_PollEvent lr_SDL_PollEvent
#define SDL_PushEvent lr_SDL_PushEvent
#define SDL_OpenAudioDevice lr_SDL_OpenAudioDevice
#define SDL_CloseAudioDevice lr_SDL_CloseAudioDevice
#define SDL_PauseAudioDevice lr_SDL_PauseAudioDevice
#define SDL_LockAudioDevice lr_SDL_LockAudioDevice
#define SDL_UnlockAudioDevice lr_SDL_UnlockAudioDevice
#define SDL_QueueAudio lr_SDL_QueueAudio
#define SDL_GetQueuedAudioSize lr_SDL_GetQueuedAudioSize
#endif

// ---- interface between the shim (emulator thread) and the libretro core (frontend thread)
struct lr_frame { const void* pixels; int w, h, pitch; int lw, lh; };

// video: 1 = a new frame was taken (valid until the next call), 0 = nothing new within the wait
int lr_video_take(struct lr_frame* out, int wait_ms);
void lr_video_set_format(int bytes_per_pixel);       // 2 = RGB565 (default), 4 = XRGB8888
int lr_video_bpp(void);
Uint32 lr_video_sdl_format(void);                    // the SDL pixel format of the output
void lr_wake_all(int quitting);                      // releases an emulator blocked in present

// audio: takes up to max_frames stereo frames (44.1/48 kHz as opened), returns the count
int lr_audio_take(Sint16* out, int max_frames);
int lr_audio_rate(void);

// input: state of the virtual pads, written by retro_run(), read by the emulator thread.
// Buttons use the R36S / GO-Super Gamepad numbering the port was written for.
enum { LR_B = 0, LR_A, LR_X, LR_Y, LR_L1, LR_R1, LR_L2, LR_R2, LR_UP, LR_DOWN, LR_LEFT, LR_RIGHT,
	LR_SELECT, LR_START, LR_L3, LR_R3, LR_NBUTTONS };
void lr_pad_set(int pad, unsigned buttons, const Sint16 axes[4]);
void lr_pads_connected(int n);
void lr_key_event(int scancode, int down);            // SDL scancode
void lr_mouse_event(int dx, int dy, int buttons);     // RETRO_DEVICE_MOUSE, bit0 left, 1 right, 2 middle
#endif
#endif
