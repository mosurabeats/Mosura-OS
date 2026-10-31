/*
 * Mosura OS: SP-404MKII port (glue between the core and Roland's firmware).
 *
 * STATUS: skeleton. The entry points below are what the patched firmware
 * will call; the fw_* pointers are filled from verified hook addresses
 * (port/sp404/hooks.json). Until a hook is verified its pointer is NULL and
 * the glue does nothing, so this file compiles and links but changes nothing.
 *
 * MIT licence.
 */
#include "mosura.h"

/* ---- firmware entry points (set by the patcher once verified) ---------- */
typedef void (*fw_note_fn)(int pad_index, int velocity);
typedef void (*fw_page_fn)(int page);
typedef void (*fw_led_fn)(int index, int value);

struct fw_table {
    uint32_t   magic;                 /* 'MOSU' - the patcher checks it */
    fw_note_fn note_on, note_off;
    fw_page_fn request_page;          /* FUN_800da598 candidate */
    fw_led_fn  led;                   /* BMC LED write */
    volatile uint32_t *cyccnt;        /* DWT_CYCCNT 0xE0001004 */
};
struct fw_table mosura_fw = { 0x55534F4Du, 0, 0, 0, 0, 0 };

static int active;                    /* Mosura owns the screen */
static uint32_t last_cyc;
static mos_leds_t shown;

/* ---- called by the patched firmware ------------------------------------- */
void mosura_boot(void) { mos_init(); active = 1; if (mosura_fw.cyccnt) last_cyc = *mosura_fw.cyccnt; }

/* returns 1 if Mosura consumed the key; 0 = run the original handler */
int mosura_on_key(int id, int down) { return mos_key(id, down); }
int mosura_on_pad(int pad, int velocity) { return active ? mos_pad(pad, velocity) : 0; }
int mosura_on_value(int detents) { return active ? mos_encoder(detents) : 0; }
void mosura_on_ctrl(int knob, int value) { mos_ctrl(knob, value); }

/* from the UI task loop: advance time, then refresh LEDs */
void mosura_tick(void)
{
    if (!mosura_fw.cyccnt) return;
    uint32_t now = *mosura_fw.cyccnt, us = (now - last_cyc) / 600u;
    if (!us) return;
    last_cyc += us * 600u;
    mos_tick(us);
    if (!mosura_fw.led) return;
    const mos_leds_t *l = mos_leds();
    for (int k = K_BUS_FX; k < K_COUNT; k++)
        if (l->key[k] != shown.key[k]) { mosura_fw.led(k + 0x21, l->key[k] >> 1); shown.key[k] = l->key[k]; }
    for (int p = 0; p < MOS_PADS; p++)
        for (int c = 0; c < 3; c++)
            if (l->pad[p][c] != shown.pad[p][c]) { mosura_fw.led(p * 3 + c, l->pad[p][c] >> 1); shown.pad[p][c] = l->pad[p][c]; }
}

/* from the OLED flush: return the frame to send, or 0 for the stock one */
const uint8_t *mosura_frame(void) { return active ? mos_render() : 0; }

/* ---- the core's host callbacks ------------------------------------------ */
void mos_host_note_on(int note, int velocity, int tune, int start_pm, int end_pm)
{
    (void)tune; (void)start_pm; (void)end_pm;    /* per-note tune/trim: later phase */
    if (mosura_fw.note_on) mosura_fw.note_on(note, velocity);
}
void mos_host_note_off(int note) { if (mosura_fw.note_off) mosura_fw.note_off(note, 0); }
void mos_host_click(int accent) { (void)accent; }
void mos_host_fx(int fx, int on, int p1, int p2, int p3) { (void)fx; (void)on; (void)p1; (void)p2; (void)p3; }
void mos_host_pad_param(int note, int param, int value) { (void)note; (void)param; (void)value; }
void mos_host_chop(int dst, int src, int a, int b) { (void)dst; (void)src; (void)a; (void)b; }
void mos_host_stock(int on)
{
    active = !on;
    if (on && mosura_fw.request_page) mosura_fw.request_page(68);   /* stock main page */
}
