/*
 * Mosura OS core: screens and the control model.
 *
 * The control model (docs/DESIGN.md has the reasoning):
 *   VALUE turn          data wheel: changes the field under the cursor
 *   VALUE push + turn   moves the cursor
 *   VALUE tap           WINDOW: opens the detail window for the field
 *   EXIT                closes a window / goes back
 *   SHIFT tap           MODE grid; SHIFT + pad jumps to a mode directly
 *   FX buttons          soft keys F1..F6 on edit screens, effects on
 *                       performance screens; the footer always says which
 *   CTRL 1..3           Q-Links: three parameters per screen, soft takeover
 *   PATTERN SELECT      PLAY / STOP        (SHIFT: play from here)
 *   PATTERN EDIT        REC (overdub) arm and punch in/out
 *   REMAIN              UNDO SEQ           DEL + pad: ERASE while playing
 *   ROLL + pad          NOTE REPEAT at the timing-correct rate
 *
 * MIT licence.
 */
#include "internal.h"

enum {
    S_MAIN, S_STEP, S_GRID, S_SONG, S_PROG, S_MIXER, S_TRIM, S_SETUP,
    S_NEXTSEQ, S_MUTE, S_16LEV, S_REPEAT, S_FULL, S_TAP, S_HELP, S_STOCK,
    S_MODES,                                     /* not a mode-grid entry */
    W_TC, W_SEQ, W_TRACK, W_CHOP,                /* windows */
    S_COUNT
};

typedef struct {
    uint8_t x, y;
    const char *lab;
    void (*fmt)(char *b);
    void (*adj)(int d);
    uint8_t win;
} field_t;

typedef struct {
    const char *title;
    const field_t *f;
    uint8_t nf;
    void (*draw)(void);
    int  (*pad)(int pad, int vel);        /* 1 = handled, else pads play   */
    void (*soft)(int i);
    const char *(*softlab)(int i);        /* NULL = performance screen     */
    void (*q)(int knob, int v);
    int  (*qget)(int knob);
    const char *qlab[3];
    void (*enc)(int d);                   /* custom data wheel             */
    void (*move)(int d);                  /* custom cursor                 */
    void (*tap)(void);                    /* custom VALUE tap              */
    uint8_t modal;
} screen_t;

static const screen_t *SCR[S_COUNT];
static uint8_t scr = S_MAIN, prev_scr = S_MAIN, under = S_MAIN;
static uint8_t cur[S_COUNT];

/* input state */
static uint8_t shift_held, shift_used, push_held, push_turned, del_held, roll_held;
static uint32_t shift_t, stock_shift_t;
static uint8_t fx_on[6];
static int     ctrl_v[3], ctrl_eng[3];
static uint8_t ctrl_owner[3];

/* feedback */
static char    toast[24];
static uint32_t toast_until;
static char    qpop[24];
static uint32_t qpop_until;
static uint8_t pad_glow[MOS_PADS];
static uint8_t beat_flash;
static uint8_t blink;
static mos_leds_t leds;

/* screen-local state */
static uint8_t grid_row, grid_step, grid_res = 2, grid_vel = 100;
static uint16_t grid_bar;
static uint8_t mix_view, lev_type, slices = 8, help_top;
static int16_t step_sel;

static const char *const bank_letters = "ABCDEFGHIJ";

/* ---- helpers ------------------------------------------------------------ */
static seq_t *cs(void) { return &M.seq[M.cur_seq]; }
static void say(const char *s) { mos_strcpy(toast, s, sizeof toast); toast_until = M.ms + 900; }
static void note_name(char *b, int note) { mos_fmt(b, 6, "%c%02d", bank_letters[note / MOS_PADS], note % MOS_PADS + 1); }
static void qshow(const char *lab, const char *val) { mos_fmt(qpop, sizeof qpop, "%s %s", lab, val); qpop_until = M.ms + 700; }

static void go(int s)
{
    if (s == S_FULL) { M.full_level = !M.full_level; say(M.full_level ? "FULL LEVEL ON" : "FULL LEVEL OFF"); return; }
    if (s == S_TAP) { seq_tap(); say("TAP"); return; }
    if (s == S_STOCK) { mos_host_stock(1); scr = S_STOCK; return; }
    if (SCR[s] && SCR[s]->modal) { under = SCR[scr]->modal ? under : scr; }
    else prev_scr = scr;
    scr = (uint8_t)s;
    for (int k = 0; k < 3; k++) ctrl_eng[k] = 0;
}
static void back(void)
{
    if (SCR[scr]->modal) scr = under;
    else if (scr == S_MODES) scr = prev_scr == S_MODES ? S_MAIN : prev_scr;
    else if (scr != S_MAIN) scr = S_MAIN;
}

/* ---- MAIN ---------------------------------------------------------------- */
static void f_sq(char *b) { mos_fmt(b, 20, "%02d-%s", M.cur_seq + 1, cs()->name); }
static void a_sq(int d)
{
    int n = CLAMP(M.cur_seq + d, 0, SEQS - 1);
    if (M.playing) { M.next_seq = (uint8_t)n; seq_ensure(n); return; }   /* queued, as on the MPC */
    M.cur_seq = (uint8_t)n; seq_ensure(n); M.pos = 0;
}
static void f_tr(char *b) { mos_fmt(b, 20, "%d-%s", M.cur_track + 1, cs()->tr[M.cur_track].name); }
static void a_tr(int d) { M.cur_track = (uint8_t)CLAMP(M.cur_track + d, 0, TRACKS - 1); }
static void f_loop(char *b) { mos_strcpy(b, cs()->loop ? "ON" : "OFF", 8); }
static void a_loop(int d) { (void)d; cs()->loop = !cs()->loop; }
static void f_bars(char *b) { mos_fmt(b, 8, "%02d", cs()->bars); }
static void a_bars(int d) { cs()->bars = (uint8_t)CLAMP(cs()->bars + d, 1, 99); if (M.pos >= seq_len_ticks(cs())) M.pos = 0; }
static void f_tc(char *b) { mos_strcpy(b, tc_names[M.tc], 8); }
static void a_tc(int d) { M.tc = (uint8_t)CLAMP(M.tc + d, 0, TC_COUNT - 1); }
static void f_sw(char *b) { mos_fmt(b, 8, "%d%%", M.swing); }
static void a_sw(int d) { M.swing = (uint8_t)CLAMP(M.swing + d, 50, 75); }
static void f_tempo(char *b) { int t = seq_tempo(); mos_fmt(b, 10, "%d.%d", t / 10, t % 10); }
static void a_tempo(int d) { seq_set_tempo(seq_tempo() + (shift_held ? d * 10 : d)); }
static void f_beats(char *b) { mos_fmt(b, 8, "%d/4", cs()->beats); }
static void a_beats(int d) { cs()->beats = (uint8_t)CLAMP(cs()->beats + d, 1, 16); M.pos = 0; }

static const field_t main_f[] = {
    {1, 11, "SQ", f_sq, a_sq, W_SEQ},
    {1, 18, "TR", f_tr, a_tr, W_TRACK},
    {1, 25, "LOOP", f_loop, a_loop, W_SEQ},
    {35, 25, "BARS", f_bars, a_bars, W_SEQ},
    {1, 32, "TC", f_tc, a_tc, W_TC},
    {35, 32, "SW", f_sw, a_sw, W_TC},
    {72, 11, NULL, f_tempo, a_tempo, W_SEQ},     /* drawn big */
};

static void draw_field(const field_t *f, int sel)
{
    char b[24]; int x = f->x;
    if (f->lab) { x += g_text(x, f->y, f->lab, 0) + 3; }
    f->fmt(b);
    if (sel) g_text(x, f->y, b, T_INV); else g_text(x, f->y, b, 0);
}

static void draw_timeline(int y, int h)
{
    seq_t *s = cs();
    uint32_t len = seq_len_ticks(s), bt = BAR_TICKS(s);
    int x0 = 1, w = 126;
    g_rframe(0, y, 128, h, C_ON);
    for (uint32_t b = 0; b * MOS_PPQ < len; b++) {
        int x = x0 + (int)((uint64_t)b * MOS_PPQ * w / len);
        if ((b * MOS_PPQ) % bt == 0) { if (b) g_vdots(x, y + 1, y + h - 2, 2); }
        else { g_px(x, y + 1, C_ON); g_px(x, y + h - 2, C_ON); }
    }
    for (int i = 0; i < s->nev; i++) {
        event_t *e = &s->ev[i];
        if (e->tick >= len) continue;
        int x = x0 + (int)((uint64_t)e->tick * w / len);
        if (e->track == M.cur_track) {
            int hh = 1 + e->vel * (h - 4) / 127;
            g_vline(x, y + h - 2 - hh + 1, y + h - 2, C_ON);
        } else g_px(x, y + h - 3, C_ON);
    }
    if (M.playing || M.pos) {
        int x = x0 + (int)((uint64_t)(M.pos % len) * w / len);
        g_fill(x, y + 1, 2, h - 2, C_XOR);
    }
}

static void header(const char *title, const char *right)
{
    int w = g_textw(title, 0);
    g_rfill(0, 0, w + 6, 7, C_ON);
    g_text(3, 1, title, T_INV);
    g_fill(2, 0, 1, 7, C_ON);  /* tidy the inverse box edge */
    if (right) g_text(127, 1, right, T_RIGHT);
    g_dots(w + 8, 127 - g_textw(right ? right : "", 0) - 3, 4, 2);
}

static void status_right(char *b)
{
    int bar, beat, clk; seq_bbc(M.pos, &bar, &beat, &clk);
    char ic = M.recording ? '\x03' : M.playing ? '\x01' : '\x02';
    if (M.overdub_armed && blink) ic = '\x03';
    if (M.counting) mos_fmt(b, 24, "COUNT %c", ic);
    else mos_fmt(b, 24, "%c %03d.%d", ic, bar, beat);
}

static void draw_main(void)
{
    char b[24]; status_right(b);
    header("MAIN", b);
    for (int i = 0; i < 6; i++) draw_field(&main_f[i], cur[S_MAIN] == i);
    /* tempo, big */
    int t = seq_tempo(), sel = cur[S_MAIN] == 6;
    g_rframe(68, 9, 60, 21, C_ON);
    if (sel) g_rfill(68, 9, 60, 21, C_ON);
    g_bignum(71, 12, t, 4, 2, 13);
    if (sel) g_fill(69, 10, 58, 19, C_XOR);
    g_text(126, 23, M.tempo_src_seq ? "SEQ" : "BPM", T_RIGHT | (sel ? T_INV : 0));
    if (M.tempo_src_seq == 0 && sel) {}
    /* track info */
    track_t *tr = &cs()->tr[M.cur_track];
    g_textf(68, 32, 0, "%s%s%s", tr->mute ? "MUTE " : "", tr->solo ? "SOLO " : "",
            M.full_level ? "FULL" : "");
    if (M.next_seq != 0xff) g_textf(127, 32, T_RIGHT | (blink ? T_INV : 0), "NEXT %02d", M.next_seq + 1);
    draw_timeline(39, 15);
}

static int main_qget(int k)
{
    if (k == 0) return (M.swing - 50) * 1023 / 25;
    if (k == 1) return M.tc * 1023 / (TC_COUNT - 1);
    return (seq_tempo() - 300) * 1023 / 2700;
}
static void main_q(int k, int v)
{
    char b[12];
    if (k == 0) { M.swing = (uint8_t)(50 + v * 25 / 1023); f_sw(b); qshow("SWING", b); }
    if (k == 1) { M.tc = (uint8_t)(v * (TC_COUNT - 1) / 1023); f_tc(b); qshow("TC", b); }
    if (k == 2) { seq_set_tempo(300 + v * 2700 / 1023); f_tempo(b); qshow("TEMPO", b); }
}
static const screen_t scr_main = {"MAIN", main_f, 7, draw_main, NULL, NULL, NULL, main_q, main_qget, {"SWING", "TC", "TEMPO"}, NULL, NULL, NULL, 0};

/* ---- MODE grid -------------------------------------------------------- */
static const char *const mode_names[16] = {
    "MAIN", "STEP", "GRID", "SONG", "PROG", "MIXER", "TRIM", "SETUP",
    "NXTSQ", "MUTE", "16LEV", "REPT", "FULLV", "TAP", "HELP", "STOCK"};
static void draw_modes(void)
{
    header("MODE", "PRESS A PAD");
    for (int i = 0; i < 16; i++) {
        int x = (i % 4) * 32, y = 9 + (i / 4) * 11;
        int on = i == prev_scr || (i == S_FULL && M.full_level);
        if (on) g_rfill(x + 1, y, 30, 10, C_ON); else g_rframe(x + 1, y, 30, 10, C_ON);
        g_text(x + 16, y + 3, mode_names[i], T_CENTER | (on ? T_INV : 0));
    }
    g_text(64, 58, "TAP SHIFT TO CLOSE", T_CENTER);
}
static int modes_pad(int pad, int vel) { if (vel) go(pad); return 1; }
static const screen_t scr_modes = {"MODE", NULL, 0, draw_modes, modes_pad, NULL, NULL, NULL, NULL, {0}, NULL, NULL, NULL, 0};

/* ---- STEP (event list) ----------------------------------------------- */
#define STEP_ROWS 5
#define STEP_COLS 5
static int step_idx[EVENTS];
static int step_list(int *idx, int max)          /* events of the current track */
{
    int n = 0; seq_t *s = cs();
    for (int i = 0; i < s->nev && n < max; i++) if (s->ev[i].track == M.cur_track) idx[n++] = i;
    return n;
}
static void step_select_at_pos(void)
{
    int *idx = step_idx, n = step_list(idx, EVENTS);
    step_sel = 0;
    for (int i = 0; i < n; i++) if (cs()->ev[idx[i]].tick >= M.pos) { step_sel = (int16_t)i; return; }
    step_sel = (int16_t)(n ? n - 1 : 0);
}
static void draw_step(void)
{
    char b[24]; int bar, beat, clk;
    seq_bbc(M.pos, &bar, &beat, &clk);
    mos_fmt(b, 24, "TR%d %s", M.cur_track + 1, tc_names[M.tc]);
    header("STEP", b);
    g_textf(1, 10, 0, "NOW %03d.%d.%02d", bar, beat, clk);
    g_textf(127, 10, T_RIGHT, "%d EVENTS", cs()->nev);
    g_hline(0, 127, 16, C_ON);
    int *idx = step_idx, n = step_list(idx, EVENTS);
    if (!n) { g_text(64, 30, "NO EVENTS - PLAY PADS", T_CENTER); g_text(64, 38, "TO STEP RECORD", T_CENTER); return; }
    int row = step_sel, col = cur[S_STEP] % STEP_COLS, top = CLAMP(row - 2, 0, n > STEP_ROWS ? n - STEP_ROWS : 0);
    static const uint8_t cx[STEP_COLS] = {1, 40, 60, 82, 108};
    for (int r = 0; r < STEP_ROWS && top + r < n; r++) {
        event_t *e = &cs()->ev[idx[top + r]];
        int y = 19 + r * 7, sel = top + r == row;
        seq_bbc(e->tick, &bar, &beat, &clk);
        char c[STEP_COLS][12];
        mos_fmt(c[0], 12, "%03d.%d.%02d", bar, beat, clk);
        note_name(c[1], e->note);
        mos_fmt(c[2], 12, "V%d", e->vel);
        mos_fmt(c[3], 12, "D%d", e->dur);
        mos_fmt(c[4], 12, "T%+d", e->tune);
        for (int k = 0; k < STEP_COLS; k++) g_text(cx[k], y, c[k], sel && k == col ? T_INV : 0);
        if (sel && col != 0) g_px(0, y + 2, C_ON);
        if (e->tick <= M.pos && (top + r + 1 >= n || cs()->ev[idx[top + r + 1]].tick > M.pos)) g_text(36, y, "\x06", 0);
    }
    if (n > STEP_ROWS) {                         /* scroll bar */
        int h = 35 * STEP_ROWS / n, y = 18 + (35 - h) * top / (n - STEP_ROWS);
        g_vline(127, y, y + h, C_ON);
    }
}
static event_t *step_event(void)
{
    int *idx = step_idx, n = step_list(idx, EVENTS);
    if (!n) return NULL;
    step_sel = (int16_t)CLAMP(step_sel, 0, n - 1);
    return &cs()->ev[idx[step_sel]];
}
static void step_enc(int d)
{
    event_t *e = step_event(); if (!e) return;
    int col = cur[S_STEP] % STEP_COLS;
    if (col == 0) {
        uint32_t len = seq_len_ticks(cs()), g = tc_ticks[M.tc];
        int t = (int)e->tick + d * (int)g;
        event_t keep = *e;
        keep.tick = (uint32_t)((t % (int)len + (int)len) % (int)len);
        *e = keep; seq_sort(cs());
        int *idx = step_idx, n = step_list(idx, EVENTS);
        for (int i = 0; i < n; i++) { event_t *f = &cs()->ev[idx[i]]; if (f->tick == keep.tick && f->note == keep.note) { step_sel = (int16_t)i; break; } }
    } else if (col == 1) e->note = (uint8_t)CLAMP(e->note + d, 0, MOS_NOTES - 1);
    else if (col == 2) e->vel = (uint8_t)CLAMP(e->vel + d, 1, 127);
    else if (col == 3) e->dur = (uint16_t)CLAMP(e->dur + d * (shift_held ? 24 : 1), 1, 9999);
    else e->tune = (int8_t)CLAMP(e->tune + d, -24, 24);
}
static void step_move(int d)
{
    int *idx = step_idx, n = step_list(idx, EVENTS);
    int c = cur[S_STEP] % STEP_COLS + d, r = step_sel;
    while (c < 0) { c += STEP_COLS; r--; }
    while (c >= STEP_COLS) { c -= STEP_COLS; r++; }
    if (r < 0) { r = 0; c = 0; }
    if (r >= n) { r = n ? n - 1 : 0; c = STEP_COLS - 1; }
    step_sel = (int16_t)r; cur[S_STEP] = (uint8_t)c;
}
static void step_go(int d)
{
    uint32_t len = seq_len_ticks(cs()), g = tc_ticks[M.tc] > 1 ? tc_ticks[M.tc] : 1;
    int p = ((int)(M.pos / g) + d) * (int)g;
    M.pos = (uint32_t)((p % (int)len + (int)len) % (int)len);
    step_select_at_pos();
}
static void step_soft(int i)
{
    event_t *e = step_event();
    if (i == 0) step_go(-1);
    else if (i == 1) step_go(1);
    else if (i == 2) {                            /* INSERT last note here */
        seq_snapshot();
        event_t n = {M.pos, (uint16_t)tc_ticks[M.tc], M.last_note, 100, 0, M.cur_track};
        seq_add_event(cs(), &n); step_select_at_pos(); say("INSERTED");
    } else if (i == 3 && e) { seq_snapshot(); seq_del_event(cs(), (int)(e - cs()->ev)); say("DELETED"); }
    else if (i == 4 && e) { seq_snapshot(); event_t n = *e; n.tick = (n.tick + tc_ticks[M.tc]) % seq_len_ticks(cs()); seq_add_event(cs(), &n); say("DUPLICATED"); }
    else if (i == 5 && e) seq_trigger(e->note, e->vel, e->tune, e->dur);
}
static const char *step_lab(int i) { static const char *const l[6] = {"<STP", "STP>", "INS", "DEL", "DUP", "PLAY"}; return l[i]; }
/* STEP RECORD: a pad press while stopped writes a note here and moves on. */
static int step_pad(int pad, int vel)
{
    if (M.playing || !vel) return 0;
    int note = M.bank * MOS_PADS + pad;
    seq_trigger(note, M.full_level ? 127 : vel, 0, tc_ticks[M.tc]);
    M.last_note = (uint8_t)note;
    seq_snapshot();
    event_t e = {M.pos, (uint16_t)tc_ticks[M.tc], (uint8_t)note, (uint8_t)(M.full_level ? 127 : vel), 0, M.cur_track};
    seq_add_event(cs(), &e);
    step_go(1);
    return 1;
}
static void step_q(int k, int v) { event_t *e = step_event(); if (!e) return; char b[8]; if (k == 0) { e->vel = (uint8_t)CLAMP(v * 127 / 1023, 1, 127); mos_fmt(b, 8, "%d", e->vel); qshow("VELO", b); } if (k == 1) { e->dur = (uint16_t)CLAMP(v * 384 / 1023, 1, 384); mos_fmt(b, 8, "%d", e->dur); qshow("DUR", b); } if (k == 2) { e->tune = (int8_t)(v * 48 / 1023 - 24); mos_fmt(b, 8, "%+d", e->tune); qshow("TUNE", b); } }
static int step_qget(int k) { event_t *e = step_event(); if (!e) return 0; return k == 0 ? e->vel * 1023 / 127 : k == 1 ? e->dur * 1023 / 384 : (e->tune + 24) * 1023 / 48; }
static void step_tap(void) { go(W_TC); }
static const screen_t scr_step = {"STEP", NULL, 0, draw_step, step_pad, step_soft, step_lab, step_q, step_qget, {"VELO", "DUR", "TUNE"}, step_enc, step_move, step_tap, 0};

/* ---- GRID (drum grid) ------------------------------------------------- */
static const uint8_t grid_res_tc[4] = {7, 1, 3, 5};    /* 1/4 1/8 1/16 1/32 */
static const char *const grid_res_name[4] = {"1/4", "1/8", "1/16", "1/32"};
static uint32_t grid_tick(int step) { return grid_bar * (uint32_t)16 * tc_ticks[grid_res_tc[grid_res]] + (uint32_t)step * tc_ticks[grid_res_tc[grid_res]]; }
static int grid_find(int row, int step)
{
    seq_t *s = cs(); uint32_t t0 = grid_tick(step), g = tc_ticks[grid_res_tc[grid_res]];
    int note = M.bank * MOS_PADS + row;
    for (int i = 0; i < s->nev; i++) {
        event_t *e = &s->ev[i];
        if (e->note == note && e->track == M.cur_track && e->tick >= t0 && e->tick < t0 + g - g / 4) return i;
    }
    return -1;
}
static void grid_toggle(int row, int step)
{
    if (grid_tick(step) >= seq_len_ticks(cs())) { say("PAST SEQ END"); return; }
    seq_snapshot();
    int i = grid_find(row, step);
    if (i >= 0) { seq_del_event(cs(), i); return; }
    event_t e = {seq_quantize(grid_tick(step)) , (uint16_t)tc_ticks[grid_res_tc[grid_res]], (uint8_t)(M.bank * MOS_PADS + row), grid_vel, 0, M.cur_track};
    if (M.swing == 50 || grid_res != 2) e.tick = grid_tick(step);
    seq_add_event(cs(), &e);
    if (!M.playing) seq_trigger(e.note, e.vel, 0, e.dur);
}
static uint8_t grid_top;
static void draw_grid(void)
{
    char b[24];
    int gi = grid_find(grid_row, grid_step);
    mos_fmt(b, 24, "TR%d BAR%d %s V%d", M.cur_track + 1, grid_bar + 1, grid_res_name[grid_res],
            gi >= 0 ? cs()->ev[gi].vel : grid_vel);
    header("GRID", b);
    if (grid_row < grid_top) grid_top = grid_row;
    if (grid_row >= grid_top + 7) grid_top = (uint8_t)(grid_row - 6);
    uint32_t len = seq_len_ticks(cs()), g = tc_ticks[grid_res_tc[grid_res]];
    for (int r = 0; r < 7; r++) {
        int row = grid_top + r, y = 10 + r * 6;
        char n[6]; note_name(n, M.bank * MOS_PADS + row);
        g_text(1, y, n, row == grid_row ? T_INV : 0);
        for (int st = 0; st < 16; st++) {
            int x = 14 + st * 7;
            uint32_t t = grid_tick(st);
            if (t >= len) { if (r == 0) g_dots(x, x + 5, 9, 2); continue; }
            int i = grid_find(row, st);
            if (i >= 0) {
                int v = cs()->ev[i].vel;
                if (v > 96) g_rfill(x, y, 6, 5, C_ON);
                else { g_rframe(x, y, 6, 5, C_ON); g_dither(x + 1, y + 1, 4, 3, v > 48 ? 3 : 2); }
            } else g_px(x + 2, y + 2, C_ON), (st % 4 == 0) ? g_px(x + 3, y + 2, C_ON) : (void)0;
            if (row == grid_row && st == grid_step && blink) g_frame(x - 1, y - 1, 8, 7, C_XOR);
        }
    }
    if (M.playing) {
        uint32_t p = M.pos; int st = (int)((p - grid_bar * 16u * g) / g);
        if (p >= grid_bar * 16u * g && st < 16) g_fill(14 + st * 7, 8, 6, 1, C_ON);
    }
}
static void grid_enc(int d)
{
    int i = grid_find(grid_row, grid_step);
    if (shift_held && i >= 0) { cs()->ev[i].vel = (uint8_t)CLAMP(cs()->ev[i].vel + d, 1, 127); return; }
    int st = grid_step + d;
    while (st < 0) { st += 16; if (grid_bar) grid_bar--; else st = 0; }
    while (st >= 16) { st -= 16; if (grid_tick(16) < seq_len_ticks(cs())) grid_bar++; else st = 15; }
    grid_step = (uint8_t)st;
}
static void grid_move(int d) { grid_row = (uint8_t)CLAMP(grid_row + d, 0, MOS_PADS - 1); }
static void grid_tap(void) { grid_toggle(grid_row, grid_step); }
static int grid_pad(int pad, int vel)
{
    if (!vel) return 1;
    if (del_held) { grid_row = (uint8_t)pad; return 1; }
    grid_step = (uint8_t)pad; grid_toggle(grid_row, pad);   /* pads are the 16 steps */
    return 1;
}
static void grid_soft(int i)
{
    if (i == 0) grid_move(-1);
    else if (i == 1) grid_move(1);
    else if (i == 2) { if (grid_bar) grid_bar--; }
    else if (i == 3) { if (grid_tick(16) < seq_len_ticks(cs())) grid_bar++; }
    else if (i == 4) { grid_res = (uint8_t)((grid_res + 1) % 4); grid_bar = 0; }
    else if (i == 5) {                                   /* clear row on page */
        seq_snapshot();
        for (int st = 0; st < 16; st++) { int k = grid_find(grid_row, st); if (k >= 0) seq_del_event(cs(), k); }
        say("ROW CLEARED");
    }
}
static const char *grid_lab(int i) { static const char *const l[6] = {"ROW\x04", "ROW\x05", "<BAR", "BAR>", "RES", "CLR"}; return l[i]; }
static void grid_q(int k, int v)
{
    char b[8];
    if (k == 0) { grid_vel = (uint8_t)CLAMP(v * 127 / 1023, 1, 127); int i = grid_find(grid_row, grid_step); if (i >= 0) cs()->ev[i].vel = grid_vel; mos_fmt(b, 8, "%d", grid_vel); qshow("VELO", b); }
    if (k == 1) { grid_row = (uint8_t)(v * 15 / 1023); note_name(b, M.bank * MOS_PADS + grid_row); qshow("ROW", b); }
    if (k == 2) { M.swing = (uint8_t)(50 + v * 25 / 1023); f_sw(b); qshow("SWING", b); }
}
static int grid_qget(int k) { return k == 0 ? grid_vel * 1023 / 127 : k == 1 ? grid_row * 1023 / 15 : (M.swing - 50) * 1023 / 25; }
static const screen_t scr_grid = {"GRID", NULL, 0, draw_grid, grid_pad, grid_soft, grid_lab, grid_q, grid_qget, {"VELO", "ROW", "SWING"}, grid_enc, grid_move, grid_tap, 0};

/* ---- SONG ------------------------------------------------------------- */
static void draw_song(void)
{
    header("SONG", M.song_mode ? "SONG MODE ON" : "SONG MODE OFF");
    int sel = cur[S_SONG] / 2, col = cur[S_SONG] % 2;
    int top = CLAMP(sel - 2, 0, M.song_len > 6 ? M.song_len - 6 : 0);
    for (int r = 0; r < 6 && top + r < M.song_len; r++) {
        int i = top + r, y = 10 + r * 7;
        song_step_t *st = &M.song[i];
        int playing = M.song_mode && M.playing && i == M.song_pos;
        g_textf(1, y, playing ? T_INV : 0, "%02d", i + 1);
        char b[16]; mos_fmt(b, 16, "SQ%02d", st->seq + 1);
        g_text(14, y, b, i == sel && col == 0 ? T_INV : 0);
        g_text(34, y, M.seq[st->seq].used ? M.seq[st->seq].name : "(empty)", 0);
        mos_fmt(b, 16, "x%02d", st->reps);
        g_text(110, y, b, i == sel && col == 1 ? T_INV : 0);
        if (playing) g_textf(102, y, 0, "%d", M.song_rep + 1);
    }
    if (M.song_len < 6) g_text(64, 46, "INS ADDS A STEP  ON/OF: SONG MODE", T_CENTER);
}
static void song_enc(int d)
{
    if (!M.song_len) return;
    song_step_t *st = &M.song[cur[S_SONG] / 2];
    if (cur[S_SONG] % 2 == 0) { st->seq = (uint8_t)CLAMP(st->seq + d, 0, SEQS - 1); seq_ensure(st->seq); }
    else st->reps = (uint8_t)CLAMP(st->reps + d, 1, 99);
}
static void song_move(int d) { int n = M.song_len * 2; if (!n) return; cur[S_SONG] = (uint8_t)CLAMP(cur[S_SONG] + d, 0, n - 1); }
static void song_soft(int i)
{
    int sel = cur[S_SONG] / 2;
    if (i == 0 && M.song_len < SONG_STEPS) {
        int at = M.song_len ? sel + 1 : 0;
        for (int k = M.song_len; k > at; k--) M.song[k] = M.song[k - 1];
        M.song[at].seq = M.song_len ? M.song[sel].seq : 0; M.song[at].reps = 1; M.song_len++;
        cur[S_SONG] = (uint8_t)(at * 2);
    } else if (i == 1 && M.song_len > 1) {
        for (int k = sel; k < M.song_len - 1; k++) M.song[k] = M.song[k + 1];
        M.song_len--; if (sel >= M.song_len) cur[S_SONG] = (uint8_t)((M.song_len - 1) * 2);
    } else if (i == 2) { M.song_mode = !M.song_mode; seq_stop(); }
    else if (i == 5) { M.song_mode = 1; seq_stop(); seq_play(1); }
}
static const char *song_lab(int i) { static const char *const l[6] = {"INS", "DEL", "ON/OF", "", "", "PLAY"}; return l[i]; }
static const screen_t scr_song = {"SONG", NULL, 0, draw_song, NULL, song_soft, song_lab, NULL, NULL, {0}, song_enc, song_move, NULL, 0};

/* ---- PROGRAM (pad parameters) ------------------------------------------ */
static pad_t *lp(void) { return &M.pad[M.last_note]; }
static void param(int p, int v) { mos_host_pad_param(M.last_note, p, v); }
static void f_pad(char *b) { note_name(b, M.last_note); }
static void a_pad(int d) { M.last_note = (uint8_t)CLAMP(M.last_note + d, 0, MOS_NOTES - 1); }
static void f_lvl(char *b) { mos_fmt(b, 8, "%d", lp()->level); }
static void a_lvl(int d) { lp()->level = (uint8_t)CLAMP(lp()->level + d, 0, 127); param(MOS_P_LEVEL, lp()->level); }
static void f_pan(char *b) { int p = lp()->pan - 50; if (!p) mos_strcpy(b, "C", 4); else mos_fmt(b, 8, "%c%d", p < 0 ? 'L' : 'R', ABS(p)); }
static void a_pan(int d) { lp()->pan = (uint8_t)CLAMP(lp()->pan + d, 0, 100); param(MOS_P_PAN, lp()->pan); }
static void f_tune(char *b) { mos_fmt(b, 8, "%+d", lp()->tune); }
static void a_tune(int d) { lp()->tune = (int8_t)CLAMP(lp()->tune + d, -24, 24); param(MOS_P_TUNE, lp()->tune); }
static void f_dec(char *b) { mos_fmt(b, 8, "%d", lp()->decay); }
static void a_dec(int d) { lp()->decay = (uint8_t)CLAMP(lp()->decay + d, 1, 100); param(MOS_P_DECAY, lp()->decay); }
static void f_grp(char *b) { if (lp()->mute_grp) mos_fmt(b, 8, "%d", lp()->mute_grp); else mos_strcpy(b, "OFF", 4); }
static void a_grp(int d) { lp()->mute_grp = (uint8_t)CLAMP(lp()->mute_grp + d, 0, 8); }
#define FLAG_FIELD(nm, bit) \
    static void f_##nm(char *b) { mos_strcpy(b, (lp()->flags & bit) ? "ON" : "OFF", 4); } \
    static void a_##nm(int d) { (void)d; lp()->flags ^= bit; param(MOS_P_FLAGS, lp()->flags); }
FLAG_FIELD(gate, PF_GATE)
FLAG_FIELD(lp, PF_LOOP)
FLAG_FIELD(rev, PF_REVERSE)
FLAG_FIELD(sync, PF_BPMSYNC)
static const field_t prog_f[] = {
    {1, 10, "PAD", f_pad, a_pad, 0},
    {1, 19, "LEVEL", f_lvl, a_lvl, 0}, {1, 26, "PAN  ", f_pan, a_pan, 0},
    {1, 33, "TUNE ", f_tune, a_tune, 0}, {1, 40, "DECAY", f_dec, a_dec, 0},
    {1, 47, "GROUP", f_grp, a_grp, 0},
    {48, 19, "GATE", f_gate, a_gate, 0}, {48, 26, "LOOP", f_lp, a_lp, 0},
    {48, 33, "REV ", f_rev, a_rev, 0}, {48, 40, "SYNC", f_sync, a_sync, 0},
};
static void draw_wave(int note, int x, int y, int w, int h, int marks)
{
    sample_t *sm = &M.smp[note]; pad_t *p = &M.pad[note];
    int mid = y + h / 2;
    if (!sm->has) { g_dots(x, x + w - 1, mid, 2); g_text(x + w / 2, mid - 2, "NO SAMPLE", T_CENTER | T_INV); return; }
    int s = x + p->start_pm * w / 1000, e = x + p->end_pm * w / 1000;
    for (int i = 0; i < w; i++) {
        int pk = sm->peaks[i * MOS_WAVE_W / w] * (h / 2) / 255;
        if (marks && (x + i < s || x + i > e)) { if (pk) g_vdots(x + i, mid - pk, mid + pk, 2); }
        else g_vline(x + i, mid - pk, mid + pk, C_ON);
    }
    g_hline(x, x + w - 1, mid, C_ON);
    if (marks) {
        g_vline(s, y, y + h, C_ON); g_fill(s, y, 3, 2, C_ON);
        g_vline(e, y, y + h, C_ON); g_fill(e - 2, y + h - 1, 3, 2, C_ON);
    }
}
static void draw_prog(void)
{
    char b[24]; note_name(b, M.last_note);
    header("PROGRAM", M.smp[M.last_note].has ? M.smp[M.last_note].name : "EMPTY PAD");
    for (int i = 0; i < 10; i++) draw_field(&prog_f[i], cur[S_PROG] == i);
    g_rframe(78, 10, 50, 44, C_ON);
    draw_wave(M.last_note, 80, 13, 46, 24, 1);
    g_textf(103, 41, T_CENTER, "%s %s", b, (lp()->flags & PF_MUTE) ? "MUTE" : "");
    g_text(103, 47, "PADS SELECT", T_CENTER);
}
static int prog_pad(int pad, int vel) { if (vel) M.last_note = (uint8_t)(M.bank * MOS_PADS + pad); return 0; }
static void prog_soft(int i)
{
    if (i == 0) a_pad(-1); else if (i == 1) a_pad(1);
    else if (i == 2) seq_trigger(M.last_note, 110, 0, 96);
    else if (i == 3) { pad_t *p = lp(); mos_memset(p, 0, sizeof *p); p->level = 100; p->pan = 50; p->decay = 100; p->end_pm = 1000; for (int k = 0; k < 5; k++) mos_host_pad_param(M.last_note, k, k == 0 ? 100 : k == 1 ? 50 : k == 3 ? 100 : 0); say("PAD INIT"); }
    else if (i == 4) go(S_TRIM); else go(S_MIXER);
}
static const char *prog_lab(int i) { static const char *const l[6] = {"<PAD", "PAD>", "PLAY", "INIT", "TRIM", "MIXER"}; return l[i]; }
static void prog_q(int k, int v) { char b[8]; if (k == 0) { lp()->level = (uint8_t)(v * 127 / 1023); param(MOS_P_LEVEL, lp()->level); f_lvl(b); qshow("LEVEL", b); } if (k == 1) { lp()->pan = (uint8_t)(v * 100 / 1023); param(MOS_P_PAN, lp()->pan); f_pan(b); qshow("PAN", b); } if (k == 2) { lp()->tune = (int8_t)(v * 48 / 1023 - 24); param(MOS_P_TUNE, lp()->tune); f_tune(b); qshow("TUNE", b); } }
static int prog_qget(int k) { return k == 0 ? lp()->level * 1023 / 127 : k == 1 ? lp()->pan * 1023 / 100 : (lp()->tune + 24) * 1023 / 48; }
static const screen_t scr_prog = {"PROGRAM", prog_f, 10, draw_prog, prog_pad, prog_soft, prog_lab, prog_q, prog_qget, {"LEVEL", "PAN", "TUNE"}, NULL, NULL, NULL, 0};

/* ---- MIXER (16 strips of the current bank) ----------------------------- */
static void draw_mixer(void)
{
    static const char *const views[3] = {"LEVEL", "PAN", "TUNE"};
    char b[24]; mos_fmt(b, 24, "BANK %c %s", bank_letters[M.bank], views[mix_view]);
    header("MIXER", b);
    int sel = M.last_note / MOS_PADS == M.bank ? M.last_note % MOS_PADS : -1;
    for (int i = 0; i < 16; i++) {
        pad_t *p = &M.pad[M.bank * MOS_PADS + i];
        int x = 1 + i * 8, top = 11, h = 31;
        if (mix_view == 0) {
            int f = p->level * (h - 2) / 127;
            g_rframe(x, top, 6, h, C_ON);
            if (p->flags & PF_MUTE) g_dither(x + 1, top + h - 1 - f, 4, f, 1);
            else g_fill(x + 1, top + h - 1 - f, 4, f, C_ON);
        } else {
            int v = mix_view == 1 ? p->pan : (p->tune + 24) * 100 / 48;
            int yy = top + h - 2 - v * (h - 4) / 100;
            g_vdots(x + 2, top, top + h - 1, 2);
            g_rfill(x, yy, 6, 3, C_ON);
        }
        if (pad_glow[i]) g_fill(x, top - 2, 6, 1, C_ON);
        g_textf(x + 3, 44, T_CENTER | (i == sel ? T_INV : 0), "%x", i + 1 > 15 ? 0 : i + 1);
    }
    if (sel >= 0) {
        char n[6], v1[8], v2[8], v3[8]; note_name(n, M.last_note); f_lvl(v1); f_pan(v2); f_tune(v3);
        g_textf(1, 50, 0, "%s  LVL %s  PAN %s  TUNE %s", n, v1, v2, v3);
    } else g_text(1, 50, "HIT A PAD TO SELECT", 0);
}
static void mix_enc(int d) { if (mix_view == 0) a_lvl(d); else if (mix_view == 1) a_pan(d); else a_tune(d); }
static void mix_move(int d) { M.last_note = (uint8_t)(M.bank * MOS_PADS + CLAMP(M.last_note % MOS_PADS + d, 0, 15)); }
static int mix_pad(int pad, int vel) { if (vel) M.last_note = (uint8_t)(M.bank * MOS_PADS + pad); return 0; }
static void mix_soft(int i)
{
    if (i < 3) mix_view = (uint8_t)i;
    else if (i == 3) { lp()->flags ^= PF_MUTE; param(MOS_P_FLAGS, lp()->flags); }
    else if (i == 4) { for (int k = 0; k < 16; k++) M.pad[M.bank * MOS_PADS + k].flags &= (uint8_t)~PF_MUTE; say("UNMUTED ALL"); }
    else { pad_t *p = lp(); p->level = 100; p->pan = 50; p->tune = 0; param(MOS_P_LEVEL, 100); param(MOS_P_PAN, 50); param(MOS_P_TUNE, 0); say("STRIP RESET"); }
}
static const char *mix_lab(int i) { static const char *const l[6] = {"LEVEL", "PAN", "TUNE", "MUTE", "UNMUT", "RESET"}; return l[i]; }
static const screen_t scr_mixer = {"MIXER", NULL, 0, draw_mixer, mix_pad, mix_soft, mix_lab, prog_q, prog_qget, {"LEVEL", "PAN", "TUNE"}, mix_enc, mix_move, NULL, 0};

/* ---- TRIM / CHOP ------------------------------------------------------- */
static void f_st(char *b) { mos_fmt(b, 10, "%d.%d%%", lp()->start_pm / 10, lp()->start_pm % 10); }
static void a_st(int d) { lp()->start_pm = (int16_t)CLAMP(lp()->start_pm + d * (shift_held ? 1 : 5), 0, lp()->end_pm - 5); }
static void f_en(char *b) { mos_fmt(b, 10, "%d.%d%%", lp()->end_pm / 10, lp()->end_pm % 10); }
static void a_en(int d) { lp()->end_pm = (int16_t)CLAMP(lp()->end_pm + d * (shift_held ? 1 : 5), lp()->start_pm + 5, 1000); }
static void f_sl(char *b) { mos_fmt(b, 8, "%d", slices); }
static void a_sl(int d) { slices = (uint8_t)CLAMP(slices + d, 2, 16); }
static const field_t trim_f[] = {
    {1, 45, "ST", f_st, a_st, 0}, {44, 45, "END", f_en, a_en, 0}, {92, 45, "SLC", f_sl, a_sl, W_CHOP},
};
static void draw_trim(void)
{
    char b[24]; note_name(b, M.last_note);
    char r[24]; mos_fmt(r, 24, "%s %s", b, M.smp[M.last_note].has ? M.smp[M.last_note].name : "");
    header("TRIM", r);
    g_rframe(0, 9, 128, 34, C_ON);
    draw_wave(M.last_note, 2, 11, 124, 30, 1);
    /* slice preview ticks */
    if (cur[S_TRIM] == 2) {
        int s = 2 + lp()->start_pm * 124 / 1000, e = 2 + lp()->end_pm * 124 / 1000;
        for (int i = 1; i < slices; i++) { int x = s + (e - s) * i / slices; g_vdots(x, 11, 40, 3); }
    }
    for (int i = 0; i < 3; i++) draw_field(&trim_f[i], cur[S_TRIM] == i);
}
static void do_chop(void)
{
    int src = M.last_note, dbank = (M.bank + 1) % MOS_BANKS;
    pad_t *p = &M.pad[src]; sample_t *sm = &M.smp[src];
    if (!sm->has) { say("NO SAMPLE"); return; }
    for (int i = 0; i < slices; i++) {
        int dst = dbank * MOS_PADS + i;
        int a = p->start_pm + (p->end_pm - p->start_pm) * i / slices;
        int z = p->start_pm + (p->end_pm - p->start_pm) * (i + 1) / slices;
        pad_t *d = &M.pad[dst]; *d = *p; d->start_pm = (int16_t)a; d->end_pm = (int16_t)z;
        sample_t *ds = &M.smp[dst]; *ds = *sm;
        d->start_pm = 0; d->end_pm = 1000;                  /* the new pad is the slice */
        for (int k = 0; k < MOS_WAVE_W; k++) ds->peaks[k] = sm->peaks[(a + (z - a) * k / MOS_WAVE_W) * (MOS_WAVE_W - 1) / 1000];
        mos_fmt(ds->name, sizeof ds->name, "SLICE%02d", i + 1);
        ds->frames = (uint32_t)((uint64_t)sm->frames * (uint32_t)(z - a) / 1000);
        mos_host_chop(dst, src, a, z);
    }
    char b[24]; mos_fmt(b, 24, "%d SLICES > BANK %c", slices, bank_letters[dbank]);
    say(b);
    M.bank = (uint8_t)dbank;
}
static void trim_soft(int i)
{
    if (i == 0) seq_trigger(M.last_note, 110, 0, 192);
    else if (i == 1) a_pad(-1);
    else if (i == 2) a_pad(1);
    else if (i == 3) { lp()->start_pm = 0; lp()->end_pm = 1000; }
    else if (i == 4) go(W_CHOP);
    else go(S_PROG);
}
static const char *trim_lab(int i) { static const char *const l[6] = {"PLAY", "<PAD", "PAD>", "FULL", "CHOP", "PROG"}; return l[i]; }
static void trim_q(int k, int v)
{
    char b[10];
    if (k == 0) { lp()->start_pm = (int16_t)CLAMP(v * 1000 / 1023, 0, lp()->end_pm - 5); f_st(b); qshow("START", b); }
    if (k == 1) { lp()->end_pm = (int16_t)CLAMP(v * 1000 / 1023, lp()->start_pm + 5, 1000); f_en(b); qshow("END", b); }
    if (k == 2) { slices = (uint8_t)(2 + v * 14 / 1023); f_sl(b); qshow("SLICES", b); cur[S_TRIM] = 2; }
}
static int trim_qget(int k) { return k == 0 ? lp()->start_pm * 1023 / 1000 : k == 1 ? lp()->end_pm * 1023 / 1000 : (slices - 2) * 1023 / 14; }
static int trim_pad(int pad, int vel) { if (vel) M.last_note = (uint8_t)(M.bank * MOS_PADS + pad); return 0; }
static const screen_t scr_trim = {"TRIM", trim_f, 3, draw_trim, trim_pad, trim_soft, trim_lab, trim_q, trim_qget, {"START", "END", "SLICES"}, NULL, NULL, NULL, 0};

/* ---- performance pad modes --------------------------------------------- */
static void tile(int i, int w, int h, int y0, int on, int dim, const char *a, const char *b)
{
    int cols = 128 / w, x = (i % cols) * w, y = y0 + (i / cols) * h;
    if (on) g_rfill(x + 1, y, w - 2, h - 1, C_ON);
    else if (dim) { g_dots(x + 2, x + w - 3, y, 2); g_dots(x + 2, x + w - 3, y + h - 2, 2); g_vdots(x + 1, y + 1, y + h - 3, 2); g_vdots(x + w - 2, y + 1, y + h - 3, 2); }
    else g_rframe(x + 1, y, w - 2, h - 1, C_ON);
    g_text(x + 3, y + 2, a, on ? T_INV : 0);
    if (b) g_text(x + 3, y + h - 7, b, on ? T_INV : 0);
}

static void draw_nextseq(void)
{
    char b[24]; mos_fmt(b, 24, "NOW %02d", M.cur_seq + 1); header("NEXT SEQ", b);
    for (int i = 0; i < 16; i++) {
        char a[6], n[8]; mos_fmt(a, 6, "%02d", i + 1);
        mos_strcpy(n, M.seq[i].used ? M.seq[i].name : "", 7);
        if (M.seq[i].used && n[0] == 'S' && n[1] == 'e') mos_fmt(n, 8, "%dBAR", M.seq[i].bars);
        tile(i, 32, 11, 9, i == M.cur_seq, !M.seq[i].used, a, NULL);
        g_text((i % 4) * 32 + 29, 9 + (i / 4) * 11 + 2, n, T_RIGHT | (i == M.cur_seq ? T_INV : 0));
        if (i == M.next_seq && blink) g_frame((i % 4) * 32, 8 + (i / 4) * 11, 32, 12, C_ON);
    }
}
static int nextseq_pad(int pad, int vel)
{
    if (!vel) return 1;
    seq_ensure(pad);
    if (M.playing) M.next_seq = (uint8_t)pad; else { M.cur_seq = (uint8_t)pad; M.pos = 0; }
    return 1;
}
static const screen_t scr_nextseq = {"NEXT SEQ", NULL, 0, draw_nextseq, nextseq_pad, NULL, NULL, main_q, main_qget, {"SWING", "TC", "TEMPO"}, NULL, NULL, NULL, 0};

static uint8_t track_glow[TRACKS];
static void draw_mute(void)
{
    header("TRACK MUTE", "PADS 9-16 SOLO");
    for (int t = 0; t < TRACKS; t++) {
        track_t *tr = &cs()->tr[t]; char a[8];
        mos_fmt(a, 8, "%d%s", t + 1, tr->solo ? " SOLO" : "");
        tile(t, 32, 22, 9, tr->solo, tr->mute, a, tr->mute ? "MUTED" : tr->name);
        if (track_glow[t]) { int x = (t % 4) * 32, y = 9 + (t / 4) * 22; g_fill(x + 25, y + 2, 3, 3, C_XOR); }
    }
}
static int mute_pad(int pad, int vel)
{
    if (!vel) return 1;
    track_t *tr = &cs()->tr[pad % 8];
    if (pad < 8) tr->mute = !tr->mute; else tr->solo = !tr->solo;
    return 1;
}
static const screen_t scr_mute = {"TRACK MUTE", NULL, 0, draw_mute, mute_pad, NULL, NULL, main_q, main_qget, {"SWING", "TC", "TEMPO"}, NULL, NULL, NULL, 0};

static void f_lt(char *b) { mos_strcpy(b, lev_type ? "TUNE" : "VELO", 6); }
static void a_lt(int d) { (void)d; lev_type = !lev_type; }
static void f_ln(char *b) { note_name(b, M.last_note); }
static void a_ln(int d) { a_pad(d); }
static const field_t lev_f[] = {{1, 10, "SRC", f_ln, a_ln, 0}, {44, 10, "TYPE", f_lt, a_lt, 0}};
static int lev_value(int i) { return lev_type ? i - 8 : (i + 1) * 8 - 1; }
static void draw_16lev(void)
{
    header("16 LEVELS", M.smp[M.last_note].has ? M.smp[M.last_note].name : "");
    for (int i = 0; i < 2; i++) draw_field(&lev_f[i], cur[S_16LEV] == i);
    for (int i = 0; i < 16; i++) {
        char a[8]; mos_fmt(a, 8, lev_type ? "%+d" : "%d", lev_value(i));
        int x = (i % 4) * 32, y = 17 + (i / 4) * 10;
        int on = pad_glow[i] > 0;
        if (on) g_rfill(x + 1, y, 30, 9, C_ON); else g_rframe(x + 1, y, 30, 9, C_ON);
        if (!lev_type) g_fill(x + 3, y + 7, (i + 1) * 26 / 16, 1, on ? C_OFF : C_ON);
        g_text(x + 16, y + 1, a, T_CENTER | (on ? T_INV : 0));
    }
}
static int lev_pad(int pad, int vel)
{
    if (!vel) return 1;
    int v = lev_type ? (M.full_level ? 127 : vel) : lev_value(pad), t = lev_type ? lev_value(pad) : 0;
    seq_trigger(M.last_note, v, t, 96);
    pad_glow[pad] = 255;
    if (M.recording && !M.counting) {
        event_t e = {seq_quantize(M.pos) % seq_len_ticks(cs()), 24, M.last_note, (uint8_t)v, (int8_t)t, M.cur_track};
        seq_add_event(cs(), &e);
    }
    return 1;
}
static const screen_t scr_16lev = {"16 LEVELS", lev_f, 2, draw_16lev, lev_pad, NULL, NULL, main_q, main_qget, {"SWING", "TC", "TEMPO"}, NULL, NULL, NULL, 0};

static void f_rr(char *b) { mos_strcpy(b, tc_names[M.rpt_tc], 8); }
static void a_rr(int d) { M.rpt_tc = (uint8_t)CLAMP(M.rpt_tc + d, 1, TC_COUNT - 1); }
static const field_t rpt_f[] = {{1, 48, "RATE", f_rr, a_rr, 0}, {50, 48, "SWING", f_sw, a_sw, 0}};
static void draw_repeat(void)
{
    header("NOTE REPEAT", "HOLD PADS");
    g_rframe(0, 9, 128, 36, C_ON);
    g_text(64, 14, tc_names[M.rpt_tc], T_CENTER | T_X2);
    for (int i = 0; i < 16; i++) {
        int x = 4 + i * 7 + (i / 4) * 2, on = (M.rpt_mask >> i) & 1;
        if (on || pad_glow[i] > 128) g_fill(x, 32, 5, 9, C_ON); else g_frame(x, 32, 5, 9, C_ON);
    }
    for (int i = 0; i < 2; i++) draw_field(&rpt_f[i], cur[S_REPEAT] == i);
}
static int repeat_pad(int pad, int vel)
{
    if (vel) { M.rpt_mask |= (uint16_t)(1u << pad); M.rpt_vel[pad] = (uint8_t)vel; M.free_pos = 0; M.free_acc = 0; if (!M.playing) seq_repeat_fire(), M.free_pos = 1; }
    else M.rpt_mask &= (uint16_t)~(1u << pad);
    return 1;
}
static void repeat_q(int k, int v) { char b[8]; if (k == 0) { M.rpt_tc = (uint8_t)(1 + v * (TC_COUNT - 2) / 1023); f_rr(b); qshow("RATE", b); } else main_q(k, v); }
static int repeat_qget(int k) { return k == 0 ? (M.rpt_tc - 1) * 1023 / (TC_COUNT - 2) : main_qget(k); }
static const screen_t scr_repeat = {"NOTE REPEAT", rpt_f, 2, draw_repeat, repeat_pad, NULL, NULL, repeat_q, repeat_qget, {"RATE", "TC", "TEMPO"}, NULL, NULL, NULL, 0};

/* ---- SETUP / HELP ------------------------------------------------------ */
static void f_click(char *b) { static const char *const c[3] = {"OFF", "REC", "ALWAYS"}; mos_strcpy(b, c[M.click_on], 8); }
static void a_click(int d) { M.click_on = (uint8_t)CLAMP(M.click_on + d, 0, 2); }
static void f_cnt(char *b) { mos_strcpy(b, M.count_in ? "ON" : "OFF", 4); }
static void a_cnt(int d) { (void)d; M.count_in = !M.count_in; }
static void f_tsrc(char *b) { mos_strcpy(b, M.tempo_src_seq ? "SEQUENCE" : "MASTER", 10); }
static void a_tsrc(int d) { (void)d; if (!M.tempo_src_seq) cs()->tempo10 = M.tempo10; M.tempo_src_seq = !M.tempo_src_seq; }
static const field_t setup_f[] = {
    {1, 11, "CLICK      ", f_click, a_click, 0}, {1, 18, "COUNT IN   ", f_cnt, a_cnt, 0},
    {1, 25, "TEMPO FROM ", f_tsrc, a_tsrc, 0}, {1, 32, "TIME SIG   ", f_beats, a_beats, 0},
};
static void draw_setup(void)
{
    header("SETUP", "OTHER");
    for (int i = 0; i < 4; i++) draw_field(&setup_f[i], cur[S_SETUP] == i);
    g_hline(0, 127, 40, C_ON);
    g_text(1, 43, "MOSURA OS 0.1  CORE BUILD", 0);
    g_text(1, 49, "MPC WORKFLOW FOR SP-404MKII", 0);
}
static const char *setup_lab(int i) { (void)i; return ""; }
static void setup_soft(int i) { (void)i; }
static const screen_t scr_setup = {"SETUP", setup_f, 4, draw_setup, NULL, setup_soft, setup_lab, NULL, NULL, {0}, NULL, NULL, NULL, 0};

static const char *const help_lines[] = {
    "VALUE TURN      CHANGE FIELD",
    "VALUE PUSH+TURN MOVE CURSOR",
    "VALUE TAP       OPEN WINDOW",
    "EXIT            BACK / CLOSE",
    "SHIFT TAP       MODE GRID",
    "SHIFT+PAD       GO TO MODE",
    "PTN SELECT      PLAY/STOP",
    "SHIFT+PTN SEL   PLAY FROM HERE",
    "PTN EDIT        REC / OVERDUB",
    "REMAIN          UNDO SEQ",
    "DEL+PAD         ERASE (PLAYING)",
    "ROLL+PAD        NOTE REPEAT",
    "COPY (MAIN)     COPY SEQUENCE",
    "START/END       TRIM",
    "PITCH/SPEED     PROGRAM",
    "FX BUTTONS      F1-F6 SOFT KEYS",
    "  (ON PERFORM SCREENS: FX)",
    "CTRL 1-3        Q-LINKS",
    "SHIFT+TURN      COARSE / FINE",
    "SHIFT x2 (STOCK) BACK TO MOSURA",
};
#define HELP_N (int)(sizeof help_lines / sizeof help_lines[0])
static void draw_help(void)
{
    header("HELP", "TURN TO SCROLL");
    for (int i = 0; i < 6 && help_top + i < HELP_N; i++) g_text(1, 10 + i * 7, help_lines[help_top + i], 0);
    int h = 42 * 6 / HELP_N, y = 9 + (42 - h) * help_top / (HELP_N - 6);
    g_vline(127, y, y + h, C_ON);
}
static void help_enc(int d) { help_top = (uint8_t)CLAMP(help_top + d, 0, HELP_N - 6); }
static const screen_t scr_help = {"HELP", NULL, 0, draw_help, NULL, NULL, NULL, NULL, NULL, {0}, help_enc, help_enc, NULL, 0};

static void draw_stock(void) { g_text(64, 26, "STOCK OS", T_CENTER | T_X2); g_text(64, 42, "DOUBLE-TAP SHIFT TO RETURN", T_CENTER); }
static const screen_t scr_stock = {"STOCK", NULL, 0, draw_stock, NULL, NULL, NULL, NULL, NULL, {0}, NULL, NULL, NULL, 0};

/* ---- windows ------------------------------------------------------------ */
static void window_frame(const char *title, int y, int h)
{
    g_fill(6, y - 1, 116, h + 2, C_OFF);
    g_rframe(6, y - 1, 116, h + 2, C_ON);
    g_rframe(8, y + 1, 112, h - 2, C_ON);
    int w = g_textw(title, 0);
    g_fill(64 - w / 2 - 3, y - 2, w + 6, 7, C_OFF);
    g_text(64, y - 1, title, T_CENTER);
}
static const field_t tc_f[] = {
    {14, 20, "NOTE VALUE  ", f_tc, a_tc, 0}, {14, 28, "SWING       ", f_sw, a_sw, 0},
    {14, 36, "REPEAT RATE ", f_rr, a_rr, 0},
};
static void draw_wtc(void) { window_frame("TIMING CORRECT", 14, 32); for (int i = 0; i < 3; i++) draw_field(&tc_f[i], cur[W_TC] == i); }
static const char *wlab(int i) { return i == 5 ? "CLOSE" : ""; }
static void wsoft(int i) { if (i == 5) back(); }
static const screen_t scr_wtc = {"TC", tc_f, 3, draw_wtc, NULL, wsoft, wlab, NULL, NULL, {0}, NULL, NULL, NULL, 1};

static void f_tempo2(char *b) { f_tempo(b); }
static const field_t wseq_f[] = {
    {14, 19, "SEQUENCE ", f_sq, a_sq, 0}, {14, 26, "BARS     ", f_bars, a_bars, 0},
    {14, 33, "TIME SIG ", f_beats, a_beats, 0}, {14, 40, "TEMPO    ", f_tempo2, a_tempo, 0},
    {74, 26, "LOOP ", f_loop, a_loop, 0},
};
static void draw_wseq(void) { window_frame("SEQUENCE", 13, 36); for (int i = 0; i < 5; i++) draw_field(&wseq_f[i], cur[W_SEQ] == i); }
static const screen_t scr_wseq = {"SEQ", wseq_f, 5, draw_wseq, NULL, wsoft, wlab, NULL, NULL, {0}, NULL, NULL, NULL, 1};

static void f_tv(char *b) { mos_fmt(b, 8, "%d%%", cs()->tr[M.cur_track].vel_pct); }
static void a_tv(int d) { track_t *t = &cs()->tr[M.cur_track]; t->vel_pct = (uint8_t)CLAMP(t->vel_pct + d, 1, 200); }
static void f_tm(char *b) { mos_strcpy(b, cs()->tr[M.cur_track].mute ? "ON" : "OFF", 4); }
static void a_tm(int d) { (void)d; cs()->tr[M.cur_track].mute ^= 1; }
static void f_ts(char *b) { mos_strcpy(b, cs()->tr[M.cur_track].solo ? "ON" : "OFF", 4); }
static void a_ts(int d) { (void)d; cs()->tr[M.cur_track].solo ^= 1; }
static const field_t wtr_f[] = {
    {14, 19, "TRACK    ", f_tr, a_tr, 0}, {14, 26, "VELOCITY ", f_tv, a_tv, 0},
    {14, 33, "MUTE     ", f_tm, a_tm, 0}, {14, 40, "SOLO     ", f_ts, a_ts, 0},
};
static void draw_wtr(void) { window_frame("TRACK", 13, 36); for (int i = 0; i < 4; i++) draw_field(&wtr_f[i], cur[W_TRACK] == i); }
static const screen_t scr_wtr = {"TRACK", wtr_f, 4, draw_wtr, NULL, wsoft, wlab, NULL, NULL, {0}, NULL, NULL, NULL, 1};

static const field_t wchop_f[] = {{14, 24, "SLICES ", f_sl, a_sl, 0}};
static void draw_wchop(void)
{
    window_frame("CHOP", 13, 36);
    char n[6]; note_name(n, M.last_note);
    g_textf(14, 17, 0, "SPLIT %s INTO PADS", n);
    draw_field(&wchop_f[0], 1);
    g_textf(14, 31, 0, "OF BANK %c, EQUAL", bank_letters[(M.bank + 1) % MOS_BANKS]);
    g_text(14, 39, "VALUE TAP = DO IT", 0);
}
static void wchop_tap(void) { do_chop(); scr = S_TRIM; }
static const screen_t scr_wchop = {"CHOP", wchop_f, 1, draw_wchop, NULL, wsoft, wlab, NULL, NULL, {0}, NULL, NULL, wchop_tap, 1};

/* ---- frame -------------------------------------------------------------- */
static int perf(void) { return SCR[scr]->softlab == NULL && !SCR[scr]->modal; }

static void footer(void)
{
    const screen_t *s = SCR[scr];
    static const char *const fx[6] = {"FILT", "RESO", "DLY", "ISO", "DJFX", "MFX"};
    for (int i = 0; i < 6; i++) {
        const char *l = s->softlab ? s->softlab(i) : fx[i];
        int x = i * 128 / 6;
        if (!l[0]) { g_dots(x + 3, x + 18, 60, 3); continue; }
        g_box_label(x, 56, 21, l, s->softlab ? 0 : fx_on[i]);
    }
}

void ui_draw(void)
{
    g_clear();
    const screen_t *s = SCR[scr];
    if (s->modal) { SCR[under]->draw(); g_fill(0, 56, 128, 8, C_OFF); }
    s->draw();
    if (scr != S_MODES && scr != S_STOCK) footer();
    if (M.ms < qpop_until) {
        int w = g_textw(qpop, 0) + 8;
        g_fill(64 - w / 2, 22, w, 11, C_OFF); g_rframe(64 - w / 2, 22, w, 11, C_ON);
        g_text(64, 25, qpop, T_CENTER);
    } else if (M.ms < toast_until) {
        int w = g_textw(toast, 0) + 10;
        g_rfill(64 - w / 2, 23, w, 11, C_ON);
        g_text(64, 26, toast, T_CENTER | T_INV);
    }
    if (beat_flash && scr != S_STOCK) g_fill(126, 0, 2, 2, C_XOR);
}

/* ---- LEDs --------------------------------------------------------------- */
static const uint8_t bank_rgb[MOS_BANKS][3] = {
    {255, 70, 20}, {255, 170, 0}, {120, 230, 40}, {0, 200, 160}, {0, 120, 255},
    {140, 60, 255}, {255, 40, 160}, {255, 255, 255}, {255, 110, 80}, {60, 255, 255}};
static void update_leds(void)
{
    mos_memset(&leds, 0, sizeof leds);
    leds.key[K_PATTERN_SELECT] = M.playing ? (beat_flash ? 255 : 90) : 0;
    leds.key[K_PATTERN_EDIT] = M.recording ? 255 : M.overdub_armed ? (blink ? 255 : 0) : 0;
    leds.key[K_SHIFT] = scr == S_MODES || shift_held ? 255 : 0;
    leds.key[K_REMAIN] = M.undo_ok ? 40 : 0;
    leds.key[K_ROLL] = roll_held || M.rpt_mask ? 255 : 0;
    leds.key[K_DEL] = del_held ? 255 : 0;
    leds.key[K_EXIT] = scr != S_MAIN ? 60 : 0;
    leds.key[K_BANK_AF + M.bank % 5] = M.bank >= 5 ? (blink ? 255 : 120) : 255;
    pad_t *p = lp();
    leds.key[K_GATE] = p->flags & PF_GATE ? 200 : 0;
    leds.key[K_LOOP] = p->flags & PF_LOOP ? 200 : 0;
    leds.key[K_REVERSE] = p->flags & PF_REVERSE ? 200 : 0;
    leds.key[K_BPM_SYNC] = p->flags & PF_BPMSYNC ? 200 : 0;
    for (int i = 0; i < 6; i++) leds.key[K_FX1 + i] = perf() ? (fx_on[i] ? 255 : 0) : 70;
    const uint8_t *c = bank_rgb[M.bank];
    for (int i = 0; i < MOS_PADS; i++) {
        int lvl = 64;                                        /* idle: dim bank colour */
        if (scr == S_GRID) lvl = grid_find(grid_row, i) >= 0 ? 160 : (i == grid_step ? 60 : 10);
        if (scr == S_MUTE) lvl = i < 8 ? (cs()->tr[i].mute ? 8 : 140) : (cs()->tr[i - 8].solo ? 255 : 12);
        if (scr == S_NEXTSEQ) lvl = i == M.cur_seq ? 255 : i == M.next_seq ? (blink ? 255 : 30) : M.seq[i].used ? 70 : 4;
        if (scr == S_MODES) lvl = i == prev_scr ? 255 : 50;
        if (M.smp[M.bank * MOS_PADS + i].has == 0 && scr != S_MODES && scr != S_MUTE && scr != S_NEXTSEQ) lvl = lvl / 3;
        int gl = pad_glow[i];
        for (int k = 0; k < 3; k++) {
            int v = c[k] * lvl / 255 + gl * (255 - c[k] * lvl / 255) / 255;
            leds.pad[i][k] = (uint8_t)CLAMP(v, 0, 255);
        }
    }
}

/* ---- input --------------------------------------------------------------- */
void ui_pad_note(int pad, int on) { if (on) pad_glow[pad] = 255; else if (pad_glow[pad] > 120) pad_glow[pad] = 120; }
void ui_beat(int beat, int bar_start) { (void)beat; beat_flash = bar_start ? 2 : 1; }

void ui_tick_ms(void)
{
    blink = (M.ms / 250) & 1;
    if (M.ms % 4 == 0) for (int i = 0; i < MOS_PADS; i++) if (pad_glow[i]) pad_glow[i] = (uint8_t)(pad_glow[i] > 8 ? pad_glow[i] - 8 : 0);
    if (M.ms % 60 == 0) beat_flash = 0;
    if (M.ms % 40 == 0) {
        for (int t = 0; t < TRACKS; t++) if (track_glow[t]) track_glow[t]--;
    }
}

void ui_init(void)
{
    SCR[S_MAIN] = &scr_main; SCR[S_STEP] = &scr_step; SCR[S_GRID] = &scr_grid; SCR[S_SONG] = &scr_song;
    SCR[S_PROG] = &scr_prog; SCR[S_MIXER] = &scr_mixer; SCR[S_TRIM] = &scr_trim; SCR[S_SETUP] = &scr_setup;
    SCR[S_NEXTSEQ] = &scr_nextseq; SCR[S_MUTE] = &scr_mute; SCR[S_16LEV] = &scr_16lev; SCR[S_REPEAT] = &scr_repeat;
    SCR[S_HELP] = &scr_help; SCR[S_STOCK] = &scr_stock; SCR[S_MODES] = &scr_modes;
    SCR[S_FULL] = &scr_main; SCR[S_TAP] = &scr_main;
    SCR[W_TC] = &scr_wtc; SCR[W_SEQ] = &scr_wseq; SCR[W_TRACK] = &scr_wtr; SCR[W_CHOP] = &scr_wchop;
    say("MOSURA OS");
}

void ui_encoder(int d)
{
    if (!d) return;
    const screen_t *s = SCR[scr];
    if (shift_held) shift_used = 1;
    if (push_held) {                                         /* cursor */
        push_turned = 1;
        if (s->move) s->move(d);
        else if (s->nf) cur[scr] = (uint8_t)CLAMP(cur[scr] + d, 0, s->nf - 1);
        return;
    }
    if (s->enc) { s->enc(d); return; }
    if (s->nf) { const field_t *f = &s->f[CLAMP(cur[scr], 0, s->nf - 1)]; f->adj(d); }
}

static void value_tap(void)
{
    const screen_t *s = SCR[scr];
    if (s->tap) { s->tap(); return; }
    if (s->nf) { const field_t *f = &s->f[cur[scr]]; if (f->win) go(f->win); else if (s->modal) back(); }
}

void ui_ctrl(int k, int v)
{
    const screen_t *s = SCR[scr];
    if (perf()) {                                            /* an effect is on: CTRLs are its */
        int p[3] = {ctrl_v[0], ctrl_v[1], ctrl_v[2]}; p[k] = v;
        for (int i = 0; i < 6; i++) if (fx_on[i]) { mos_host_fx(i, 1, p[0], p[1], p[2]); return; }
    }
    if (!s->q || !s->qget) return;
    if (ctrl_owner[k] != scr) { ctrl_owner[k] = scr; ctrl_eng[k] = 0; }
    int target = s->qget(k);
    if (!ctrl_eng[k]) {                                      /* soft takeover */
        int prev = ctrl_v[k];
        if (ABS(v - target) < 24 || (prev - target) * (v - target) <= 0) ctrl_eng[k] = 1;
        else { char b[8]; mos_fmt(b, 8, "%s", v < target ? ">>" : "<<"); qshow(s->qlab[k] ? s->qlab[k] : "", b); }
    }
    if (ctrl_eng[k]) s->q(k, v);
}

int ui_pad(int pad, int vel)
{
    if (scr == S_STOCK) return 0;
    if (shift_held && vel) { shift_used = 1; go(pad); return 1; }
    if (del_held && vel && M.playing && scr != S_GRID) { seq_erase_note(M.bank * MOS_PADS + pad); say("ERASE"); return 1; }
    if (roll_held && scr != S_REPEAT) return repeat_pad(pad, vel);
    if (!vel && (M.rpt_mask >> pad & 1)) { repeat_pad(pad, 0); return 1; }
    const screen_t *s = SCR[scr];
    if (s->pad && s->pad(pad, vel)) return 1;
    seq_live_note(pad, vel);
    if (vel) track_glow[M.cur_track] = 4;
    return 1;
}

int ui_key(int k, int down)
{
    /* stock mode: everything goes to Roland's firmware; a double SHIFT tap returns */
    if (scr == S_STOCK) {
        if (k == K_SHIFT && down) {
            if (M.ms - stock_shift_t < 400) { mos_host_stock(0); scr = S_MAIN; return 1; }
            stock_shift_t = M.ms;
        }
        return 0;
    }
    if (k != K_SHIFT && down && shift_held) shift_used = 1;
    switch (k) {
    case K_SHIFT:
        if (down) { shift_held = 1; shift_used = 0; shift_t = M.ms; }
        else {
            shift_held = 0;
            if (!shift_used && M.ms - shift_t < 500) { if (scr == S_MODES) back(); else go(S_MODES); }
        }
        return 1;
    case K_VALUE_PUSH:
        if (down) { push_held = 1; push_turned = 0; }
        else { push_held = 0; if (!push_turned) value_tap(); }
        return 1;
    case K_EXIT: if (down) back(); return 1;
    case K_PATTERN_SELECT:
        if (!down) return 1;
        if (M.playing) seq_stop(); else seq_play(!shift_held);
        return 1;
    case K_PATTERN_EDIT: if (down) seq_rec_toggle(); return 1;
    case K_REMAIN: if (down) { if (M.undo_ok) { seq_undo(); say("UNDO SEQ"); } else say("NOTHING TO UNDO"); } return 1;
    case K_DEL: del_held = (uint8_t)down; return 1;
    case K_ROLL:
        roll_held = (uint8_t)down;
        if (!down && scr != S_REPEAT) M.rpt_mask = 0;
        return 1;
    case K_COPY:
        if (down && scr == S_MAIN) {
            int dst = -1;
            for (int i = 0; i < SEQS; i++) if (!M.seq[i].used) { dst = i; break; }
            if (dst < 0) { say("NO FREE SEQUENCE"); return 1; }
            M.seq[dst] = *cs(); mos_fmt(M.seq[dst].name, sizeof M.seq[dst].name, "Sequence%02d", dst + 1);
            char b[24]; mos_fmt(b, 24, "COPIED TO SQ%02d", dst + 1); say(b);
            if (!M.playing) M.cur_seq = (uint8_t)dst; else M.next_seq = (uint8_t)dst;
        }
        return 1;
    case K_START_END: if (down) go(S_TRIM); return 1;
    case K_PITCH_SPEED: if (down) { go(S_PROG); cur[S_PROG] = 3; } return 1;
    case K_BANK_AF: case K_BANK_BG: case K_BANK_CH: case K_BANK_DI: case K_BANK_EJ:
        if (down) {
            int b = k - K_BANK_AF;
            M.bank = (uint8_t)(shift_held ? b + 5 : b);
            if (scr == S_MIXER || scr == S_PROG) M.last_note = (uint8_t)(M.bank * MOS_PADS + M.last_note % MOS_PADS);
        }
        return 0;                                            /* the stock bank follows too */
    case K_GATE: case K_LOOP: case K_REVERSE: case K_BPM_SYNC:
        if (down) {
            int bit = k == K_GATE ? PF_GATE : k == K_LOOP ? PF_LOOP : k == K_REVERSE ? PF_REVERSE : PF_BPMSYNC;
            lp()->flags ^= (uint8_t)bit; param(MOS_P_FLAGS, lp()->flags);
        }
        return 0;                                            /* stock pad attributes stay in step */
    default: break;
    }
    if (k >= K_FX1 && k <= K_FX6) {
        int i = k - K_FX1;
        if (!perf() && scr != S_MAIN) { if (down && SCR[scr]->soft) SCR[scr]->soft(i); return 1; }
        if (down) {
            fx_on[i] = !fx_on[i];
            mos_host_fx(i, fx_on[i], ctrl_v[0], ctrl_v[1], ctrl_v[2]);
        }
        return 0;                                            /* the stock effect too */
    }
    return 0;                                                /* REC, RESAMPLE, HOLD, MARK... */
}

/* ---- public entry points --------------------------------------------- */
void mos_init(void)
{
    mos_memset(&M, 0, sizeof M);
    seq_init();
    ui_init();
    for (int k = 0; k < 3; k++) ctrl_v[k] = -1;
}
void mos_tick(uint32_t us) { seq_tick(us); }
int  mos_key(int key, int down) { return ui_key(key, down); }
int  mos_pad(int pad, int vel) { if (pad < 0 || pad >= MOS_PADS) return 0; return ui_pad(pad, CLAMP(vel, 0, 127)); }
int  mos_encoder(int d) { if (scr == S_STOCK) return 0; ui_encoder(d); return 1; }
void mos_ctrl(int k, int v)
{
    if (k < 0 || k > 2) return;
    v = CLAMP(v, 0, 1023);
    if (ctrl_v[k] < 0) { ctrl_v[k] = v; return; }            /* first reading: just learn it */
    if (ABS(v - ctrl_v[k]) < 3) return;                      /* pot jitter */
    if (scr != S_STOCK) ui_ctrl(k, v);
    ctrl_v[k] = v;
}
const uint8_t *mos_render(void) { ui_draw(); return g_fb; }
const mos_leds_t *mos_leds(void) { update_leds(); return &leds; }
int mos_bank(void) { return M.bank; }
void mos_set_sample(int note, const char *name, const uint8_t *peaks, uint32_t frames)
{
    if (note < 0 || note >= MOS_NOTES) return;
    sample_t *s = &M.smp[note];
    mos_strcpy(s->name, name, sizeof s->name);
    if (peaks) mos_memcpy(s->peaks, peaks, MOS_WAVE_W);
    s->frames = frames; s->has = 1;
}
