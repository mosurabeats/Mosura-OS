# SP-404MKII hardware notes

What we know, where it came from, and what Mosura depends on. Almost all of
this was established by the **DM-404** project (an open-source SP-404MKII
emulator: https://github.com/nickpetty/dm-404, tools under MIT), which runs
Roland's own 5.52 firmware in a QEMU fork. Credit to its author. We re-state
facts here; we copy no code.

## Platform

| | |
|---|---|
| SoC | NXP i.MX RT1060, Cortex-M7 @ 600 MHz |
| OS | micro T-Kernel 2.00.44 + NXP MCUXpresso SDK drivers |
| Firmware | `SP404MKII_APP1.bin` (main app) flashed at QSPI 0x60080000; `SP404MKII_APP0.bin` is the BMC sound chip's firmware (partly encrypted) |
| 5.52 APP1 sha256 | `4a3d67711e14dcc97d50249a4eee7dd6df0251a2f37757cbbe2556c233730d80` |
| Memory | ITCM, DTCM, OCRAM, 64 MB SDRAM at 0x80000000 (scatter-loaded by Arm Compiler startup) |
| Display | SSD1309 128×64 OLED on LPSPI4 (0x403A0000), D/C = GPIO2.1, RES# = GPIO2.10 |
| Keys | GPIO matrix (rows GPIO2.23–25 × cols GPIO2.20,21,22,28,26), active low |
| Pads | 16 pressure pads on ADC1 ch4/ch5 through a mux, scanned at 2 kHz |
| CTRL 1–3, SUB PAD | ADC1 ch6 mux 1–4 |
| VALUE | quadrature on GPIO2.18/19; menus see it as keys 0x3f/0x40, push = 0x31 |
| LEDs | sent to the BMC over LPUART3 (1 Mbaud): `01 00 idx value`; pads idx 0–0x2f (RGB triplets), key LED = key ID + 0x21 |
| Effects | computed by the BMC chip, configured by DT1 SysEx from the i.MX |
| Audio | SAI1 TDM to the BMC, 64-frame blocks (1.33 ms) |

## Key IDs

These are the firmware's own numbers, and `core/include/mosura.h` uses them
as they are:

```
0x10 BUS FX        0x18 PITCH/SPEED   0x20 REVERSE      0x28 D/I
0x11 HOLD          0x19 MARK          0x21 ROLL         0x29 E/J
0x12 EXT SOURCE    0x1a DEL           0x22 EXIT         0x2a SHIFT
0x13 SUB PAD       0x1b REC           0x23 COPY         0x2b FILTER+DRIVE
0x14 PATTERN SEL   0x1c RESAMPLE      0x24 REMAIN       0x2c RESONATOR
0x15 PATTERN EDIT  0x1d BPM SYNC      0x25 A/F          0x2d DELAY
0x16 REC SETTING   0x1e GATE          0x26 B/G          0x2e ISOLATOR
0x17 START/END     0x1f LOOP          0x27 C/H          0x2f DJFX LOOPER
                                                        0x30 MFX
                                                        0x31 VALUE push
```

## Firmware landmarks (5.52, from DM-404's notes)

- 94 UI pages; handler table 0x8023b4fc; current page ID at 0x80245880;
  `FUN_800da598(n)` requests a page; the main page is 68.
- DrawString `FUN_800ee530`; file open `FUN_800b5d88(handle, path, mode)`.
- Key matrix decoding `FUN_8005da10`, ID table 0x82e48774.
- Audio task `FUN_0001cf78` (T-Kernel task 1), DMA IRQ `FUN_80003408`.

## How Mosura attaches

The core is self-contained (no libc, no allocation, no floats in the hot
path). The port (`port/sp404/port.c`) is a thin layer the patched firmware
calls:

```
key matrix  ──► mosura_on_key(id, down) ──► 1: consumed
                                          └► 0: original handler runs
pad scan    ──► mosura_on_pad(pad, vel)
UI loop     ──► mosura_tick()            (DWT cycle counter → µs)
OLED flush  ──► mosura_frame()           (1 KB, SSD1309 page layout, or 0 = stock)
core        ──► fw.note_on/off, fw.led, fw.request_page
```

The core draws straight into the SSD1309's page format (8 pages × 128
columns, LSB at the top), so the frame goes to the panel without
conversion.

Open questions, tracked in `port/sp404/hooks.json`:

1. Free SDRAM for ~35 KB code + ~185 KB state, and how to get our code
   loaded (an extra scatter row, or a copy from a hook in startup).
2. Does the updater check a checksum inside APP1? DOOM OS ships a patched
   APP1 of a different size, which suggests no fixed-size CRC. Verify.
3. The routine that sends the frame over LPSPI4.
4. The routine that starts a pad's voice, so sequenced notes sound exactly
   like hits (choke, gate, BPM sync, effects buses all for free).
