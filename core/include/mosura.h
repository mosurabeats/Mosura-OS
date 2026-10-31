/*
 * Mosura OS core: public interface.
 *
 * The core is freestanding C99. It owns the screen, the sequencer and the
 * MPC-style workflow, and knows nothing about where it runs. A host (the
 * browser simulator, or the SP-404MKII port) feeds it time and input and
 * implements the mos_host_* callbacks at the bottom of this file.
 *
 * Copyright (c) 2026 Mosura OS contributors. MIT licence (see LICENSE).
 */
#ifndef MOSURA_H
#define MOSURA_H

#include <stdint.h>

#define MOS_W 128
#define MOS_H 64
/* SSD1309 native layout: 8 pages of 128 columns, one byte = 8 vertical
 * pixels, least significant bit at the top. */
#define MOS_FB_BYTES (MOS_W * MOS_H / 8)

#define MOS_PADS   16
#define MOS_BANKS  10            /* A..J, as on the unit                   */
#define MOS_NOTES  (MOS_PADS * MOS_BANKS)
#define MOS_PPQ    96            /* MPC2000XL resolution                    */
#define MOS_WAVE_W 96            /* peak columns per sample for TRIM view   */

/* Key IDs. These are the SP-404MKII firmware's own key numbers (see
 * docs/HARDWARE.md), so the device port can pass them straight through. */
enum {
    K_BUS_FX = 0x10, K_HOLD, K_EXT_SOURCE, K_SUB_PAD,
    K_PATTERN_SELECT, K_PATTERN_EDIT, K_RECORD_SETTING, K_START_END,
    K_PITCH_SPEED, K_MARK, K_DEL, K_REC,
    K_RESAMPLE, K_BPM_SYNC, K_GATE, K_LOOP,
    K_REVERSE, K_ROLL, K_EXIT, K_COPY,
    K_REMAIN, K_BANK_AF, K_BANK_BG, K_BANK_CH,
    K_BANK_DI, K_BANK_EJ, K_SHIFT,
    K_FX1, K_FX2, K_FX3, K_FX4, K_FX5, K_FX6,   /* FILTER+DRIVE .. MFX */
    K_VALUE_PUSH,                               /* 0x31                */
    K_COUNT
};

/* LED state handed to the host every frame. Levels 0..255. */
typedef struct {
    uint8_t key[K_COUNT];        /* indexed by key ID (0x10..0x31 used)     */
    uint8_t pad[MOS_PADS][3];    /* RGB                                     */
} mos_leds_t;

/* ---- driving the core ------------------------------------------------- */
void              mos_init(void);
void              mos_tick(uint32_t microseconds);
/* Input returns 1 when Mosura consumed it, 0 when the host should hand it to
 * the stock firmware (device) or ignore it (simulator). */
int               mos_key(int key, int down);
int               mos_pad(int pad, int velocity);   /* 0 = release        */
int               mos_encoder(int detents);
void              mos_ctrl(int knob, int value);    /* knob 0..2, 0..1023  */
const uint8_t    *mos_render(void);                 /* MOS_FB_BYTES        */
const mos_leds_t *mos_leds(void);
int               mos_bank(void);                   /* 0..9 = A..J         */

/* Sample information the host knows and the core shows. */
void mos_set_sample(int note, const char *name, const uint8_t *peaks,
                    uint32_t frames);

/* ---- implemented by the host ----------------------------------------- */
void mos_host_note_on(int note, int velocity, int tune, int start_pm,
                      int end_pm);
void mos_host_note_off(int note);
void mos_host_click(int accent);
/* fx 0..5 (the six effect buttons), params 0..1023 */
void mos_host_fx(int fx, int on, int p1, int p2, int p3);
/* A pad parameter changed in PROGRAM / MIXER. */
enum { MOS_P_LEVEL, MOS_P_PAN, MOS_P_TUNE, MOS_P_DECAY, MOS_P_FLAGS };
void mos_host_pad_param(int note, int param, int value);
/* CHOP made a new pad: dst plays src from start to end (per mille). */
void mos_host_chop(int dst_note, int src_note, int start_pm, int end_pm);
/* The user asked for the stock screen (device) - the core goes idle until
 * the next mode key. */
void mos_host_stock(int on);

#endif
