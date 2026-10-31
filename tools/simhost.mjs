// Headless host for the Mosura core (wasm): used by tests and screenshots.
import { readFileSync } from 'node:fs';
import { deflateSync } from 'node:zlib';

export const K = {
  BUS_FX: 0x10, HOLD: 0x11, EXT_SOURCE: 0x12, SUB_PAD: 0x13, PATTERN_SELECT: 0x14,
  PATTERN_EDIT: 0x15, RECORD_SETTING: 0x16, START_END: 0x17, PITCH_SPEED: 0x18, MARK: 0x19,
  DEL: 0x1a, REC: 0x1b, RESAMPLE: 0x1c, BPM_SYNC: 0x1d, GATE: 0x1e, LOOP: 0x1f, REVERSE: 0x20,
  ROLL: 0x21, EXIT: 0x22, COPY: 0x23, REMAIN: 0x24, BANK_AF: 0x25, BANK_BG: 0x26, BANK_CH: 0x27,
  BANK_DI: 0x28, BANK_EJ: 0x29, SHIFT: 0x2a, FX1: 0x2b, FX2: 0x2c, FX3: 0x2d, FX4: 0x2e,
  FX5: 0x2f, FX6: 0x30, VALUE_PUSH: 0x31,
};

export async function boot(wasmPath = new URL('../sim/mosura.wasm', import.meta.url)) {
  const log = { notes: [], offs: [], clicks: 0, fx: [], chops: [], stock: [] };
  const env = {
    mos_host_note_on: (n, v, t, s, e) => log.notes.push({ n, v, t, s, e }),
    mos_host_note_off: (n) => log.offs.push(n),
    mos_host_click: () => { log.clicks++; },
    mos_host_fx: (fx, on, a, b, c) => log.fx.push({ fx, on, a, b, c }),
    mos_host_pad_param: () => {},
    mos_host_chop: (d, s, a, b) => log.chops.push({ d, s, a, b }),
    mos_host_stock: (on) => log.stock.push(on),
  };
  const { instance } = await WebAssembly.instantiate(readFileSync(wasmPath), { env });
  const x = instance.exports;
  const mem = () => new Uint8Array(x.memory.buffer);
  const api = {
    x, log,
    init() { x.mos_init(); },
    ms(n) { for (let i = 0; i < n; i++) x.mos_tick(1000); },
    key(k, hold = 30) { x.mos_key(k, 1); api.ms(hold); x.mos_key(k, 0); api.ms(10); },
    down(k) { x.mos_key(k, 1); }, up(k) { x.mos_key(k, 0); },
    pad(p, v = 100, hold = 40) { x.mos_pad(p, v); api.ms(hold); x.mos_pad(p, 0); },
    turn(d) { x.mos_encoder(d); },
    cursor(d) { x.mos_key(K.VALUE_PUSH, 1); x.mos_encoder(d); x.mos_key(K.VALUE_PUSH, 0); },
    tap() { api.key(K.VALUE_PUSH); },
    mode(p) { x.mos_key(K.SHIFT, 1); x.mos_pad(p, 100); x.mos_pad(p, 0); x.mos_key(K.SHIFT, 0); api.ms(5); },
    sample(note, name, peaks, frames) {
      const b = x.sim_buf(), m = mem();
      const enc = new TextEncoder().encode(name.slice(0, 12));
      m.set(enc, b); m[b + enc.length] = 0;
      m.set(peaks, b + 64);
      x.mos_set_sample(note, b, b + 64, frames);
    },
    frame() { const p = x.mos_render(); return mem().slice(p, p + 1024); },
    leds() { const p = x.mos_leds(); return mem().slice(p, p + x.sim_leds_size()); },
    pixel(fb, X, Y) { return (fb[(Y >> 3) * 128 + X] >> (Y & 7)) & 1; },
    ascii(fb) {
      let s = '';
      for (let y = 0; y < 64; y++) { for (let X = 0; X < 128; X++) s += api.pixel(fb, X, y) ? '#' : '.'; s += '\n'; }
      return s;
    },
    png(fb, scale = 4) { return pngFromFb(fb, scale, (X, Y) => api.pixel(fb, X, Y)); },
  };
  return api;
}

// OLED look: warm white pixels with a 1px gap, on near-black.
export function pngFromFb(fb, s, px) {
  const W = 128 * s + 2 * s * 2, H = 64 * s + 2 * s * 2, pad = s * 2;
  const raw = Buffer.alloc((W * 3 + 1) * H);
  for (let y = 0; y < H; y++) {
    raw[y * (W * 3 + 1)] = 0;
    for (let x = 0; x < W; x++) {
      const X = Math.floor((x - pad) / s), Y = Math.floor((y - pad) / s);
      let c = [12, 12, 14];
      if (X >= 0 && X < 128 && Y >= 0 && Y < 64 && px(X, Y)) {
        const gap = s >= 3 && ((x - pad) % s === s - 1 || (y - pad) % s === s - 1);
        c = gap ? [150, 170, 175] : [228, 246, 248];
      }
      const o = y * (W * 3 + 1) + 1 + x * 3;
      raw[o] = c[0]; raw[o + 1] = c[1]; raw[o + 2] = c[2];
    }
  }
  const chunk = (type, data) => {
    const len = Buffer.alloc(4); len.writeUInt32BE(data.length);
    const td = Buffer.concat([Buffer.from(type), data]);
    const crc = Buffer.alloc(4); crc.writeUInt32BE(crc32(td) >>> 0);
    return Buffer.concat([len, td, crc]);
  };
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(W, 0); ihdr.writeUInt32BE(H, 4); ihdr[8] = 8; ihdr[9] = 2;
  return Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', ihdr),
    chunk('IDAT', deflateSync(raw)), chunk('IEND', Buffer.alloc(0))]);
}
let T;
function crc32(b) {
  if (!T) { T = new Int32Array(256); for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; T[n] = c; } }
  let c = -1; for (const v of b) c = T[(c ^ v) & 255] ^ (c >>> 8); return c ^ -1;
}

// Synthetic peaks for a named sound, for screens and tests.
export function fakePeaks(kind, seed = 1) {
  const p = new Uint8Array(96); let r = seed;
  const rnd = () => ((r = (r * 1103515245 + 12345) & 0x7fffffff) / 0x7fffffff);
  for (let i = 0; i < 96; i++) {
    const t = i / 96;
    let v;
    if (kind === 'loop') v = (0.25 + 0.75 * Math.exp(-((i % 12) / 4))) * (0.7 + 0.3 * rnd());
    else if (kind === 'pad') v = Math.min(1, t * 6) * (1 - t * 0.5) * (0.8 + 0.2 * rnd());
    else v = Math.exp(-t * (kind === 'hat' ? 18 : 6)) * (0.75 + 0.25 * rnd());
    p[i] = Math.round(255 * Math.min(1, v));
  }
  return p;
}
