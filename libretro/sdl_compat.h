/*
 * SDL Compatibility Layer for libretro
 *
 * This header provides stub implementations of SDL functions
 * used by DCaSTaway, redirecting them to libretro equivalents.
 */

#ifndef SDL_COMPAT_H
#define SDL_COMPAT_H

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Basic SDL types */
typedef uint8_t  Uint8;
typedef uint16_t Uint16;
typedef uint32_t Uint32;
typedef uint64_t Uint64;
typedef int8_t   Sint8;
typedef int16_t  Sint16;
typedef int32_t  Sint32;
typedef int64_t  Sint64;

/* SDL boolean */
#define SDL_TRUE  1
#define SDL_FALSE 0

/* SDL Surface - minimal stub */
typedef struct SDL_Surface {
    int w;
    int h;
    int pitch;
    void *pixels;
    void *format;  /* Not used */
} SDL_Surface;

/* Rect structure */
typedef struct SDL_Rect {
    int x, y;
    int w, h;
} SDL_Rect;

/* Timer functions - redirect to libretro frame counting */
/* In SF2000 libretro, we count frames at ~50fps */
extern volatile uint32_t libretro_frame_count;

static inline Uint32 SDL_GetTicks(void)
{
    /* Convert frames to milliseconds (50 fps = 20ms per frame) */
    return libretro_frame_count * 20;
}

static inline void SDL_Delay(Uint32 ms)
{
    /* No-op in libretro - timing is handled by frontend */
    (void)ms;
}

/* Video functions - stubs, rendering handled by libretro */
static inline void SDL_UpdateRect(SDL_Surface *screen, int x, int y, int w, int h)
{
    (void)screen; (void)x; (void)y; (void)w; (void)h;
}

static inline void SDL_Flip(SDL_Surface *screen)
{
    (void)screen;
}

static inline int SDL_FillRect(SDL_Surface *dst, SDL_Rect *rect, Uint32 color)
{
    /* Fill with color - basic implementation */
    if (!dst || !dst->pixels) return -1;

    int x0 = rect ? rect->x : 0;
    int y0 = rect ? rect->y : 0;
    int w = rect ? rect->w : dst->w;
    int h = rect ? rect->h : dst->h;

    uint16_t *pixels = (uint16_t *)dst->pixels;
    int pitch16 = dst->pitch / 2;

    for (int y = y0; y < y0 + h && y < dst->h; y++) {
        for (int x = x0; x < x0 + w && x < dst->w; x++) {
            pixels[y * pitch16 + x] = (uint16_t)color;
        }
    }
    return 0;
}

static inline Uint32 SDL_MapRGB(void *format, Uint8 r, Uint8 g, Uint8 b)
{
    (void)format;
    /* RGB565 conversion */
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}

#define SDL_MUSTLOCK(surface) (0)
static inline int SDL_LockSurface(SDL_Surface *surface) { (void)surface; return 0; }
static inline void SDL_UnlockSurface(SDL_Surface *surface) { (void)surface; }

/* Video mode flags - not used in libretro */
#define SDL_HWSURFACE   0x00000001
#define SDL_HWPALETTE   0x20000000
#define SDL_DOUBLEBUF   0x40000000
#define SDL_FULLSCREEN  0x80000000
#define SDL_SWSURFACE   0x00000000

static inline SDL_Surface *SDL_SetVideoMode(int width, int height, int bpp, Uint32 flags)
{
    (void)width; (void)height; (void)bpp; (void)flags;
    /* UAE4ALL pattern: return the global screen pointer
     * Screen is defined in libretro-castaway.cpp, declared extern in render.cpp
     * This ensures all code uses the same surface */
    extern SDL_Surface *screen;
    return screen;
}

/* Init/Quit */
#define SDL_INIT_VIDEO 0x00000020
#define SDL_INIT_AUDIO 0x00000010
#define SDL_INIT_TIMER 0x00000001
#define SDL_INIT_JOYSTICK 0x00000200

static inline int SDL_Init(Uint32 flags)
{
    (void)flags;
    return 0;
}

static inline int SDL_InitSubSystem(Uint32 flags)
{
    (void)flags;
    return 0;
}

static inline void SDL_QuitSubSystem(Uint32 flags)
{
    (void)flags;
}

static inline void SDL_Quit(void)
{
}

static inline const char *SDL_GetError(void)
{
    return "No error";
}

/* Audio - handled by libretro */
#define AUDIO_S16SYS 0x8010

typedef struct SDL_AudioSpec {
    int freq;
    Uint16 format;
    Uint8 channels;
    Uint8 silence;
    Uint16 samples;
    Uint32 size;
    void (*callback)(void *userdata, Uint8 *stream, int len);
    void *userdata;
} SDL_AudioSpec;

static inline int SDL_OpenAudio(SDL_AudioSpec *desired, SDL_AudioSpec *obtained)
{
    if (obtained) *obtained = *desired;
    return 0;
}

static inline void SDL_CloseAudio(void) {}
static inline void SDL_PauseAudio(int pause_on) { (void)pause_on; }
static inline void SDL_LockAudio(void) {}
static inline void SDL_UnlockAudio(void) {}
static inline Uint32 SDL_WasInit(Uint32 flags) { (void)flags; return 0; }

/* Audio formats for sound.cpp */
#define AUDIO_S16 0x8010
#define AUDIO_S8  0x8008

/* Key definitions */
typedef int SDLKey;
#define SDLK_LAST 512
#define SDL_ENABLE 1
#define SDL_DISABLE 0

/* SDL Key codes (subset used by Castaway) */
#define SDLK_UNKNOWN 0
#define SDLK_BACKSPACE 8
#define SDLK_TAB 9
#define SDLK_RETURN 13
#define SDLK_ESCAPE 27
#define SDLK_SPACE 32
#define SDLK_DELETE 127
#define SDLK_UP 273
#define SDLK_DOWN 274
#define SDLK_RIGHT 275
#define SDLK_LEFT 276
#define SDLK_INSERT 277
#define SDLK_HOME 278
#define SDLK_END 279
#define SDLK_PAGEUP 280
#define SDLK_PAGEDOWN 281
#define SDLK_F1 282
#define SDLK_F2 283
#define SDLK_F3 284
#define SDLK_F4 285
#define SDLK_F5 286
#define SDLK_F6 287
#define SDLK_F7 288
#define SDLK_F8 289
#define SDLK_F9 290
#define SDLK_F10 291
#define SDLK_F11 292
#define SDLK_F12 293
#define SDLK_NUMLOCK 300
#define SDLK_CAPSLOCK 301
#define SDLK_SCROLLOCK 302
#define SDLK_RSHIFT 303
#define SDLK_LSHIFT 304
#define SDLK_RCTRL 305
#define SDLK_LCTRL 306
#define SDLK_RALT 307
#define SDLK_LALT 308
#define SDLK_RMETA 309
#define SDLK_LMETA 310
#define SDLK_LSUPER 311
#define SDLK_RSUPER 312
#define SDLK_MODE 313
#define SDLK_COMPOSE 314

/* Modifier key masks */
#define KMOD_NONE 0x0000
#define KMOD_LSHIFT 0x0001
#define KMOD_RSHIFT 0x0002
#define KMOD_LCTRL 0x0040
#define KMOD_RCTRL 0x0080
#define KMOD_LALT 0x0100
#define KMOD_RALT 0x0200
#define KMOD_LMETA 0x0400
#define KMOD_RMETA 0x0800
#define KMOD_NUM 0x1000
#define KMOD_CAPS 0x2000
#define KMOD_MODE 0x4000

static inline int SDL_GetModState(void) { return 0; }

/* Mouse events */
typedef struct SDL_MouseMotionEvent {
    int x, y;
    int xrel, yrel;
} SDL_MouseMotionEvent;

typedef struct SDL_MouseButtonEvent {
    int button;
    int x, y;
} SDL_MouseButtonEvent;

typedef struct SDL_ResizeEvent {
    int w, h;
} SDL_ResizeEvent;

#define SDL_MOUSEMOTION 0x800
#define SDL_MOUSEBUTTONDOWN 0x801
#define SDL_MOUSEBUTTONUP 0x802
#define SDL_VIDEORESIZE 0x900

/* Events - handled by libretro input */
typedef struct SDL_JoyButtonEvent {
    int which;
    int button;
} SDL_JoyButtonEvent;

typedef struct SDL_JoyAxisEvent {
    int which;
    int axis;
    int value;
} SDL_JoyAxisEvent;

typedef struct SDL_JoyHatEvent {
    int which;
    int hat;
    int value;
} SDL_JoyHatEvent;

typedef struct SDL_KeyboardEvent {
    struct { SDLKey sym; } keysym;
} SDL_KeyboardEvent;

typedef struct SDL_Event {
    int type;
    SDL_JoyButtonEvent jbutton;
    SDL_JoyAxisEvent jaxis;
    SDL_JoyHatEvent jhat;
    SDL_KeyboardEvent key;
    SDL_MouseMotionEvent motion;
    SDL_MouseButtonEvent button;
    SDL_ResizeEvent resize;
} SDL_Event;

#define SDL_QUIT 0x100
#define SDL_KEYDOWN 0x300
#define SDL_KEYUP 0x301
#define SDL_JOYBUTTONDOWN 0x400
#define SDL_JOYBUTTONUP 0x401
#define SDL_JOYAXISMOTION 0x600
#define SDL_JOYHATMOTION 0x700

/* Hat values */
#define SDL_HAT_CENTERED 0x00
#define SDL_HAT_UP       0x01
#define SDL_HAT_RIGHT    0x02
#define SDL_HAT_DOWN     0x04
#define SDL_HAT_LEFT     0x08

static inline int SDL_PollEvent(SDL_Event *event)
{
    (void)event;
    return 0;  /* No SDL events - input handled by libretro */
}

static inline int SDL_JoystickEventState(int state) { (void)state; return 0; }

/* Joystick - not used, libretro handles input */
typedef struct SDL_Joystick SDL_Joystick;
static inline int SDL_NumJoysticks(void) { return 0; }
static inline SDL_Joystick *SDL_JoystickOpen(int index) { (void)index; return NULL; }
static inline void SDL_JoystickClose(SDL_Joystick *joystick) { (void)joystick; }
static inline Uint8 SDL_JoystickGetHat(SDL_Joystick *joystick, int hat) { (void)joystick; (void)hat; return SDL_HAT_CENTERED; }
static inline Sint16 SDL_JoystickGetAxis(SDL_Joystick *joystick, int axis) { (void)joystick; (void)axis; return 0; }
static inline Uint8 SDL_JoystickGetButton(SDL_Joystick *joystick, int button) { (void)joystick; (void)button; return 0; }

/* SDL RWops - stubs for libretro */
typedef struct SDL_RWops SDL_RWops;
static inline SDL_RWops *SDL_RWFromMem(void *mem, int size) { (void)mem; (void)size; return NULL; }
static inline SDL_Surface *SDL_LoadBMP_RW(SDL_RWops *src, int freesrc) { (void)src; (void)freesrc; return NULL; }

/* Window manager - stubs */
static inline void SDL_WM_SetCaption(const char *title, const char *icon) { (void)title; (void)icon; }
static inline void SDL_WM_SetIcon(SDL_Surface *icon, void *mask) { (void)icon; (void)mask; }
static inline void SDL_ShowCursor(int toggle) { (void)toggle; }

/* Surface operations - stubs */
#define SDL_SRCCOLORKEY 0x00001000
#define SDL_RLEACCEL    0x00004000
static inline SDL_Surface *SDL_DisplayFormat(SDL_Surface *surface) { return surface; }
static inline int SDL_SetColorKey(SDL_Surface *surface, Uint32 flag, Uint32 key) { (void)surface; (void)flag; (void)key; return 0; }
static inline void SDL_FreeSurface(SDL_Surface *surface) { (void)surface; }
static inline int SDL_BlitSurface(SDL_Surface *src, SDL_Rect *srcrect, SDL_Surface *dst, SDL_Rect *dstrect) {
    (void)src; (void)srcrect; (void)dst; (void)dstrect;
    return 0;
}

#ifdef __cplusplus
}
#endif

#endif /* SDL_COMPAT_H */
