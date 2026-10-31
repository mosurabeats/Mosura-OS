# Roadmap

## Phase 0: design and core ✅ (this commit)

- MPC-style control model and screen system (docs/DESIGN.md)
- Portable C core: 15 screens, 4 windows, sequencer (96 PPQ, TC + swing,
  overdub, punch, undo/redo, erase, note repeat, next seq, song mode, track
  mute/solo, 16 levels, full level, step record, grid edit, chop)
- Browser simulator running the same core (WebAssembly)
- Builds for Cortex-M7 (`make device`, ~33 KB code)
- `mospatch` toolchain: version check, scatter-table unpack, hook table,
  guarded patch builder

## Phase 1: find the hooks (emulator)

Work in DM-404 (QEMU + GDB + Ghidra) with your own 5.52 file.

1. `mospatch unpack` → load the regions into Ghidra.
2. Confirm each `candidate` in `port/sp404/hooks.json`; find each `todo`.
3. Record expected bytes for every patch site; mark the hook `verified`.

Exit: `mospatch hooks` reports 10 of 10 verified.

## Phase 2: first light (emulator)

- Load Mosura's code into free SDRAM, call `mosura_boot` once the main page is up.
- Replace the OLED frame with `mosura_frame()`. Keys stay stock.
- Then route keys, VALUE and pads; fall through to stock when not consumed.

Exit: MAIN, MODE grid and STOCK hand-off work in DM-404.

## Phase 3: sound

- `mos_host_note_on/off` → the firmware's own pad trigger.
- Sequencer timing from the cycle counter; check jitter against the 1.33 ms audio blocks.
- LEDs through the BMC link.

Exit: record, overdub, undo and song playback in the emulator, timing within 1 ms.

## Phase 4: hardware beta

- Volunteers with a unit and a recovery plan. Roland's updater can always
  re-flash the stock 5.52 file (hold SHIFT at power-on).
- Browser patcher (same checks as `mospatch build`), no files uploaded.

## Phase 5: depth

- Save/load Mosura projects (sequences, songs, programs) to SD as plain files
- Per-note tune and trim on the stock voice (16 LEVELS: TUNE, CHOP playback)
- Q-Link recording (MPC2500/5000 style), arpeggiator (MPC5000)
- Sequence edit tools: copy bars, shift timing, change velocity (MPC2000XL EDIT SEQ)
- Transient chop (Chop Shop "auto"), MIDI clock in/out settings
- Name entry with pads (MPC2000XL style)
