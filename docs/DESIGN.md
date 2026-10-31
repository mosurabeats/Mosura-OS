# Mosura OS design

## The problem

The SP-404MKII is a great instrument with a small panel. Most functions sit
behind a SHIFT combination, a held key plus a pad, or a menu page. You have
to remember where things are, because the screen rarely tells you. The
sequencer (TR-REC and pattern mode) is the worst case: it works, but you
can't see your music and you can't edit it like a list.

## What we take from the MPCs

The MPC2000XL, MPC2500 and MPC5000 share a few rules that made them fast.
Mosura keeps the rules, not the look.

| MPC principle | What it means | Mosura on the SP-404MKII |
|---|---|---|
| **One home screen** (MAIN) | Sequence, track, tempo, position and timing correct all visible and editable in one place | `MAIN`: SQ, TR, LOOP, BARS, TC, SWING, big tempo, a timeline of the sequence |
| **Modes are one gesture away** (MODE + pad on the 2000XL/1000/2500) | No menu trees: pick a pad, you're there | Tap `SHIFT`, then a pad, or hold `SHIFT` + pad. The 4×4 mode grid is drawn on screen as it maps to the pads |
| **Cursor + DATA wheel** | Every value is a field: move to it, turn | `VALUE` turn changes the field; `VALUE` push+turn moves the cursor |
| **WINDOW key** | Details of the field under the cursor open in a window | `VALUE` tap opens the window (TC/SWING, SEQUENCE, TRACK, CHOP) |
| **Soft keys F1–F6** labelled on screen | Context actions without memorising | The six effect buttons are F1–F6 on edit screens; the footer always labels them |
| **Q-Links** (1000/2500/5000) | Physical controls for three or four parameters of the current screen | CTRL 1–3 are per-screen Q-Links with soft takeover; a popup shows name and value |
| **Timing Correct + Swing** | Recording lands on the grid, with MPC swing | 1/4 … 1/32T, swing 50–75 % on 1/8 and 1/16 |
| **Overdub, Undo Seq, Erase** | Loop-record without fear | TR-REC arms/punches; REMAIN = UNDO SEQ (again = redo); hold DEL + pad while playing = ERASE |
| **Note Repeat** | Hold a pad, get rolls at the TC rate | ROLL + pad, or the NOTE REPEAT pad mode; rate on CTRL 1 |
| **Full Level, 16 Levels** | Fixed velocity; one sound across 16 pads by velocity or tune | Mode pads 13 and 11 |
| **Track Mute, Next Seq** | Perform arrangements live from the pads | Mode pads 10 and 9: pads mute/solo tracks or queue sequences |
| **Step Edit** (event list) | See and edit every note as numbers | `STEP`: position, pad, velocity, duration, tune per event; pads step-record |
| **Grid edit** (5000) | See a drum pattern as a grid | `GRID`: 16 steps × pads, pads toggle steps, velocity by fill |
| **Song mode** | Chain sequences with repeats | `SONG`: steps of SQ × repeats, INS/DEL, SONG MODE on/off |
| **Program / Mixer** | Per-pad level, pan, tune, decay, mute groups | `PROGRAM` and `MIXER` (16 strips, LEVEL/PAN/TUNE views) |
| **Chop Shop** (1000/2500) | Slice a sample to pads | `TRIM`: start/end with waveform; CHOP into 2–16 equal slices on the next bank |

## The control model

A handful of rules, and they hold on every screen:

1. **VALUE turn** changes the field under the cursor.
2. **VALUE push + turn** moves the cursor. **VALUE tap** opens the field's window.
3. **EXIT** closes a window or goes back to MAIN.
4. **SHIFT tap** opens the mode grid. **SHIFT + pad** jumps straight to a mode.
5. **The footer labels the six effect buttons.** On edit screens (STEP,
   GRID, SONG, PROGRAM, MIXER, TRIM, SETUP, windows) they are soft keys
   F1–F6. On performance screens (MAIN, NEXT SEQ, TRACK MUTE, 16 LEVELS,
   NOTE REPEAT) they are the stock effects, and the footer shows which are on.
6. **CTRL 1–3 are Q-Links**, except when an effect is on during a
   performance screen. Then they belong to the effect, as on the stock unit.

Only two combinations exist (SHIFT + pad and push + turn), and the screen
explains both.

### Key map

| Panel key | Mosura | Stock behaviour kept? |
|---|---|---|
| PATTERN SELECT | PLAY / STOP (from start). SHIFT: play from the current position | — |
| TR-REC (PATTERN EDIT) | REC: arm overdub when stopped, punch in/out while playing | — |
| REMAIN | UNDO SEQ (press again to redo) | — |
| DEL | hold + pad while playing = ERASE that pad near the playhead; DEL in GRID + pad = select row | — |
| COPY | on MAIN: copy the sequence to the next free slot | — |
| ROLL | hold + pads = NOTE REPEAT | — |
| START/END | jump to TRIM | — |
| PITCH/SPEED | jump to PROGRAM (TUNE field) | — |
| EXIT | back / close window | — |
| A/F … E/J | pad bank (SHIFT for F–J) | yes, the stock bank follows |
| GATE, LOOP, REVERSE, BPM SYNC | toggle the attribute on the last pad | yes, passed through |
| FX ×6 | F1–F6 on edit screens, effects elsewhere | yes on performance screens |
| REC, RESAMPLE, MARK, HOLD, BUS FX, EXT SOURCE, SUB PAD, REC SETTING | untouched | yes, passed through |

The core reports every input as *consumed* or *not*. Anything it doesn't
consume goes to Roland's firmware unchanged, so sampling, resampling, skip
back, effects and bus routing keep working exactly as they do today.

### Mode grid

```
 MAIN    STEP    GRID    SONG
 PROG    MIXER   TRIM    SETUP
 NXTSQ   MUTE    16LEV   REPT
 FULLV   TAP     HELP    STOCK
```

The pads light to match. STOCK hands the screen back to Roland's UI;
double-tap SHIFT to return. Nothing is ever out of reach.

## Screens

The OLED is 128×64, monochrome (SSD1309). The MPC2000XL had 240×64, so the
MPC idiom of dense, labelled fields fits once the type is small enough. Mosura
uses its own 3×5 pixel face in a 4×6 cell: 32 columns by 10 lines.

Layout grid, the same everywhere:

```
 y 0-6    header: inverse mode tab, dotted rule, status (transport, bar.beat)
 y 9-54   body
 y 56-63  footer: six soft-key cells, one per effect button
```

Visual language: inverse = selected/active; rounded frames = controls;
dotted lines and frames = empty, unused, or "not here"; dithering = partial
(soft velocities, out-of-region waveform). One accent glyph set: ▶ ■ ● ▲ ▼ ◀.

Screenshots of every screen are in `docs/img/` (generated by `make shots`).

## What stays stock

Sampling, resampling, the effects engine (it runs on a separate DSP chip),
the looper, DJ mode, USB audio and storage formats. Mosura is a new front end
and a new sequencer. It is not a new audio engine.
