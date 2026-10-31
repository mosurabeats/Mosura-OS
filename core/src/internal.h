/* Mosura OS core: shared internals. MIT licence. */
#ifndef MOS_INTERNAL_H
#define MOS_INTERNAL_H

#include <stdarg.h>
#include <stddef.h>
#include "mosura.h"

/* ---- tiny libc ---------------------------------------------------------- */
void  *mos_memset(void *d, int c, size_t n);
void  *mos_memcpy(void *d, const void *s, size_t n);
int    mos_strlen(const char *s);
void   mos_strcpy(char *d, const char *s, int max);
int    mos_vfmt(char *out, int max, const char *f, va_list ap);
int    mos_fmt(char *out, int max, const char *f, ...);
#define CLAMP(v, lo, hi) ((v) < (lo) ? (lo) : (v) > (hi) ? (hi) : (v))
#define ABS(v) ((v) < 0 ? -(v) : (v))

/* ---- graphics ----------------------------------------------------------- */
enum { C_OFF = 0, C_ON = 1, C_XOR = 2 };
enum { T_INV = 1, T_X2 = 2, T_RIGHT = 4, T_CENTER = 8 };
extern uint8_t g_fb[MOS_FB_BYTES];
void g_clear(void);
void g_px(int x, int y, int c);
void g_hline(int x0, int x1, int y, int c);
void g_vline(int x, int y0, int y1, int c);
void g_dots(int x0, int x1, int y, int step);           /* dotted hline   */
void g_vdots(int x, int y0, int y1, int step);
void g_line(int x0, int y0, int x1, int y1, int c);
void g_fill(int x, int y, int w, int h, int c);
void g_frame(int x, int y, int w, int h, int c);
void g_rframe(int x, int y, int w, int h, int c);       /* rounded         */
void g_rfill(int x, int y, int w, int h, int c);
void g_dither(int x, int y, int w, int h, int level);   /* 1..3 of 4       */
int  g_text(int x, int y, const char *s, int flags);    /* returns width   */
int  g_textf(int x, int y, int flags, const char *f, ...);
int  g_textw(const char *s, int flags);
void g_bignum(int x, int y, int value, int digits, int dp, int h);
void g_box_label(int x, int y, int w, const char *s, int on);

/* ---- sequencer ---------------------------------------------------------- */
#define SEQS        16
#define TRACKS      8
#define EVENTS      768
#define SONG_STEPS  32
#define BAR_TICKS(seq) ((uint32_t)(seq)->beats * MOS_PPQ)

typedef struct {
    uint32_t tick;
    uint16_t dur;
    uint8_t  note;      /* bank * 16 + pad */
    uint8_t  vel;
    int8_t   tune;      /* semitones, from 16 LEVELS: TUNE */
    uint8_t  track;
} event_t;

typedef struct {
    char    name[10];
    uint8_t mute, solo;
    uint8_t vel_pct;    /* 1..200 */
} track_t;

typedef struct {
    char     name[12];
    uint8_t  used;
    uint8_t  bars;      /* 1..99 */
    uint8_t  beats;     /* per bar, 1..16 (quarter notes) */
    uint8_t  loop;
    uint16_t tempo10;   /* BPM x 10, used when tempo source = SEQ */
    track_t  tr[TRACKS];
    uint16_t nev;
    event_t  ev[EVENTS];
} seq_t;

typedef struct { uint8_t seq, reps; } song_step_t;

typedef struct {
    uint8_t level, pan, decay, flags;   /* pan 0..100, centre 50 */
    int8_t  tune;                       /* semitones -24..24 */
    uint8_t mute_grp;
    int16_t start_pm, end_pm;           /* per mille */
} pad_t;
enum { PF_GATE = 1, PF_LOOP = 2, PF_REVERSE = 4, PF_BPMSYNC = 8, PF_MUTE = 16 };

typedef struct {
    char     name[13];
    uint8_t  peaks[MOS_WAVE_W];
    uint32_t frames;
    uint8_t  has;
} sample_t;

/* timing correct choices */
extern const char *const tc_names[];
extern const uint16_t tc_ticks[];
#define TC_COUNT 8

typedef struct {
    /* project */
    seq_t      seq[SEQS];
    song_step_t song[SONG_STEPS];
    uint8_t    song_len;
    pad_t      pad[MOS_NOTES];
    sample_t   smp[MOS_NOTES];
    uint16_t   tempo10;          /* master tempo */
    uint8_t    tempo_src_seq;
    /* transport */
    uint8_t    cur_seq, cur_track, next_seq; /* next_seq 0xff = none */
    uint8_t    playing, recording, overdub_armed, count_in, counting;
    uint8_t    song_mode, song_pos, song_rep;
    uint32_t   pos;              /* tick */
    uint64_t   acc;              /* tick fraction accumulator */
    uint8_t    tc, swing;        /* swing 50..75 */
    uint8_t    click_on;
    /* note-offs pending (sequencer and live) */
    struct { uint8_t note; uint32_t off; } held[32];
    uint8_t    nheld;
    /* live pads */
    uint8_t    bank;
    uint32_t   pad_down_tick[MOS_PADS];
    int16_t    pad_rec_idx[MOS_PADS];   /* event index of a held recorded note */
    uint8_t    pad_vel[MOS_PADS];
    uint8_t    last_note;
    uint8_t    full_level;
    uint32_t   guard_tick[MOS_PADS];    /* skip a just-recorded note once */
    uint8_t    guard_note[MOS_PADS], guard_on[MOS_PADS];
    /* note repeat */
    uint16_t   rpt_mask;
    uint8_t    rpt_vel[MOS_PADS];
    uint8_t    rpt_tc;
    uint32_t   free_pos;
    uint64_t   free_acc;
    /* undo */
    seq_t      undo;
    uint8_t    undo_ok, undo_seq;
    /* time */
    uint32_t   ms;
    uint64_t   us_frac;
} mos_state_t;

extern mos_state_t M;

void   seq_init(void);
void   seq_tick(uint32_t us);
void   seq_play(int from_start);
void   seq_stop(void);
void   seq_rec_toggle(void);
void   seq_live_note(int pad, int vel);           /* pad down/up from pads */
void   seq_trigger(int note, int vel, int tune, uint32_t dur_ticks);
uint32_t seq_len_ticks(const seq_t *s);
int    seq_add_event(seq_t *s, const event_t *e);
void   seq_del_event(seq_t *s, int i);
void   seq_sort(seq_t *s);
void   seq_snapshot(void);                        /* for UNDO */
void   seq_undo(void);
uint32_t seq_quantize(uint32_t tick);
void   seq_bbc(uint32_t tick, int *bar, int *beat, int *clk);
void   seq_ensure(int i);
uint16_t seq_tempo(void);
void   seq_set_tempo(int t10);
void   seq_tap(void);
void   seq_erase_note(int note);                  /* ERASE while playing */
void   seq_all_off(void);
void   seq_repeat_fire(void);

/* ---- ui ----------------------------------------------------------------- */
void   ui_init(void);
void   ui_draw(void);
int    ui_key(int key, int down);
int    ui_pad(int pad, int vel);
void   ui_encoder(int d);
void   ui_ctrl(int knob, int v);
void   ui_tick_ms(void);
void   ui_pad_note(int pad, int on);   /* LED feedback when a pad sounds */
void   ui_beat(int beat, int bar_start);

#endif
