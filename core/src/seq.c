/* Mosura OS core: the sequencer. 96 PPQ, MPC2000XL-style. MIT licence. */
#include "internal.h"

mos_state_t M;

const char *const tc_names[TC_COUNT] = {"OFF", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T", "1/4"};
const uint16_t tc_ticks[TC_COUNT]    = {1, 48, 32, 24, 16, 12, 8, 96};

static uint32_t tap_ms[4];
static uint8_t  ntap;

void seq_ensure(int i)
{
    seq_t *s = &M.seq[i];
    if (s->used) return;
    mos_memset(s, 0, sizeof *s);
    s->used = 1; s->bars = 2; s->beats = 4; s->loop = 1; s->tempo10 = M.tempo10;
    mos_fmt(s->name, sizeof s->name, "Sequence%02d", i + 1);
    for (int t = 0; t < TRACKS; t++) {
        mos_fmt(s->tr[t].name, sizeof s->tr[t].name, "Track-%d", t + 1);
        s->tr[t].vel_pct = 100;
    }
}

void seq_init(void)
{
    M.tempo10 = 900; M.tc = 3; M.rpt_tc = 3; M.swing = 50; M.click_on = 1; M.count_in = 1;
    M.next_seq = 0xff;
    for (int n = 0; n < MOS_NOTES; n++) {
        pad_t *p = &M.pad[n];
        p->level = 100; p->pan = 50; p->decay = 100; p->end_pm = 1000;
    }
    seq_ensure(0);
    for (int i = 0; i < MOS_PADS; i++) M.pad_rec_idx[i] = -1;
    M.song[0].seq = 0; M.song[0].reps = 2; M.song_len = 1;
}

uint32_t seq_len_ticks(const seq_t *s) { return (uint32_t)s->bars * BAR_TICKS(s); }
uint16_t seq_tempo(void) { return M.tempo_src_seq ? M.seq[M.cur_seq].tempo10 : M.tempo10; }
void seq_set_tempo(int t10)
{
    t10 = CLAMP(t10, 300, 3000);
    if (M.tempo_src_seq) M.seq[M.cur_seq].tempo10 = (uint16_t)t10; else M.tempo10 = (uint16_t)t10;
}

void seq_bbc(uint32_t tick, int *bar, int *beat, int *clk)
{
    const seq_t *s = &M.seq[M.cur_seq];
    uint32_t bt = BAR_TICKS(s);
    *bar = (int)(tick / bt) + 1;
    *beat = (int)((tick % bt) / MOS_PPQ) + 1;
    *clk = (int)(tick % MOS_PPQ);
}

/* Timing correct with MPC swing: every second grid step is pushed late. */
uint32_t seq_quantize(uint32_t tick)
{
    uint32_t g = tc_ticks[M.tc];
    if (g <= 1) return tick;
    uint32_t q = (tick + g / 2) / g;
    uint32_t t = q * g;
    if (M.swing > 50 && (g == 24 || g == 48) && (q & 1))
        t += (uint32_t)(g * 2) * (M.swing - 50) / 100;
    return t;
}

void seq_sort(seq_t *s)
{
    for (int i = 1; i < s->nev; i++) {     /* insertion sort: lists are nearly sorted */
        event_t e = s->ev[i]; int j = i - 1;
        while (j >= 0 && s->ev[j].tick > e.tick) { s->ev[j + 1] = s->ev[j]; j--; }
        s->ev[j + 1] = e;
    }
}

int seq_add_event(seq_t *s, const event_t *e)
{
    if (s->nev >= EVENTS) return -1;
    int i = s->nev;
    while (i > 0 && s->ev[i - 1].tick > e->tick) { s->ev[i] = s->ev[i - 1]; i--; }
    s->ev[i] = *e; s->nev++;
    return i;
}

void seq_del_event(seq_t *s, int i)
{
    if (i < 0 || i >= s->nev) return;
    for (; i < s->nev - 1; i++) s->ev[i] = s->ev[i + 1];
    s->nev--;
}

void seq_snapshot(void)
{
    M.undo = M.seq[M.cur_seq];
    M.undo_seq = M.cur_seq; M.undo_ok = 1;
}
void seq_undo(void)
{
    if (!M.undo_ok) return;
    seq_t tmp = M.seq[M.undo_seq];        /* undo of undo = redo, as on the MPC */
    M.seq[M.undo_seq] = M.undo;
    M.undo = tmp;
}

/* ---- sounding ----------------------------------------------------------- */
static void hold_note(int note, uint32_t off_tick)
{
    for (int i = 0; i < M.nheld; i++)
        if (M.held[i].note == note) { M.held[i].off = off_tick; return; }
    if (M.nheld < 32) { M.held[M.nheld].note = (uint8_t)note; M.held[M.nheld].off = off_tick; M.nheld++; }
}

void seq_trigger(int note, int vel, int tune, uint32_t dur)
{
    pad_t *p = &M.pad[note];
    if (p->flags & PF_MUTE) return;
    /* mute groups: a new note in a group silences the others */
    if (p->mute_grp)
        for (int i = 0; i < M.nheld; i++)
            if (M.held[i].note != note && M.pad[M.held[i].note].mute_grp == p->mute_grp) {
                mos_host_note_off(M.held[i].note);
                M.held[i] = M.held[--M.nheld]; i--;
            }
    mos_host_note_on(note, vel, tune + p->tune, p->start_pm, p->end_pm);
    if (note / MOS_PADS == M.bank) ui_pad_note(note % MOS_PADS, 1);
    if (dur) hold_note(note, M.ms + (uint32_t)((uint64_t)dur * 600000 / ((uint64_t)seq_tempo() * MOS_PPQ)));
}

void seq_all_off(void)
{
    for (int i = 0; i < M.nheld; i++) mos_host_note_off(M.held[i].note);
    M.nheld = 0;
}

static void play_range(uint32_t from, uint32_t to)   /* [from, to) */
{
    seq_t *s = &M.seq[M.cur_seq];
    int solo = 0;
    for (int t = 0; t < TRACKS; t++) solo |= s->tr[t].solo;
    for (int i = 0; i < s->nev; i++) {
        event_t *e = &s->ev[i];
        if (e->tick < from) continue;
        if (e->tick >= to) break;
        track_t *tr = &s->tr[e->track];
        if (tr->mute || (solo && !tr->solo)) continue;
        int pd = e->note % MOS_PADS;
        if (M.recording && M.pad_rec_idx[pd] == i) continue;
        if (M.guard_on[pd] && M.guard_note[pd] == e->note && M.guard_tick[pd] == e->tick) { M.guard_on[pd] = 0; continue; }
        int v = e->vel * tr->vel_pct / 100;
        seq_trigger(e->note, CLAMP(v, 1, 127), e->tune, e->dur ? e->dur : 24);
    }
}

static void loop_wrap(void)
{
    seq_t *s = &M.seq[M.cur_seq];
    if (M.song_mode) {
        if (++M.song_rep >= M.song[M.song_pos].reps) {
            M.song_rep = 0;
            if (++M.song_pos >= M.song_len) M.song_pos = 0;
        }
        M.cur_seq = M.song[M.song_pos].seq; seq_ensure(M.cur_seq);
    } else if (M.next_seq != 0xff) {
        M.cur_seq = M.next_seq; M.next_seq = 0xff; seq_ensure(M.cur_seq);
        M.recording = 0;
    } else if (!s->loop) {
        seq_stop();
        return;
    }
    M.pos = 0;
}

void seq_tick(uint32_t us)
{
    /* wall clock in ms for gates */
    M.us_frac += us;
    while (M.us_frac >= 1000) { M.us_frac -= 1000; M.ms++; ui_tick_ms(); }
    for (int i = 0; i < M.nheld; i++)
        if ((int32_t)(M.ms - M.held[i].off) >= 0 && M.held[i].off) {
            mos_host_note_off(M.held[i].note);
            int n = M.held[i].note;
            if (n / MOS_PADS == M.bank) ui_pad_note(n % MOS_PADS, 0);
            M.held[i] = M.held[--M.nheld]; i--;
        }
    /* note repeat runs on the transport when playing, else on a free clock */
    if (M.rpt_mask) {
        if (!M.playing) {
            M.free_acc += (uint64_t)us * seq_tempo() * MOS_PPQ;
            while (M.free_acc >= 600000000ull) {
                M.free_acc -= 600000000ull;
                if (M.free_pos++ % tc_ticks[M.rpt_tc] == 0) seq_repeat_fire();
            }
        }
    } else M.free_pos = 0;
    if (!M.playing) return;

    /* ticks = us * tempo10 * PPQ / (60e6 * 10) */
    M.acc += (uint64_t)us * seq_tempo() * MOS_PPQ;
    const uint64_t one = 600000000ull;
    while (M.acc >= one) {
        M.acc -= one;
        seq_t *s = &M.seq[M.cur_seq];
        if (M.counting) {
            if (M.pos % MOS_PPQ == 0) mos_host_click(M.pos == 0);
            if (++M.pos >= BAR_TICKS(s)) { M.counting = 0; M.pos = 0; }
            else continue;
        }
        if (M.pos % MOS_PPQ == 0) {
            int bs = M.pos % BAR_TICKS(s) == 0;
            if (M.click_on && (M.recording || M.click_on == 2)) mos_host_click(bs);
            ui_beat((int)(M.pos / MOS_PPQ), bs);
        }
        play_range(M.pos, M.pos + 1);
        if (M.rpt_mask && M.pos % tc_ticks[M.rpt_tc] == 0) seq_repeat_fire();
        if (++M.pos >= seq_len_ticks(s)) loop_wrap();
    }
}

void seq_play(int from_start)
{
    if (M.playing) return;
    if (from_start) { M.pos = 0; if (M.song_mode) { M.song_pos = 0; M.song_rep = 0; M.cur_seq = M.song[0].seq; seq_ensure(M.cur_seq); } }
    M.acc = 0;
    M.playing = 1;
    if (M.overdub_armed) {
        seq_snapshot();
        M.recording = 1; M.overdub_armed = 0;
        M.counting = M.count_in && from_start;
    }
    if (!M.counting) play_range(M.pos, M.pos + 1), M.pos++;
}

void seq_stop(void)
{
    M.playing = 0; M.recording = 0; M.counting = 0; M.overdub_armed = 0;
    for (int i = 0; i < MOS_PADS; i++) M.pad_rec_idx[i] = -1;
    seq_all_off();
}

void seq_rec_toggle(void)
{
    if (M.playing) {                     /* punch in / out */
        if (!M.recording) seq_snapshot();
        M.recording = !M.recording;
    } else {
        M.overdub_armed = !M.overdub_armed;
    }
}

/* A live pad: plays it, and records it when recording. */
void seq_live_note(int pad, int vel)
{
    int note = M.bank * MOS_PADS + pad;
    seq_t *s = &M.seq[M.cur_seq];
    if (vel) {
        if (M.full_level) vel = 127;
        M.last_note = (uint8_t)note;
        pad_t *p = &M.pad[note];
        seq_trigger(note, vel, 0, (p->flags & PF_GATE) ? 0 : 24 * 4);
        if ((p->flags & PF_GATE)) hold_note(note, 0);
        M.pad_down_tick[pad] = M.pos;
        if (M.recording && !M.counting) {
            event_t e = {0};
            uint32_t len = seq_len_ticks(s);
            e.tick = seq_quantize(M.pos ? M.pos - 1 : 0) % len;
            e.note = (uint8_t)note; e.vel = (uint8_t)vel; e.track = M.cur_track; e.dur = 24;
            M.pad_rec_idx[pad] = (int16_t)seq_add_event(s, &e);
            if (e.tick >= M.pos) { M.guard_on[pad] = 1; M.guard_note[pad] = e.note; M.guard_tick[pad] = e.tick; }
        }
    } else {
        if (M.pad[note].flags & PF_GATE) {
            mos_host_note_off(note);
            for (int i = 0; i < M.nheld; i++)
                if (M.held[i].note == note) { M.held[i] = M.held[--M.nheld]; break; }
            ui_pad_note(pad, 0);
        }
        int i = M.pad_rec_idx[pad];
        if (i >= 0 && i < s->nev && s->ev[i].note == note) {
            uint32_t len = seq_len_ticks(s);
            uint32_t d = (M.pos + len - M.pad_down_tick[pad]) % len;
            s->ev[i].dur = (uint16_t)CLAMP((int)d, 1, 65535);
        }
        M.pad_rec_idx[pad] = -1;
    }
}

/* MPC ERASE: hold DEL and a pad while playing removes that note near the
 * playhead, on the current track. */
void seq_erase_note(int note)
{
    seq_t *s = &M.seq[M.cur_seq];
    uint32_t w = tc_ticks[M.tc] > 1 ? tc_ticks[M.tc] : 12;
    for (int i = 0; i < s->nev; i++) {
        event_t *e = &s->ev[i];
        if (e->note != note || e->track != M.cur_track) continue;
        uint32_t d = M.pos > e->tick ? M.pos - e->tick : e->tick - M.pos;
        if (d <= w) { seq_del_event(s, i); i--; }
    }
}

void seq_tap(void)
{
    if (ntap && M.ms - tap_ms[(ntap - 1) & 3] > 2000) ntap = 0;
    tap_ms[ntap & 3] = M.ms; ntap++;
    if (ntap >= 2) {
        int n = ntap < 4 ? ntap : 4;
        uint32_t newest = tap_ms[(ntap - 1) & 3], oldest = tap_ms[(ntap - n) & 3];
        uint32_t per = (newest - oldest) / (n - 1);
        if (per) seq_set_tempo((int)(600000 / per));
    }
}

/* NOTE REPEAT: every held pad sounds on the repeat grid, and records. */
void seq_repeat_fire(void)
{
    seq_t *s = &M.seq[M.cur_seq];
    for (int pad = 0; pad < MOS_PADS; pad++) {
        if (!(M.rpt_mask & (1u << pad))) continue;
        int note = M.bank * MOS_PADS + pad, vel = M.full_level ? 127 : M.rpt_vel[pad];
        seq_trigger(note, vel, 0, tc_ticks[M.rpt_tc] / 2 + 1);
        M.last_note = (uint8_t)note;
        if (M.playing && M.recording && !M.counting) {
            event_t e = {0};
            e.tick = M.pos % seq_len_ticks(s); e.note = (uint8_t)note; e.vel = (uint8_t)vel;
            e.track = M.cur_track; e.dur = (uint16_t)(tc_ticks[M.rpt_tc] / 2 + 1);
            seq_add_event(s, &e);   /* at the playhead: already past, no guard */
        }
    }
}
