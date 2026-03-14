/*
 * Castaway Atari ST emulator - libretro core for SF2000
 *
 * Based on DCaSTaway (Dingoo/Miyoo port)
 * Ported to SF2000 using libretro API pattern from UAE4ALL
 *
 * (C) 2024 - SF2000 port
 * Original Castaway (C) 1994-2002 Joachim Hoenig, Martin Doering
 */

#include "libretro.h"
#include "sdl_compat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* Include Castaway headers for proper declarations */
#include "config.h"
#include "st/st.h"
#include "st/mem.h"
#include "dcastaway.h"

/* Sound support - v017 */
#ifndef NO_SOUND
#include "sound/sound.h"
/* External MixBuffer from sound.cpp */
extern char MixBuffer[];
/* CompleteSndBufIdx is normally in audio.cpp, but we replace it */
int CompleteSndBufIdx = 0;
extern int nGeneratedSamples;
#define SOUND_SAMPLES_PER_FRAME 441  /* 22050 Hz / 50 fps */
/* Forward declarations */
static void libretro_push_audio(void);
#endif

/* SF2000 diagnostic macros - UAE4ALL pattern */
#ifdef SF2000
extern "C" void xlog(const char *fmt, ...);
#define XLOG(msg) xlog("CASTAWAY: %s\n", msg)
#define DIAG(msg) xlog("CASTAWAY: %s\n", msg)

/* v022: SF2000 firmware filesystem functions for per-game config */
extern "C" int fs_open(const char *path, int flags, int perms);
extern "C" ssize_t fs_read(int fd, void *buf, size_t count);
extern "C" ssize_t fs_write(int fd, const void *buf, size_t count);
extern "C" int fs_close(int fd);
extern "C" int fs_mkdir(const char *path, int mode);
extern "C" void fs_sync(const char *path);

#define FS_O_RDONLY 0x0000
#define FS_O_WRONLY 0x0001
#define FS_O_RDWR   0x0002
#define FS_O_CREAT  0x0100
#define FS_O_TRUNC  0x0200

#else
#define XLOG(msg)
#define DIAG(msg)
#endif

/* v022: Castaway frameskip variables (from events.cpp and dcastaway.cpp) */
extern int mainMenu_frameskip;
extern int maxframeskip;  /* v024: Also need to set this for frameskip to work! */
extern int frameskip;     /* v024: Current frameskip counter */

/* v027: CPU Boost - emu_hsync_add controls cycles per horizontal line
 * Normal: 512 (8 MHz / 15625 Hz hsync). Increase to "overclock" CPU.
 * v030: mfp_base_hsync stores the base value (512=PAL, 427=NTSC) for
 * MFP timer compensation - keeps music timing correct with CPU boost.
 * Both are declared in dcastaway.h */

/* membase is int8* in castaway, we need unsigned char* for libretro */
#define membase_ptr ((unsigned char*)membase)

/* Framebuffer: 320x200 for low res, 640x400 for high res */
/* We'll use 320x240 to match SF2000 screen */
#define CASTAWAY_WIDTH  320
#define CASTAWAY_HEIGHT 200
#define SCREEN_HEIGHT   240

static uint16_t *frame_buffer = NULL;
static uint16_t frame_buffer_data[CASTAWAY_WIDTH * SCREEN_HEIGHT];

/* Libretro callbacks */
static retro_environment_t environ_cb;
static retro_video_refresh_t video_cb;
static retro_audio_sample_t audio_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;

/* Input state for Castaway */
int libretro_input_state[16];

/* Joystick state for Atari ST (directly readable by emulator) */
int castaway_joy_up = 0;
int castaway_joy_down = 0;
int castaway_joy_left = 0;
int castaway_joy_right = 0;
int castaway_joy_fire = 0;

/* ROM/disk paths */
static char tos_rom_path[512];
static char disk_path[512];

/* Directory paths from libretro */
static const char *system_directory = NULL;
static const char *save_directory = NULL;
static const char *content_directory = NULL;

/* Version string */
#define CASTAWAY_VERSION "v030"

/*===========================================================================*/
/* SF2000 MENU & INPUT SYSTEM - v022                                         */
/*===========================================================================*/

/* Input mode: 0=Joystick, 1=Mouse */
static int sf2000_input_mode = 0;  /* Start in joystick mode */

/* v021: 2-Player support - SF2000 supports wireless 2nd controller */
static int sf2000_player2_enabled = 0;  /* 0=disabled, 1=enabled */

/* v022: Frameskip setting (mirrors mainMenu_frameskip) */
/* -1=Auto, 0=Off, 1-5=skip N frames */
static int sf2000_frameskip = 0;  /* Default: no frameskip */

/* v023: FPS counter (ON by default, like QPSX) */
static int fps_show = 1;        /* 1=show FPS, 0=hide */
static int fps_current = 0;     /* Current FPS value */
static int fps_frame_count = 0; /* Frame counter for FPS calc */
static int fps_last_frame = 0;  /* libretro_frame_count at last FPS update */

/* v027: CPU Boost - overclock 68000 by running more cycles per frame
 * 0=OFF (512 cycles/line), 1=1.25x (640), 2=1.5x (768), 3=2x (1024) */
static int sf2000_cpu_boost = 0;

/* L+R hold counter for toggle (3 seconds @ 50fps = 150 frames) */
static int lr_hold_frames = 0;
#define LR_TOGGLE_FRAMES 150

/* Menu state */
static int sf2000_menu_active = 0;
static int sf2000_menu_item = 0;
#define SF2000_MENU_ITEMS 8  /* Disk Swap, Frameskip, CPU Boost, Input Mode, 2 Player, Show FPS, About, Exit */

/* v022: Per-game config directory */
#define CASTAWAY_CONFIG_DIR "/mnt/sda1/cores/config/castaway"

/*===========================================================================*/
/* MULTI-DISK SUPPORT - v018 (like UAE4ALL)                                  */
/*===========================================================================*/
#define MAX_MULTIDISK 10
static char multidisk_paths[MAX_MULTIDISK][512];
static int multidisk_count = 0;
static int multidisk_current = 0;  /* Currently inserted disk index */

/* Forward declarations */
static int parse_multidisk_name(const char* fname, char* base_out, char* ext_out);
static void detect_multidisk(const char* loaded_path);
static void disk_shuffle(void);
static const char* get_current_disk_name(void);

/* Button edge detection */
static int prev_start = 0;
static int prev_up = 0;
static int prev_down = 0;
static int prev_left = 0;
static int prev_right = 0;
static int prev_a = 0;
static int prev_b = 0;
static int prev_x = 0;
static int prev_y = 0;
static int prev_l = 0;
static int prev_r = 0;
static int prev_select = 0;

/*===========================================================================*/
/* VIRTUAL KEYBOARD - v019                                                    */
/*===========================================================================*/
static int vkbd_active = 0;
static int vkbd_x = 0;  /* Current column */
static int vkbd_y = 0;  /* Current row */
static int vkbd_shift = 0;  /* Shift state */

/* Atari ST Scancodes for VKBD (from keymap.h) */
#define ST_ESC    0x01
#define ST_1      0x02
#define ST_2      0x03
#define ST_3      0x04
#define ST_4      0x05
#define ST_5      0x06
#define ST_6      0x07
#define ST_7      0x08
#define ST_8      0x09
#define ST_9      0x0A
#define ST_0      0x0B
#define ST_MINUS  0x0C
#define ST_EQUALS 0x0D
#define ST_BKSP   0x0E
#define ST_TAB    0x0F
#define ST_Q      0x10
#define ST_W      0x11
#define ST_E      0x12
#define ST_R      0x13
#define ST_T      0x14
#define ST_Y      0x15
#define ST_U      0x16
#define ST_I      0x17
#define ST_O      0x18
#define ST_P      0x19
#define ST_LBRACK 0x1A
#define ST_RBRACK 0x1B
#define ST_RETURN 0x1C
#define ST_CTRL   0x1D
#define ST_A      0x1E
#define ST_S      0x1F
#define ST_D      0x20
#define ST_F      0x21
#define ST_G      0x22
#define ST_H      0x23
#define ST_J      0x24
#define ST_K      0x25
#define ST_L      0x26
#define ST_SEMI   0x27
#define ST_QUOTE  0x28
#define ST_TILDE  0x29
#define ST_LSHIFT 0x2A
#define ST_BSLASH 0x2B
#define ST_Z      0x2C
#define ST_X      0x2D
#define ST_C      0x2E
#define ST_V      0x2F
#define ST_B      0x30
#define ST_N      0x31
#define ST_M      0x32
#define ST_COMMA  0x33
#define ST_PERIOD 0x34
#define ST_SLASH  0x35
#define ST_RSHIFT 0x36
#define ST_ALT    0x38
#define ST_SPACE  0x39
#define ST_CAPS   0x3A
#define ST_F1     0x3B
#define ST_F2     0x3C
#define ST_F3     0x3D
#define ST_F4     0x3E
#define ST_F5     0x3F
#define ST_F6     0x40
#define ST_F7     0x41
#define ST_F8     0x42
#define ST_F9     0x43
#define ST_F10    0x44
#define ST_DEL    0x53
#define ST_UP     0x48
#define ST_DOWN   0x50
#define ST_LEFT   0x4B
#define ST_RIGHT  0x4D
#define ST_HELP   0x62

/* VKBD Layout: 5 rows x 13 columns */
#define VKBD_COLS 13
#define VKBD_ROWS 5

/* Key labels and scancodes */
static const char* vkbd_labels[VKBD_ROWS][VKBD_COLS] = {
    {"ESC","1","2","3","4","5","6","7","8","9","0","-","BS"},
    {"TAB","Q","W","E","R","T","Y","U","I","O","P","[","]"},
    {"CTL","A","S","D","F","G","H","J","K","L",";","'","RET"},
    {"SHF","Z","X","C","V","B","N","M",",",".","/","UP","DEL"},
    {"ALT","SPC","SPC","SPC","SPC","SPC","F1","F5","F10","LT","DN","RT","HLP"}
};

static const uint8_t vkbd_codes[VKBD_ROWS][VKBD_COLS] = {
    {ST_ESC,ST_1,ST_2,ST_3,ST_4,ST_5,ST_6,ST_7,ST_8,ST_9,ST_0,ST_MINUS,ST_BKSP},
    {ST_TAB,ST_Q,ST_W,ST_E,ST_R,ST_T,ST_Y,ST_U,ST_I,ST_O,ST_P,ST_LBRACK,ST_RBRACK},
    {ST_CTRL,ST_A,ST_S,ST_D,ST_F,ST_G,ST_H,ST_J,ST_K,ST_L,ST_SEMI,ST_QUOTE,ST_RETURN},
    {ST_LSHIFT,ST_Z,ST_X,ST_C,ST_V,ST_B,ST_N,ST_M,ST_COMMA,ST_PERIOD,ST_SLASH,ST_UP,ST_DEL},
    {ST_ALT,ST_SPACE,ST_SPACE,ST_SPACE,ST_SPACE,ST_SPACE,ST_F1,ST_F5,ST_F10,ST_LEFT,ST_DOWN,ST_RIGHT,ST_HELP}
};

/* Track pressed key for release */
static uint8_t vkbd_pressed_key = 0;
static int vkbd_key_held = 0;

/* Simple 5x8 font for menu (minimal ASCII subset) */
static const unsigned char font5x8[] = {
    /* Space 32 */ 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    /* ! 33 */ 0x04,0x04,0x04,0x04,0x00,0x04,0x00,0x00,
    /* " 34 */ 0x0A,0x0A,0x00,0x00,0x00,0x00,0x00,0x00,
    /* - 45 */ 0x00,0x00,0x00,0x1F,0x00,0x00,0x00,0x00,
    /* . 46 */ 0x00,0x00,0x00,0x00,0x00,0x04,0x00,0x00,
    /* / 47 */ 0x01,0x02,0x04,0x08,0x10,0x00,0x00,0x00,
    /* 0-9 48-57 */
    0x0E,0x11,0x13,0x15,0x19,0x11,0x0E,0x00,
    0x04,0x0C,0x04,0x04,0x04,0x04,0x0E,0x00,
    0x0E,0x11,0x01,0x06,0x08,0x10,0x1F,0x00,
    0x0E,0x11,0x01,0x06,0x01,0x11,0x0E,0x00,
    0x02,0x06,0x0A,0x12,0x1F,0x02,0x02,0x00,
    0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E,0x00,
    0x06,0x08,0x10,0x1E,0x11,0x11,0x0E,0x00,
    0x1F,0x01,0x02,0x04,0x08,0x08,0x08,0x00,
    0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E,0x00,
    0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C,0x00,
    /* : 58 */ 0x00,0x04,0x00,0x00,0x04,0x00,0x00,0x00,
    /* @ 64 */ 0x0E,0x11,0x17,0x15,0x17,0x10,0x0E,0x00,
    /* A-Z 65-90 */
    0x0E,0x11,0x11,0x1F,0x11,0x11,0x11,0x00,
    0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E,0x00,
    0x0E,0x11,0x10,0x10,0x10,0x11,0x0E,0x00,
    0x1E,0x11,0x11,0x11,0x11,0x11,0x1E,0x00,
    0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F,0x00,
    0x1F,0x10,0x10,0x1E,0x10,0x10,0x10,0x00,
    0x0E,0x11,0x10,0x17,0x11,0x11,0x0F,0x00,
    0x11,0x11,0x11,0x1F,0x11,0x11,0x11,0x00,
    0x0E,0x04,0x04,0x04,0x04,0x04,0x0E,0x00,
    0x07,0x02,0x02,0x02,0x02,0x12,0x0C,0x00,
    0x11,0x12,0x14,0x18,0x14,0x12,0x11,0x00,
    0x10,0x10,0x10,0x10,0x10,0x10,0x1F,0x00,
    0x11,0x1B,0x15,0x15,0x11,0x11,0x11,0x00,
    0x11,0x19,0x15,0x13,0x11,0x11,0x11,0x00,
    0x0E,0x11,0x11,0x11,0x11,0x11,0x0E,0x00,
    0x1E,0x11,0x11,0x1E,0x10,0x10,0x10,0x00,
    0x0E,0x11,0x11,0x11,0x15,0x12,0x0D,0x00,
    0x1E,0x11,0x11,0x1E,0x14,0x12,0x11,0x00,
    0x0E,0x11,0x10,0x0E,0x01,0x11,0x0E,0x00,
    0x1F,0x04,0x04,0x04,0x04,0x04,0x04,0x00,
    0x11,0x11,0x11,0x11,0x11,0x11,0x0E,0x00,
    0x11,0x11,0x11,0x11,0x0A,0x0A,0x04,0x00,
    0x11,0x11,0x11,0x15,0x15,0x1B,0x11,0x00,
    0x11,0x11,0x0A,0x04,0x0A,0x11,0x11,0x00,
    0x11,0x11,0x0A,0x04,0x04,0x04,0x04,0x00,
    0x1F,0x01,0x02,0x04,0x08,0x10,0x1F,0x00,
    /* _ 95 */ 0x00,0x00,0x00,0x00,0x00,0x00,0x1F,0x00,
    /* a-z 97-122 (same as uppercase) */
};

static int font_index(char c) {
    if (c == ' ') return 0;
    if (c >= '0' && c <= '9') return 6 + (c - '0');  /* v020: fixed indices */
    if (c >= 'A' && c <= 'Z') return 18 + (c - 'A');
    if (c >= 'a' && c <= 'z') return 18 + (c - 'a');
    if (c == '!') return 1;
    if (c == '"') return 2;
    if (c == '-') return 3;
    if (c == '.') return 4;
    if (c == '/') return 5;
    if (c == ':') return 16;  /* v020: fixed index */
    if (c == '@') return 17;  /* v020: fixed index */
    if (c == '_') return 44;
    if (c == '(') return 0;   /* v020: parentheses as space */
    if (c == ')') return 0;
    if (c == '[') return 0;
    if (c == ']') return 0;
    return 0;  /* space for unknown */
}

static void draw_char(uint16_t *fb, int x, int y, char c, uint16_t color) {
    int idx = font_index(c);
    const unsigned char *glyph = &font5x8[idx * 8];
    for (int row = 0; row < 8; row++) {
        if (y + row >= SCREEN_HEIGHT) break;
        unsigned char bits = glyph[row];
        for (int col = 0; col < 5; col++) {
            if (x + col >= CASTAWAY_WIDTH) break;
            if (bits & (0x10 >> col)) {
                fb[(y + row) * CASTAWAY_WIDTH + x + col] = color;
            }
        }
    }
}

static void draw_text(uint16_t *fb, int x, int y, const char *text, uint16_t color) {
    while (*text) {
        draw_char(fb, x, y, *text, color);
        x += 6;
        text++;
    }
}

static void draw_rect(uint16_t *fb, int x, int y, int w, int h, uint16_t color) {
    for (int j = y; j < y + h && j < SCREEN_HEIGHT; j++) {
        for (int i = x; i < x + w && i < CASTAWAY_WIDTH; i++) {
            fb[j * CASTAWAY_WIDTH + i] = color;
        }
    }
}

/* RGB565 colors */
#define COLOR_BLACK   0x0000
#define COLOR_WHITE   0xFFFF
#define COLOR_BLUE    0x001F
#define COLOR_YELLOW  0xFFE0
#define COLOR_GRAY    0x8410
#define COLOR_DKBLUE  0x000A
#define COLOR_RED     0xF800  /* v029: For warnings */

static void sf2000_draw_menu(uint16_t *fb) {
    /* Menu background */
    draw_rect(fb, 20, 40, 280, 170, COLOR_DKBLUE);
    draw_rect(fb, 22, 42, 276, 166, COLOR_BLACK);

    /* Header */
    draw_text(fb, 30, 50, "ATARI ST - SF2000", COLOR_YELLOW);
    draw_text(fb, 30, 62, "BY GRZEGORZ KORYCKI", COLOR_WHITE);
    draw_text(fb, 30, 74, "@THE_Q_DEV ON TELEGRAM", COLOR_GRAY);

    /* Separator */
    draw_rect(fb, 30, 88, 260, 1, COLOR_GRAY);

    /* Menu items - v023: reduced spacing (12px) to fit more items */
    for (int i = 0; i < SF2000_MENU_ITEMS; i++) {
        int item_y = 96 + i * 12;  /* v023: 12px spacing instead of 18px */
        uint16_t color = (i == sf2000_menu_item) ? COLOR_YELLOW : COLOR_WHITE;

        /* Selection marker */
        if (i == sf2000_menu_item) {
            draw_text(fb, 30, item_y, ">", color);
        }

        switch (i) {
            case 0: {
                /* Disk Swap - v020: show as "(X/Y) filename" format */
                char diskbuf[50];
                if (multidisk_count > 1) {
                    snprintf(diskbuf, sizeof(diskbuf), "DISK: (%d/%d) %s",
                             multidisk_current + 1, multidisk_count, get_current_disk_name());
                } else {
                    snprintf(diskbuf, sizeof(diskbuf), "DISK: %s", get_current_disk_name());
                }
                draw_text(fb, 45, item_y, diskbuf, color);
                break;
            }
            case 1: {
                /* v025: Frameskip 0-8 */
                draw_text(fb, 45, item_y, "FRAMESKIP:", color);
                const char *fskip;
                switch (sf2000_frameskip) {
                    case 0:  fskip = "OFF"; break;
                    case 1:  fskip = "1"; break;
                    case 2:  fskip = "2"; break;
                    case 3:  fskip = "3"; break;
                    case 4:  fskip = "4"; break;
                    case 5:  fskip = "5"; break;
                    case 6:  fskip = "6"; break;
                    case 7:  fskip = "7"; break;
                    case 8:  fskip = "8"; break;
                    default: fskip = "?"; break;
                }
                draw_text(fb, 140, item_y, fskip, COLOR_YELLOW);
                break;
            }
            case 2: {
                /* v030: CPU Boost - MFP timer fix preserves music timing */
                draw_text(fb, 45, item_y, "CPU BOOST:", color);
                const char *boost;
                switch (sf2000_cpu_boost) {
                    case 0:  boost = "OFF"; break;
                    case 1:  boost = "1.25X"; break;
                    case 2:  boost = "1.5X"; break;
                    case 3:  boost = "2X"; break;
                    default: boost = "?"; break;
                }
                draw_text(fb, 140, item_y, boost, COLOR_YELLOW);
                break;
            }
            case 3: {
                /* Input Mode */
                draw_text(fb, 45, item_y, "INPUT:", color);
                const char *mode = sf2000_input_mode ? "MOUSE" : "JOYSTICK";
                draw_text(fb, 110, item_y, mode, COLOR_YELLOW);
                break;
            }
            case 4: {
                /* v021: 2-Player Mode */
                draw_text(fb, 45, item_y, "2 PLAYER:", color);
                const char *p2mode = sf2000_player2_enabled ? "ON" : "OFF";
                draw_text(fb, 130, item_y, p2mode, COLOR_YELLOW);
                break;
            }
            case 5: {
                /* v023: Show FPS toggle */
                draw_text(fb, 45, item_y, "SHOW FPS:", color);
                draw_text(fb, 130, item_y, fps_show ? "ON" : "OFF", COLOR_YELLOW);
                break;
            }
            case 6:
                draw_text(fb, 45, item_y, "ABOUT", color);
                break;
            case 7:
                draw_text(fb, 45, item_y, "EXIT MENU", color);
                break;
        }
    }

    /* Footer - v028: moved lower to not overlap EXIT MENU */
    draw_text(fb, 30, 198, "UP/DN:SEL A:OK B:EXIT", COLOR_GRAY);
}

/*===========================================================================*/
/* VIRTUAL KEYBOARD DRAWING - v019                                            */
/*===========================================================================*/
#define COLOR_VKBD_BG    0x2104  /* Dark gray */
#define COLOR_VKBD_KEY   0x4208  /* Medium gray */
#define COLOR_VKBD_SEL   0x07E0  /* Green */
#define COLOR_VKBD_TEXT  0xFFFF  /* White */

static void sf2000_draw_vkbd(uint16_t *fb) {
    /* VKBD at bottom of screen */
    int base_x = 4;
    int base_y = 160;
    int key_w = 24;
    int key_h = 15;
    int gap = 1;

    /* Background */
    draw_rect(fb, 0, base_y - 2, 320, 82, COLOR_VKBD_BG);

    /* Draw keys */
    for (int row = 0; row < VKBD_ROWS; row++) {
        for (int col = 0; col < VKBD_COLS; col++) {
            int x = base_x + col * (key_w + gap);
            int y = base_y + row * (key_h + gap);

            /* Key background */
            uint16_t bg_color = (row == vkbd_y && col == vkbd_x) ? COLOR_VKBD_SEL : COLOR_VKBD_KEY;
            draw_rect(fb, x, y, key_w, key_h, bg_color);

            /* Key label - center it */
            const char* label = vkbd_labels[row][col];
            int label_len = strlen(label);
            int text_x = x + (key_w - label_len * 6) / 2;
            int text_y = y + 4;
            draw_text(fb, text_x, text_y, label, COLOR_VKBD_TEXT);
        }
    }

    /* Footer hint */
    draw_text(fb, 4, base_y - 12, "SELECT:CLOSE A:PRESS B:SHIFT", COLOR_GRAY);
}

/*===========================================================================*/
/* MULTI-DISK FUNCTIONS - v020                                               */
/*===========================================================================*/

/* Multi-disk pattern type */
#define MULTIDISK_NONE       0
#define MULTIDISK_UNDERSCORE 1  /* game_1.st */
#define MULTIDISK_DISKOF     2  /* game (Disk 1 of 2).st */

static int multidisk_pattern_type = MULTIDISK_NONE;
static int multidisk_num_pos = 0;      /* Position of disk number in filename */
static char multidisk_original[512];   /* Original loaded filename (just name, no path) */

/* Parse filename to extract disk number and pattern type
 * v020: Properly supports:
 *   - game_1.st, game_2.st (underscore + single digit)
 *   - game (Disk 1 of 2).st (explicit "Disk X of Y" pattern)
 * Does NOT match: "cannon fodder 2.st" (sequel, not disk 2!)
 * Returns disk number (1-9), or -1 if not a numbered disk
 */
static int parse_multidisk_name(const char* fname, char* base_out, char* ext_out)
{
    int len = strlen(fname);
    if (len < 5) return -1;

    /* Find last dot for extension */
    int dot_pos = -1;
    for (int i = len - 1; i >= 0; i--) {
        if (fname[i] == '.') {
            dot_pos = i;
            break;
        }
        if (fname[i] == '/' || fname[i] == '\\') break;
    }
    if (dot_pos < 2) return -1;

    /* Copy extension */
    strcpy(ext_out, fname + dot_pos);

    /* Check for "(Disk X of Y)" pattern (case insensitive) */
    const char* disk_of = strstr(fname, "(Disk ");
    if (!disk_of) disk_of = strstr(fname, "(disk ");
    if (!disk_of) disk_of = strstr(fname, "(DISK ");

    if (disk_of) {
        /* Find the number after "(Disk " */
        const char* p = disk_of + 6;  /* Skip "(Disk " */
        if (*p >= '1' && *p <= '9') {
            int num = *p - '0';
            /* Copy base (everything before the pattern) */
            int base_len = disk_of - fname;
            strncpy(base_out, fname, base_len);
            base_out[base_len] = '\0';
            /* Store pattern info for reconstruction */
            multidisk_pattern_type = MULTIDISK_DISKOF;
            multidisk_num_pos = p - fname;
            return num;
        }
    }

    /* v020: ONLY accept underscore + single digit pattern: game_1.st
     * This prevents "Cannon Fodder 2.st" from being detected as disk 2 */
    if (dot_pos >= 2) {
        char before_dot = fname[dot_pos - 1];
        char before_num = fname[dot_pos - 2];

        /* Must be underscore followed by single digit: _1, _2, etc. */
        if (before_num == '_' && before_dot >= '1' && before_dot <= '9') {
            int num = before_dot - '0';
            /* Copy base name (everything before the underscore) */
            int base_len = dot_pos - 2;
            strncpy(base_out, fname, base_len);
            base_out[base_len] = '\0';
            /* Store pattern info */
            multidisk_pattern_type = MULTIDISK_UNDERSCORE;
            multidisk_num_pos = dot_pos - 1;
            return num;
        }
    }

    /* No valid multi-disk pattern found */
    multidisk_pattern_type = MULTIDISK_NONE;
    return -1;
}

/* Check if file exists */
static int file_exists_st(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (f) {
        fclose(f);
        return 1;
    }
    return 0;
}

/* Detect all disks in a multi-disk set
 * v020: Properly handles both "_N" and "(Disk N of M)" patterns
 */
static void detect_multidisk(const char* loaded_path)
{
    char base[256], ext[32];
    char path_prefix[512] = "";

    multidisk_count = 0;
    multidisk_current = 0;

    /* Find directory prefix */
    int len = strlen(loaded_path);
    int last_sep = -1;
    for (int i = 0; i < len; i++) {
        if (loaded_path[i] == '/' || loaded_path[i] == '\\') {
            last_sep = i;
        }
    }

    const char* filename = loaded_path;
    if (last_sep >= 0) {
        strncpy(path_prefix, loaded_path, last_sep + 1);
        path_prefix[last_sep + 1] = '\0';
        filename = loaded_path + last_sep + 1;
    }

    /* Store original filename for pattern reconstruction */
    strncpy(multidisk_original, filename, 511);
    multidisk_original[511] = '\0';

    int disk_num = parse_multidisk_name(filename, base, ext);
    if (disk_num < 0) {
        /* Not a numbered disk - store as single disk */
        strncpy(multidisk_paths[0], loaded_path, 511);
        multidisk_paths[0][511] = '\0';
        multidisk_count = 1;
        return;
    }

    /* Search for disks 1-9 based on pattern type */
    char found_disks[10][512];
    int disk_exists[10] = {0};

    for (int i = 1; i <= 9; i++) {
        char try_path[512];
        char try_name[512];

        if (multidisk_pattern_type == MULTIDISK_DISKOF) {
            /* For "(Disk X of Y)" pattern: copy original and replace number char */
            strcpy(try_name, multidisk_original);
            try_name[multidisk_num_pos] = '0' + i;  /* Replace disk number */
            snprintf(try_path, sizeof(try_path), "%s%s", path_prefix, try_name);
        } else {
            /* For "_N" pattern: base + underscore + number + ext */
            snprintf(try_path, sizeof(try_path), "%s%s_%d%s", path_prefix, base, i, ext);
        }

        if (file_exists_st(try_path)) {
            strcpy(found_disks[i], try_path);
            disk_exists[i] = 1;
        }
    }

    /* Build list starting from disk 1 */
    for (int i = 1; i <= 9 && multidisk_count < MAX_MULTIDISK; i++) {
        if (disk_exists[i]) {
            strncpy(multidisk_paths[multidisk_count], found_disks[i], 511);
            multidisk_paths[multidisk_count][511] = '\0';
            multidisk_count++;
        }
    }

    /* If nothing found, store original path */
    if (multidisk_count == 0) {
        strncpy(multidisk_paths[0], loaded_path, 511);
        multidisk_paths[0][511] = '\0';
        multidisk_count = 1;
    }

    /* Find which disk was originally loaded */
    for (int i = 0; i < multidisk_count; i++) {
        if (strcmp(multidisk_paths[i], loaded_path) == 0) {
            multidisk_current = i;
            break;
        }
    }
}

/* Shuffle to next disk */
static void disk_shuffle(void)
{
    if (multidisk_count <= 1) return;

    /* Move to next disk */
    multidisk_current = (multidisk_current + 1) % multidisk_count;

    /* Load the new disk using FDCInit */
    extern struct Disk disk[2];
    extern int FDCInit(int i);

    strncpy(disk[0].name, multidisk_paths[multidisk_current], 79);
    disk[0].name[79] = '\0';
    FDCInit(0);
}

/* Get filename part of current disk path
 * v020: Returns shorter name suitable for "(X/Y) name" format
 */
static const char* get_current_disk_name(void)
{
    static char namebuf[32];

    if (multidisk_count == 0) {
        strcpy(namebuf, "-");
        return namebuf;
    }

    const char* path = multidisk_paths[multidisk_current];
    const char* name = path;

    /* Find filename part */
    for (const char* p = path; *p; p++) {
        if (*p == '/' || *p == '\\') name = p + 1;
    }

    /* Copy name, truncate at first '(' or '[' for cleaner display */
    int i = 0;
    while (name[i] && i < 18) {
        if (name[i] == '(' || name[i] == '[') break;
        namebuf[i] = name[i];
        i++;
    }
    namebuf[i] = '\0';

    /* Trim trailing spaces and underscores */
    while (i > 0 && (namebuf[i-1] == ' ' || namebuf[i-1] == '_')) {
        namebuf[--i] = '\0';
    }

    /* Remove extension if still present */
    char* dot = strrchr(namebuf, '.');
    if (dot) *dot = '\0';

    /* Add "..." if truncated */
    if (strlen(name) > 18 && strlen(namebuf) >= 15) {
        namebuf[15] = '.';
        namebuf[16] = '.';
        namebuf[17] = '.';
        namebuf[18] = '\0';
    }

    return namebuf;
}

/*===========================================================================*/
/* PER-GAME CONFIG - v022                                                     */
/*===========================================================================*/

/* Get config file path for current game */
static void get_config_path(char* path, int size) {
    /* Extract game name from disk_path (last path component without extension) */
    const char* filename = strrchr(disk_path, '/');
    if (!filename) filename = strrchr(disk_path, '\\');
    if (filename) {
        filename++;  /* Skip the separator */
    } else {
        filename = disk_path;  /* No path separator, use whole string */
    }

    /* Build path: /mnt/sda1/cores/config/castaway/GameName.cfg */
    snprintf(path, size, "%s/%s", CASTAWAY_CONFIG_DIR, filename);

    /* Replace extension with .cfg */
    char* dot = strrchr(path, '.');
    if (dot && (dot > strrchr(path, '/'))) {
        strcpy(dot, ".cfg");
    } else {
        strcat(path, ".cfg");
    }
}

/* Apply frameskip setting to emulator
 * v024: Must set BOTH mainMenu_frameskip AND maxframeskip!
 * - mainMenu_frameskip is the config value
 * - maxframeskip is the active value used in dcastaway_one_frame()
 * - frameskip is the current counter (reset to 0 to apply immediately)
 */
static void apply_frameskip(void) {
    mainMenu_frameskip = sf2000_frameskip;
    maxframeskip = sf2000_frameskip;  /* v024: This is what actually controls skipping! */
    frameskip = 0;  /* v024: Reset counter to apply immediately */
}

/* v028: Apply CPU boost setting
 * Modifies emu_hsync_add to run more cycles per horizontal line
 * Must handle both PAL (base 512) and NTSC (base 427)
 * v031: Now solely controls emu_hsync_add (not reset in dcastaway.cpp anymore)
 * Also sets mfp_base_hsync for MFP timer compensation
 * Multipliers: 0=1x, 1=1.25x, 2=1.5x, 3=2x
 */
static void apply_cpu_boost(void) {
    /* Determine base cycles based on refresh rate (PAL=50Hz, NTSC=60Hz) */
    int base = (nScreenRefreshRate == 50) ? 512 : 427;

    /* v031: Set base for MFP timer compensation (always real 8MHz timing) */
    mfp_base_hsync = base;

    switch (sf2000_cpu_boost) {
        case 0:  emu_hsync_add = base;            break;  /* 1x - Normal */
        case 1:  emu_hsync_add = (base * 5) / 4;  break;  /* 1.25x */
        case 2:  emu_hsync_add = (base * 3) / 2;  break;  /* 1.5x */
        case 3:  emu_hsync_add = base * 2;        break;  /* 2x */
        default: emu_hsync_add = base;            break;
    }
}

#ifdef SF2000
/* Save per-game config using SF2000 firmware fs_* calls */
static int sf2000_save_config(void) {
    char path[256];
    get_config_path(path, sizeof(path));

    /* Ensure config directory exists */
    fs_mkdir(CASTAWAY_CONFIG_DIR, 0755);

    /* Open file for writing */
    int fd = fs_open(path, FS_O_WRONLY | FS_O_CREAT | FS_O_TRUNC, 0666);
    if (fd < 0) return 0;

    /* Build config content */
    char buf[256];
    int len = snprintf(buf, sizeof(buf),
        "# Castaway ST Config v028\n"
        "frameskip=%d\n"
        "cpu_boost=%d\n"
        "input_mode=%d\n"
        "player2=%d\n"
        "fps_show=%d\n",
        sf2000_frameskip,
        sf2000_cpu_boost,
        sf2000_input_mode,
        sf2000_player2_enabled,
        fps_show);

    /* Write and close */
    ssize_t written = fs_write(fd, buf, len);
    fs_close(fd);

    /* Sync to SD card */
    fs_sync(path);

    return (written == len) ? 1 : 0;
}

/* Load per-game config using SF2000 firmware fs_* calls */
static int sf2000_load_config(void) {
    char path[256];
    get_config_path(path, sizeof(path));

    /* Open file for reading */
    int fd = fs_open(path, FS_O_RDONLY, 0);
    if (fd < 0) return 0;

    /* Read entire file */
    char buf[256];
    ssize_t bytes_read = fs_read(fd, buf, sizeof(buf) - 1);
    fs_close(fd);

    if (bytes_read <= 0) return 0;
    buf[bytes_read] = '\0';

    /* Parse line by line */
    char* line = buf;
    while (line && *line) {
        char* next = strchr(line, '\n');
        if (next) *next++ = '\0';

        if (line[0] != '#' && line[0] != '\0') {
            int val;
            if (sscanf(line, "frameskip=%d", &val) == 1) sf2000_frameskip = val;
            else if (sscanf(line, "cpu_boost=%d", &val) == 1) sf2000_cpu_boost = val;
            else if (sscanf(line, "input_mode=%d", &val) == 1) sf2000_input_mode = val;
            else if (sscanf(line, "player2=%d", &val) == 1) sf2000_player2_enabled = val;
            else if (sscanf(line, "fps_show=%d", &val) == 1) fps_show = val;
        }
        line = next;
    }

    /* Apply loaded settings */
    apply_frameskip();
    apply_cpu_boost();

    return 1;
}
#else
/* Non-SF2000 stubs */
static int sf2000_save_config(void) { return 0; }
static int sf2000_load_config(void) { return 0; }
#endif

/* SDL compatibility - frame counter for SDL_GetTicks() */
volatile uint32_t libretro_frame_count = 0;

/* Global screen surface for SDL compat - UAE4ALL pattern
 * CRITICAL: render.cpp uses 'extern SDL_Surface *screen' - we DEFINE it here!
 * This is the ONLY place where screen is defined. render.cpp declares extern. */
static SDL_Surface screen_surface;
SDL_Surface *libretro_screen_surface = &screen_surface;
SDL_Surface *screen = NULL;  /* DEFINITION - render.cpp has extern declaration */

/*
 * Libretro callbacks setup
 */

void retro_set_environment(retro_environment_t cb)
{
    environ_cb = cb;

    struct retro_variable variables[] = {
        { "castaway_frameskip", "Frameskip; 0|1|2|3|4" },
        { NULL, NULL },
    };

    cb(RETRO_ENVIRONMENT_SET_VARIABLES, variables);
}

void retro_set_video_refresh(retro_video_refresh_t cb)
{
    video_cb = cb;
}

void retro_set_audio_sample(retro_audio_sample_t cb)
{
    audio_cb = cb;
}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
    audio_batch_cb = cb;
}

void retro_set_input_poll(retro_input_poll_t cb)
{
    input_poll_cb = cb;
}

void retro_set_input_state(retro_input_state_t cb)
{
    input_state_cb = cb;
}

/*
 * Core information
 */

unsigned retro_api_version(void)
{
    return RETRO_API_VERSION;
}

void retro_get_system_info(struct retro_system_info *info)
{
    memset(info, 0, sizeof(*info));
    info->library_name     = "castaway";
    info->library_version  = CASTAWAY_VERSION;
    info->valid_extensions = "st|msa|zip";
    info->need_fullpath    = 1;  /* We need the disk image path */
    info->block_extract    = 0;
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
    struct retro_game_geometry geom = {
        CASTAWAY_WIDTH,   /* base_width */
        SCREEN_HEIGHT,    /* base_height */
        CASTAWAY_WIDTH,   /* max_width */
        SCREEN_HEIGHT,    /* max_height */
        4.0 / 3.0         /* aspect_ratio */
    };

    struct retro_system_timing timing = {
        50.0,             /* fps (PAL) */
        22050.0           /* sample_rate (same as SF2000) */
    };

    info->geometry = geom;
    info->timing = timing;
}

/*
 * Core initialization
 */

void retro_init(void)
{
    /* Get directories from libretro frontend */
    if (environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &system_directory) && system_directory) {
        snprintf(tos_rom_path, sizeof(tos_rom_path), "%s/tos.rom", system_directory);
    } else {
        strcpy(tos_rom_path, "./tos.rom");
    }

    environ_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &save_directory);
    environ_cb(RETRO_ENVIRONMENT_GET_CONTENT_DIRECTORY, &content_directory);

    /* Set pixel format to RGB565 */
    int fmt = RETRO_PIXEL_FORMAT_RGB565;
    environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt);

    /* Input descriptors */
    struct retro_input_descriptor desc[] = {
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "Up" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "Down" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "Left" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Right" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "Fire" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "Fire 2" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Menu" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,"Select" },
        { 0, 0, 0, 0, NULL }
    };
    environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, desc);

    /* Initialize framebuffer */
    frame_buffer = frame_buffer_data;
    memset(frame_buffer, 0, CASTAWAY_WIDTH * SCREEN_HEIGHT * sizeof(uint16_t));

    /* Initialize SDL compat screen surface - UAE4ALL pattern
     * CRITICAL: Set up screen_surface FIRST, then point 'screen' to it
     * This must happen BEFORE any render.cpp functions are called! */
    screen_surface.w = CASTAWAY_WIDTH;
    screen_surface.h = SCREEN_HEIGHT;
    screen_surface.pitch = CASTAWAY_WIDTH << 1;  /* UAE4ALL uses << 1 for pitch */
    screen_surface.pixels = frame_buffer;
    screen_surface.format = NULL;

    /* CRITICAL: Connect global screen pointer to our surface
     * render.cpp has 'extern SDL_Surface *screen' - we set it here */
    screen = &screen_surface;
    libretro_screen_surface = &screen_surface;

    DIAG("retro_init: screen initialized");

    /* Initialize emulator */
    nScreenRefreshRate = 50;  /* PAL */

#ifndef NO_SOUND
    /* Initialize audio - v017 */
    audio_init();
    DIAG("retro_init: audio initialized");
#endif
}

void retro_deinit(void)
{
    MemQuit();
    frame_buffer = NULL;
}

/*
 * Game loading
 */

/* External disk path variable from FDC */
extern char *dcastaway_image_file;
extern int changed_fdc0;

int retro_load_game(const struct retro_game_info *game)
{
    DIAG("retro_load_game() start");

    if (!game || !game->path) {
        DIAG("retro_load_game() - no game path!");
        return 0;
    }

    /* Save disk path */
    strncpy(disk_path, game->path, sizeof(disk_path) - 1);
    disk_path[sizeof(disk_path) - 1] = '\0';

    DIAG("retro_load_game() - Setting ROM path");

    /* Set TOS ROM path before MemInit (extern char rom[80] from mem.h) */
    strncpy(rom, tos_rom_path, 79);
    rom[79] = '\0';

    DIAG("retro_load_game() - MemInit");

    /* Initialize ST memory (loads TOS ROM) */
    if (MemInit()) {
        /* Failed to init memory (probably missing TOS ROM) */
        DIAG("retro_load_game() - MemInit FAILED!");
        return 0;
    }

    DIAG("retro_load_game() - dcastaway_init");

    /* Initialize emulator using new frame-by-frame function */
    dcastaway_init();

    /* Set disk image path for FDC */
    if (dcastaway_image_file) {
        strcpy(dcastaway_image_file, disk_path);
        changed_fdc0 = 1;
        FDCInit(0);
    }

    /* v018: Detect multi-disk set */
    detect_multidisk(disk_path);
    DIAG("retro_load_game() - multidisk detection done");

    /* v022: Load per-game config (frameskip, input mode, etc.) */
    if (sf2000_load_config()) {
        DIAG("retro_load_game() - per-game config loaded");
    } else {
        /* No config found - use defaults */
        sf2000_frameskip = 0;  /* OFF by default */
        sf2000_input_mode = 0;  /* Joystick by default */
        sf2000_player2_enabled = 0;  /* Off by default */
        fps_show = 1;  /* v023: FPS ON by default */
        apply_frameskip();
        DIAG("retro_load_game() - using default config");
    }

    /* Deferred init handled by static Deffered in retro_run() - UAE4ALL pattern */

    DIAG("retro_load_game() - done");

    return 1;
}

int retro_load_game_special(unsigned game_type, const struct retro_game_info *info, size_t num_info)
{
    (void)game_type;
    (void)info;
    (void)num_info;
    return 0;
}

void retro_unload_game(void)
{
    emulating = 0;
}

/*
 * Input polling - UAE4ALL pattern: direct IKBD joystick control
 */

/* Atari ST joystick bit definitions (from events.cpp) */
#define JOY_UP       1
#define JOY_DOWN     2
#define JOY_LEFT     4
#define JOY_RIGHT    8
#define JOY_BUTTON 128

/* Cached joystick state - UAE4ALL pattern */
static uint8_t g_cached_joy0 = 0;

/* Mouse button state tracking */
static int mouse_lmb_pressed = 0;
static int mouse_rmb_pressed = 0;
/* A/B mouse button state (mouse mode only) */
static int mouse_a_lmb_pressed = 0;
static int mouse_b_rmb_pressed = 0;

/* Mouse speed */
#define MOUSE_SPEED 3

static void poll_input(void)
{
    input_poll_cb();

    /* Read all buttons */
    int cur_up    = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP);
    int cur_down  = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN);
    int cur_left  = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT);
    int cur_right = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT);
    int cur_a     = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A);
    int cur_b     = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B);
    int cur_start = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START);
    int cur_select= input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT);
    int cur_x     = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X);
    int cur_y     = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y);
    int cur_l     = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L);
    int cur_r     = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R);

    /* L+R held for 3 seconds = toggle input mode */
    if (cur_l && cur_r) {
        lr_hold_frames++;
        if (lr_hold_frames >= LR_TOGGLE_FRAMES) {
            sf2000_input_mode = !sf2000_input_mode;
            lr_hold_frames = 0;  /* Reset counter */
        }
    } else {
        lr_hold_frames = 0;
    }

    /* SELECT = toggle virtual keyboard (edge triggered) - v019 */
    if (cur_select && !prev_select && !sf2000_menu_active) {
        vkbd_active = !vkbd_active;
        /* Release any held key when closing VKBD */
        if (!vkbd_active) {
            if (vkbd_key_held) {
                IkbdKeyRelease(vkbd_pressed_key);
                vkbd_key_held = 0;
            }
            /* Release shift if held */
            if (vkbd_shift) {
                IkbdKeyRelease(ST_LSHIFT);
                vkbd_shift = 0;
            }
            /* v028: Clear VKBD area (rows 158-239) when closing
             * VKBD draws from y=158 (base_y-2), game will redraw 158-219 next frame */
            if (frame_buffer) {
                memset(&frame_buffer[158 * CASTAWAY_WIDTH], 0,
                       (SCREEN_HEIGHT - 158) * CASTAWAY_WIDTH * sizeof(uint16_t));
            }
        }
    }
    prev_select = cur_select;

    /* START = toggle menu (edge triggered) */
    if (cur_start && !prev_start && !vkbd_active) {
        sf2000_menu_active = !sf2000_menu_active;
        sf2000_menu_item = 0;
    }
    prev_start = cur_start;

    /* L = LMB, R = RMB (always, like UAE4ALL)
     * IKBD protocol: bit 0 = RMB, bit 1 = LMB
     * So: IkbdMousePress(2) = LMB, IkbdMousePress(1) = RMB */
    if (cur_l && !prev_l) {
        IkbdMousePress(2);  /* LMB press (bit 1 = left button) */
        mouse_lmb_pressed = 1;
    } else if (!cur_l && prev_l && mouse_lmb_pressed) {
        IkbdMouseRelease(2);  /* LMB release */
        mouse_lmb_pressed = 0;
    }
    prev_l = cur_l;

    if (cur_r && !prev_r) {
        IkbdMousePress(1);  /* RMB press (bit 0 = right button) */
        mouse_rmb_pressed = 1;
    } else if (!cur_r && prev_r && mouse_rmb_pressed) {
        IkbdMouseRelease(1);  /* RMB release */
        mouse_rmb_pressed = 0;
    }
    prev_r = cur_r;

    /* Don't process game input if menu is active */
    if (sf2000_menu_active) {
        /* Menu navigation */
        if (cur_up && !prev_up) {
            sf2000_menu_item--;
            if (sf2000_menu_item < 0) sf2000_menu_item = SF2000_MENU_ITEMS - 1;
        }
        if (cur_down && !prev_down) {
            sf2000_menu_item++;
            if (sf2000_menu_item >= SF2000_MENU_ITEMS) sf2000_menu_item = 0;
        }
        /* A = select/change */
        if (cur_a && !prev_a) {
            switch (sf2000_menu_item) {
                case 0:  /* Disk Swap - v018 */
                    disk_shuffle();
                    break;
                case 1:  /* v025: Frameskip cycle: OFF->1->2->...->8->OFF */
                    if (sf2000_frameskip < 8) sf2000_frameskip++;
                    else sf2000_frameskip = 0;  /* Wrap back to OFF */
                    apply_frameskip();
                    sf2000_save_config();  /* Save per-game config */
                    break;
                case 2:  /* v027: CPU Boost cycle: OFF->1.25x->1.5x->2x->OFF */
                    if (sf2000_cpu_boost < 3) sf2000_cpu_boost++;
                    else sf2000_cpu_boost = 0;
                    apply_cpu_boost();
                    sf2000_save_config();
                    break;
                case 3:  /* Input Mode */
                    sf2000_input_mode = !sf2000_input_mode;
                    sf2000_save_config();
                    break;
                case 4:  /* v021: 2-Player toggle */
                    sf2000_player2_enabled = !sf2000_player2_enabled;
                    sf2000_save_config();
                    break;
                case 5:  /* v023: Show FPS toggle */
                    fps_show = !fps_show;
                    sf2000_save_config();
                    break;
                case 6:  /* About - just flash */
                    break;
                case 7:  /* Exit Menu */
                    sf2000_menu_active = 0;
                    break;
            }
        }
        /* Left/Right for frameskip fine control (v025: 0-8) */
        if (sf2000_menu_item == 1) {
            if (cur_left && !prev_left) {
                if (sf2000_frameskip > 0) sf2000_frameskip--;
                else sf2000_frameskip = 8;  /* Wrap from OFF to 8 */
                apply_frameskip();
                sf2000_save_config();
            }
            if (cur_right && !prev_right) {
                if (sf2000_frameskip < 8) sf2000_frameskip++;
                else sf2000_frameskip = 0;  /* Wrap to OFF */
                apply_frameskip();
                sf2000_save_config();
            }
        }
        /* v027: Left/Right for CPU Boost fine control */
        if (sf2000_menu_item == 2) {
            if (cur_left && !prev_left) {
                if (sf2000_cpu_boost > 0) sf2000_cpu_boost--;
                else sf2000_cpu_boost = 3;  /* Wrap from OFF to 2x */
                apply_cpu_boost();
                sf2000_save_config();
            }
            if (cur_right && !prev_right) {
                if (sf2000_cpu_boost < 3) sf2000_cpu_boost++;
                else sf2000_cpu_boost = 0;  /* Wrap to OFF */
                apply_cpu_boost();
                sf2000_save_config();
            }
        }
        /* B = exit menu */
        if (cur_b && !prev_b) {
            sf2000_menu_active = 0;
        }
        prev_up = cur_up;
        prev_down = cur_down;
        prev_left = cur_left;
        prev_right = cur_right;
        prev_a = cur_a;
        prev_b = cur_b;
        return;  /* Don't process game input */
    }

    /* v019: Virtual Keyboard input handling */
    if (vkbd_active) {
        /* Navigation with D-pad */
        if (cur_up && !prev_up) {
            vkbd_y--;
            if (vkbd_y < 0) vkbd_y = VKBD_ROWS - 1;
        }
        if (cur_down && !prev_down) {
            vkbd_y++;
            if (vkbd_y >= VKBD_ROWS) vkbd_y = 0;
        }
        if (cur_left && !prev_left) {
            vkbd_x--;
            if (vkbd_x < 0) vkbd_x = VKBD_COLS - 1;
        }
        if (cur_right && !prev_right) {
            vkbd_x++;
            if (vkbd_x >= VKBD_COLS) vkbd_x = 0;
        }

        /* A = press key */
        if (cur_a && !prev_a) {
            uint8_t keycode = vkbd_codes[vkbd_y][vkbd_x];
            /* Press shift first if active */
            if (vkbd_shift && keycode != ST_LSHIFT && keycode != ST_RSHIFT) {
                IkbdKeyPress(ST_LSHIFT);
            }
            IkbdKeyPress(keycode);
            vkbd_pressed_key = keycode;
            vkbd_key_held = 1;
        }
        if (!cur_a && prev_a && vkbd_key_held) {
            IkbdKeyRelease(vkbd_pressed_key);
            /* Release shift after key if it was held */
            if (vkbd_shift && vkbd_pressed_key != ST_LSHIFT && vkbd_pressed_key != ST_RSHIFT) {
                IkbdKeyRelease(ST_LSHIFT);
            }
            vkbd_key_held = 0;
        }

        /* B = toggle shift */
        if (cur_b && !prev_b) {
            vkbd_shift = !vkbd_shift;
        }

        prev_up = cur_up;
        prev_down = cur_down;
        prev_left = cur_left;
        prev_right = cur_right;
        prev_a = cur_a;
        prev_b = cur_b;
        return;  /* Don't process game input when VKBD active */
    }

    prev_up = cur_up;
    prev_down = cur_down;
    prev_left = cur_left;
    prev_right = cur_right;
    prev_a = cur_a;
    prev_b = cur_b;
    /* NOTE: prev_x/prev_y updated at END of function for edge detection */

    /* Input mode: Joystick or Mouse */
    if (sf2000_input_mode == 0) {
        /* JOYSTICK MODE - Player 1 */
        uint8_t joystate = 0;

        if (cur_up) joystate |= JOY_UP;
        if (cur_down) joystate |= JOY_DOWN;
        if (cur_left) joystate |= JOY_LEFT;
        if (cur_right) joystate |= JOY_RIGHT;
        if (cur_a || cur_b) joystate |= JOY_BUTTON;

        if (joystate != g_cached_joy0) {
            g_cached_joy0 = joystate;
            IkbdJoystickChange(0, joystate);
        }

        castaway_joy_up    = (joystate & JOY_UP) ? 1 : 0;
        castaway_joy_down  = (joystate & JOY_DOWN) ? 1 : 0;
        castaway_joy_left  = (joystate & JOY_LEFT) ? 1 : 0;
        castaway_joy_right = (joystate & JOY_RIGHT) ? 1 : 0;
        castaway_joy_fire  = (joystate & JOY_BUTTON) ? 1 : 0;

        /* v025: X = Space key */
        static int space_pressed = 0;
        if (cur_x && !prev_x) {
            IkbdKeyPress(ST_SPACE);
            space_pressed = 1;
        } else if (!cur_x && prev_x && space_pressed) {
            IkbdKeyRelease(ST_SPACE);
            space_pressed = 0;
        }

        /* v025: Y = Enter/Return key */
        static int enter_pressed = 0;
        if (cur_y && !prev_y) {
            IkbdKeyPress(ST_RETURN);
            enter_pressed = 1;
        } else if (!cur_y && prev_y && enter_pressed) {
            IkbdKeyRelease(ST_RETURN);
            enter_pressed = 0;
        }

        /* v021: Player 2 input from second controller (SF2000 wireless) */
        if (sf2000_player2_enabled) {
            static uint8_t g_cached_joy1 = 0;
            uint8_t joystate2 = 0;

            /* Read from libretro player 1 (second controller) */
            int p2_up    = input_state_cb(1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP);
            int p2_down  = input_state_cb(1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN);
            int p2_left  = input_state_cb(1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT);
            int p2_right = input_state_cb(1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT);
            int p2_a     = input_state_cb(1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A);
            int p2_b     = input_state_cb(1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B);

            if (p2_up) joystate2 |= JOY_UP;
            if (p2_down) joystate2 |= JOY_DOWN;
            if (p2_left) joystate2 |= JOY_LEFT;
            if (p2_right) joystate2 |= JOY_RIGHT;
            if (p2_a || p2_b) joystate2 |= JOY_BUTTON;

            /* Send to Atari ST Port 0 (mouse port, used for Player 2) */
            if (joystate2 != g_cached_joy1) {
                g_cached_joy1 = joystate2;
                IkbdJoystickChange(1, joystate2);
            }
        }
    } else {
        /* MOUSE MODE - D-pad controls mouse movement
         * CRITICAL: IkbdMouseMotion uses (x - prev_x) internally, NOT dx!
         * So we must track absolute position like events.cpp does */
        static int mouse_x = 160;  /* Absolute position, start at center */
        static int mouse_y = 100;

        int new_x = mouse_x;
        int new_y = mouse_y;

        if (cur_left)  new_x -= MOUSE_SPEED;
        if (cur_right) new_x += MOUSE_SPEED;
        if (cur_up)    new_y -= MOUSE_SPEED;
        if (cur_down)  new_y += MOUSE_SPEED;

        /* Clamp to screen bounds (320x200 low-res) */
        if (new_x < 0) new_x = 0;
        if (new_x > 319) new_x = 319;
        if (new_y < 0) new_y = 0;
        if (new_y > 199) new_y = 199;

        /* Only call IkbdMouseMotion if position changed */
        if (new_x != mouse_x || new_y != mouse_y) {
            int dx = new_x - mouse_x;
            int dy = new_y - mouse_y;
            /* Pass current position AND delta (function uses position diff internally) */
            IkbdMouseMotion(new_x, new_y, dx, dy);
            mouse_x = new_x;
            mouse_y = new_y;
        }

        /* A/B work as mouse buttons in mouse mode */
        /* A = LMB, B = RMB (easier to press than shoulders) */
        if (cur_a && !mouse_a_lmb_pressed) {
            IkbdMousePress(2);  /* LMB (bit 1) */
            mouse_a_lmb_pressed = 1;
        } else if (!cur_a && mouse_a_lmb_pressed) {
            IkbdMouseRelease(2);
            mouse_a_lmb_pressed = 0;
        }

        if (cur_b && !mouse_b_rmb_pressed) {
            IkbdMousePress(1);  /* RMB (bit 0) */
            mouse_b_rmb_pressed = 1;
        } else if (!cur_b && mouse_b_rmb_pressed) {
            IkbdMouseRelease(1);
            mouse_b_rmb_pressed = 0;
        }

        /* Clear joystick state */
        if (g_cached_joy0 != 0) {
            g_cached_joy0 = 0;
            IkbdJoystickChange(0, 0);
        }
        castaway_joy_up = castaway_joy_down = castaway_joy_left = castaway_joy_right = castaway_joy_fire = 0;
    }

    /* Store all button states */
    libretro_input_state[RETRO_DEVICE_ID_JOYPAD_UP]     = cur_up;
    libretro_input_state[RETRO_DEVICE_ID_JOYPAD_DOWN]   = cur_down;
    libretro_input_state[RETRO_DEVICE_ID_JOYPAD_LEFT]   = cur_left;
    libretro_input_state[RETRO_DEVICE_ID_JOYPAD_RIGHT]  = cur_right;
    libretro_input_state[RETRO_DEVICE_ID_JOYPAD_A]      = cur_a;
    libretro_input_state[RETRO_DEVICE_ID_JOYPAD_B]      = cur_b;
    libretro_input_state[RETRO_DEVICE_ID_JOYPAD_START]  = cur_start;
    libretro_input_state[RETRO_DEVICE_ID_JOYPAD_L]      = cur_l;
    libretro_input_state[RETRO_DEVICE_ID_JOYPAD_R]      = cur_r;

    /* v025: Update X/Y prev state at END for proper edge detection */
    prev_x = cur_x;
    prev_y = cur_y;
}

/*===========================================================================*/
/* FPS COUNTER - v023 (like QPSX)                                            */
/*===========================================================================*/

/* FPS color thresholds (green/yellow/red like QPSX) */
#define FPS_GOOD     0x07E0  /* Green: >= 25 fps (50% of target) */
#define FPS_OK       0xFFE0  /* Yellow: >= 15 fps */
#define FPS_BAD      0xF800  /* Red: < 15 fps */
#define FPS_BG       0x0000  /* Black background */

/* Update FPS counter - called every frame */
static void update_fps_counter(void)
{
    fps_frame_count++;

    /* Check if one second has passed (50 frames at PAL 50Hz) */
    int elapsed_frames = libretro_frame_count - fps_last_frame;
    if (elapsed_frames >= 50) {
        /* Calculate FPS based on frames rendered in the elapsed time */
        fps_current = (fps_frame_count * 50) / elapsed_frames;
        fps_frame_count = 0;
        fps_last_frame = libretro_frame_count;
    }
}

/* Draw FPS overlay in top-left corner (like QPSX) */
static void draw_fps_overlay(uint16_t *fb)
{
    if (!fps_show || !fb || sf2000_menu_active) return;

    /* Choose color based on FPS value */
    uint16_t col;
    if (fps_current >= 25) col = FPS_GOOD;       /* Green: >= 50% target */
    else if (fps_current >= 15) col = FPS_OK;    /* Yellow: >= 30% target */
    else col = FPS_BAD;                          /* Red: < 30% target */

    /* Draw small black background box */
    draw_rect(fb, 2, 2, 24, 11, FPS_BG);

    /* Draw FPS value */
    char buf[8];
    snprintf(buf, sizeof(buf), "%2d", fps_current > 99 ? 99 : fps_current);
    draw_text(fb, 4, 3, buf, col);
}

/*
 * Main run loop - called ~50 times per second
 */

void retro_run(void)
{
    static int Deffered = 0;

    libretro_frame_count++;

    if (frame_buffer == NULL || screen == NULL) {
        video_cb(NULL, CASTAWAY_WIDTH, SCREEN_HEIGHT, CASTAWAY_WIDTH << 1);
        return;
    }

    if (Deffered == 0) {
        Deffered = 1;
        video_cb(frame_buffer, CASTAWAY_WIDTH, SCREEN_HEIGHT, CASTAWAY_WIDTH << 1);
        return;
    }

    if (Deffered == 1) {
        Deffered = 2;
    }

    /* Poll input */
    poll_input();

    /* Run emulation if not paused by menu */
    if (emulating && !sf2000_menu_active) {
        dcastaway_one_frame();

        /* v028: Re-apply CPU boost after each frame
         * dcastaway.cpp may reset emu_hsync_add during video mode changes */
        if (sf2000_cpu_boost > 0) {
            apply_cpu_boost();
        }
#ifndef NO_SOUND
        /* Update sound and push audio - v017 */
        Sound_Update_VBL();
        libretro_push_audio();
#endif
    }

    /* v032: REMOVED border clearing completely!
     * Some demos/cracktros use overscan (320x240) which fills the entire screen.
     * Border clearing was cutting off top/bottom 20 lines of overscan content.
     * The Atari ST emulator (render.cpp) handles borders itself via screen_add.
     * If screen_add=20: content at rows 20-219, borders at 0-19 and 220-239
     * If screen_add=0: content may span full 0-239 (overscan mode) */

    /* Draw menu overlay if active */
    if (sf2000_menu_active) {
        sf2000_draw_menu(frame_buffer);
    }

    /* v019: Draw virtual keyboard if active */
    if (vkbd_active) {
        sf2000_draw_vkbd(frame_buffer);
    }

    /* v023: FPS counter update and draw */
    update_fps_counter();
    draw_fps_overlay(frame_buffer);

    /* v024: Hard frameskip - skip video_cb for N frames
     * This VISIBLY skips frames and reduces frontend load */
    static int video_skip_count = 0;
    if (sf2000_frameskip > 0 && !sf2000_menu_active && !vkbd_active) {
        video_skip_count++;
        if (video_skip_count < sf2000_frameskip) {
            /* Skip this frame - tell frontend no new frame */
            video_cb(NULL, CASTAWAY_WIDTH, SCREEN_HEIGHT, CASTAWAY_WIDTH << 1);
            return;
        }
        video_skip_count = 0;
    }

    /* Push framebuffer */
    video_cb(frame_buffer, CASTAWAY_WIDTH, SCREEN_HEIGHT, CASTAWAY_WIDTH << 1);
}

/*
 * Reset
 */

void retro_reset(void)
{
    /* TODO: Implement ST reset */
    /* For now, just reinitialize memory */
    MemQuit();
    MemInit();
    emulating = 1;
}

/*
 * Save states (not implemented yet)
 */

size_t retro_serialize_size(void)
{
    return 0;
}

int retro_serialize(void *data, size_t size)
{
    (void)data;
    (void)size;
    return 0;
}

int retro_unserialize(const void *data, size_t size)
{
    (void)data;
    (void)size;
    return 0;
}

/*
 * Cheats (not implemented)
 */

void retro_cheat_reset(void) {}
void retro_cheat_set(unsigned index, int enabled, const char *code)
{
    (void)index;
    (void)enabled;
    (void)code;
}

/*
 * Controller port
 */

void retro_set_controller_port_device(unsigned port, unsigned device)
{
    (void)port;
    (void)device;
}

/*
 * Region
 */

unsigned retro_get_region(void)
{
    return RETRO_REGION_PAL;
}

/*
 * Memory access (not implemented)
 */

void *retro_get_memory_data(unsigned id)
{
    (void)id;
    return NULL;
}

size_t retro_get_memory_size(unsigned id)
{
    (void)id;
    return 0;
}

/*
 * ============================================================
 * LIBRETRO PLATFORM ABSTRACTION
 * These functions replace SDL calls in Castaway
 * ============================================================
 */

/* Framebuffer pointer for render.cpp */
uint16_t *libretro_get_framebuffer(void)
{
    return frame_buffer;
}

unsigned libretro_get_screen_pitch(void)
{
    return CASTAWAY_WIDTH << 1;  /* UAE4ALL pattern: << 1 for 16-bit pitch */
}

unsigned libretro_get_screen_width(void)
{
    return CASTAWAY_WIDTH;
}

unsigned libretro_get_screen_height(void)
{
    return SCREEN_HEIGHT;
}

/* Called by render.cpp to flip the display (no-op for libretro) */
void libretro_video_flip(void)
{
    /* Nothing to do - video_cb is called in retro_run */
}

/*
 * ============================================================
 * AUDIO SUPPORT - v017
 * Replace SDL audio callback with libretro push model
 * ============================================================
 */
#ifndef NO_SOUND
/* These functions are called by sound.cpp */
bool bSoundWorking = true;
volatile bool bPlayingBuffer = false;

void Audio_Lock(void) { /* No locking needed in libretro */ }
void Audio_Unlock(void) { /* No locking needed in libretro */ }

void Audio_EnableAudio(bool bEnable)
{
    bPlayingBuffer = bEnable;
}

void audio_init(void)
{
    Sound_Init();
    bSoundWorking = true;
    bPlayingBuffer = true;
}

void audio_stop(void)
{
    bPlayingBuffer = false;
}

/* Push audio samples to libretro frontend
 * Called from retro_run() after dcastaway_one_frame() */
static void libretro_push_audio(void)
{
    if (!bPlayingBuffer || !audio_batch_cb) return;

    /* MixBuffer contains 8-bit signed mono samples
     * We need to convert to 16-bit stereo for libretro */
    static int16_t audio_out[SOUND_SAMPLES_PER_FRAME * 2];

    int samples_available = nGeneratedSamples;
    if (samples_available > SOUND_SAMPLES_PER_FRAME)
        samples_available = SOUND_SAMPLES_PER_FRAME;

    if (samples_available > 0) {
        for (int i = 0; i < samples_available; i++) {
            /* Get 8-bit signed sample from MixBuffer */
            int8_t sample8 = MixBuffer[(CompleteSndBufIdx + i) % MIXBUFFER_SIZE];
            /* Convert to 16-bit */
            int16_t sample16 = (int16_t)sample8 << 8;
            /* Stereo: duplicate to both channels */
            audio_out[i * 2] = sample16;
            audio_out[i * 2 + 1] = sample16;
        }

        /* Advance buffer index */
        CompleteSndBufIdx = (CompleteSndBufIdx + samples_available) % MIXBUFFER_SIZE;
        nGeneratedSamples -= samples_available;

        /* Push to libretro */
        audio_batch_cb(audio_out, samples_available);
    }
}
#else
/* NO_SOUND stubs */
static void libretro_push_audio(void) {}
#endif

/* Audio push (called by sound.cpp) */
void libretro_audio_push(int16_t *samples, size_t num_samples)
{
    if (audio_batch_cb && num_samples > 0) {
        audio_batch_cb(samples, num_samples);
    }
}

/* Get TOS ROM path */
const char *libretro_get_tos_path(void)
{
    return tos_rom_path;
}

/* Get disk image path */
const char *libretro_get_disk_path(void)
{
    return disk_path;
}
