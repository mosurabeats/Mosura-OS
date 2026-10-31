// Mosura simulator: runs the real Mosura core (mosura.wasm) behind an
// SP-404MKII-style panel. The core is the same C that builds for the unit.
import { Engine } from './audio.js';

const K = {
  BUS_FX: 0x10, HOLD: 0x11, EXT_SOURCE: 0x12, SUB_PAD: 0x13, PATTERN_SELECT: 0x14, PATTERN_EDIT: 0x15,
  RECORD_SETTING: 0x16, START_END: 0x17, PITCH_SPEED: 0x18, MARK: 0x19, DEL: 0x1a, REC: 0x1b,
  RESAMPLE: 0x1c, BPM_SYNC: 0x1d, GATE: 0x1e, LOOP: 0x1f, REVERSE: 0x20, ROLL: 0x21, EXIT: 0x22,
  COPY: 0x23, REMAIN: 0x24, BANK_AF: 0x25, BANK_BG: 0x26, BANK_CH: 0x27, BANK_DI: 0x28, BANK_EJ: 0x29,
  SHIFT: 0x2a, FX1: 0x2b, FX2: 0x2c, FX3: 0x2d, FX4: 0x2e, FX5: 0x2f, FX6: 0x30, VALUE_PUSH: 0x31,
};

// [key, printed label, Mosura role]
const FX_KEYS = [[K.FX1, 'FILTER+DRIVE', 'F1'], [K.FX2, 'RESONATOR', 'F2'], [K.FX3, 'DELAY', 'F3'],
  [K.FX4, 'ISOLATOR', 'F4'], [K.FX5, 'DJFX LOOPER', 'F5'], [K.FX6, 'MFX', 'F6']];
const FN_KEYS = [
  [K.PATTERN_SELECT, 'PATTERN SELECT', 'PLAY / STOP'], [K.PATTERN_EDIT, 'TR-REC', 'REC · OVERDUB'],
  [K.REMAIN, 'REMAIN', 'UNDO SEQ'], [K.EXIT, 'EXIT', 'BACK'],
  [K.START_END, 'START/END', 'TRIM'], [K.PITCH_SPEED, 'PITCH/SPEED', 'PROGRAM'],
  [K.DEL, 'DEL', 'ERASE'], [K.COPY, 'COPY', 'COPY SEQ'],
  [K.ROLL, 'ROLL', 'NOTE REPEAT'], [K.MARK, 'MARK', 'stock'],
  [K.REC, 'REC', 'stock'], [K.RESAMPLE, 'RESAMPLE', 'stock'],
  [K.BPM_SYNC, 'BPM SYNC', 'pad attr'], [K.GATE, 'GATE', 'pad attr'],
  [K.LOOP, 'LOOP', 'pad attr'], [K.REVERSE, 'REVERSE', 'pad attr'],
];
const SIDE_KEYS = [[K.BUS_FX, 'BUS FX', 'stock'], [K.HOLD, 'HOLD', 'stock'], [K.EXT_SOURCE, 'EXT SOURCE', 'stock'],
  [K.RECORD_SETTING, 'REC SETTING', 'stock']];
const BANK_KEYS = [[K.BANK_AF, 'A/F'], [K.BANK_BG, 'B/G'], [K.BANK_CH, 'C/H'], [K.BANK_DI, 'D/I'], [K.BANK_EJ, 'E/J']];

const PAD_KEYS = ['1', '2', '3', '4', 'q', 'w', 'e', 'r', 'a', 's', 'd', 'f', 'z', 'x', 'c', 'v'];
const KEYMAP = {
  ' ': K.PATTERN_SELECT, 'Enter': K.VALUE_PUSH, 'Escape': K.EXIT, 'Shift': K.SHIFT, 'Backspace': K.DEL,
  'u': K.REMAIN, 'y': K.PATTERN_EDIT, 't': K.ROLL, 'F1': K.FX1, 'F2': K.FX2, 'F3': K.FX3, 'F4': K.FX4,
  'F5': K.FX5, 'F6': K.FX6, '[': K.START_END, ']': K.PITCH_SPEED, 'k': K.COPY,
  '5': K.FX1, '6': K.FX2, '7': K.FX3, '8': K.FX4, '9': K.FX5, '0': K.FX6,
  'g': K.BANK_AF, 'h': K.BANK_BG, 'j': K.BANK_CH, 'l': K.BANK_DI, ';': K.BANK_EJ,
};

let core, mem, eng, schedAt = 0;
const els = { keys: new Map(), pads: [] };
const $ = (s) => document.querySelector(s);

async function loadCore() {
  const env = {
    mos_host_note_on: (n, v, t, s, e) => eng && eng.noteOn(n, v, t, s, e, schedAt),
    mos_host_note_off: (n) => eng && eng.noteOff(n, schedAt),
    mos_host_click: (a) => eng && eng.click(a, schedAt),
    mos_host_fx: (i, on, a, b, c) => { eng && eng.fx(i, on, a, b, c); if (i === 1 || i === 4 || i === 5) note('This effect runs on the unit’s DSP; the simulator only has FILTER, DELAY and ISOLATOR.'); },
    mos_host_pad_param: (n, p, v) => { if (!eng) return; const pp = eng.padParams(n); pp[['level', 'pan', 'tune', 'decay', 'flags'][p]] = v; },
    mos_host_chop: (d, s, a, b) => eng && eng.chop(d, s, a, b),
    mos_host_stock: (on) => note(on ? 'STOCK: on the unit Roland’s screens take over here. Double-tap SHIFT to return.' : 'Back in Mosura.'),
  };
  const src = window.MOSURA_WASM_B64
    ? Uint8Array.from(atob(window.MOSURA_WASM_B64), (c) => c.charCodeAt(0))
    : await (await fetch(new URL('./mosura.wasm', import.meta.url))).arrayBuffer();
  const { instance } = await WebAssembly.instantiate(src, { env });
  core = instance.exports;
  mem = () => new Uint8Array(core.memory.buffer);
  core.mos_init();
}

function setSample(note, name, buf) {
  eng.set(note, name, buf);
  const b = core.sim_buf(), m = mem(), enc = new TextEncoder().encode(name.toUpperCase().slice(0, 12));
  m.set(enc, b); m[b + enc.length] = 0;
  m.set(Engine.peaks(buf), b + 64);
  core.mos_set_sample(note, b, b + 64, buf.length);
}

let noteTimer;
function note(s) {
  const n = $('#note'); n.textContent = s; n.classList.add('show');
  clearTimeout(noteTimer); noteTimer = setTimeout(() => n.classList.remove('show'), 3500);
}

// ---- input --------------------------------------------------------------
const key = (k, down) => { if (eng) schedAt = 0; core.mos_key(k, down ? 1 : 0); };
function bindButton(el, k) {
  const press = (e) => { e.preventDefault(); el.setPointerCapture?.(e.pointerId); el.classList.add('down'); key(k, 1); };
  const release = () => { if (!el.classList.contains('down')) return; el.classList.remove('down'); key(k, 0); };
  el.addEventListener('pointerdown', press);
  el.addEventListener('pointerup', release); el.addEventListener('pointercancel', release);
  els.keys.set(k, el);
}

function padVelocity(e, el) {
  // like the unit: the upper part of a pad is harder
  const r = el.getBoundingClientRect(), y = (e.clientY - r.top) / r.height;
  return Math.max(20, Math.min(127, Math.round(127 - Math.max(0, y - 0.25) * 140)));
}
function bindPad(el, i) {
  el.addEventListener('pointerdown', (e) => { e.preventDefault(); el.setPointerCapture?.(e.pointerId); el.classList.add('down'); schedAt = 0; core.mos_pad(i, padVelocity(e, el)); });
  const up = () => { if (!el.classList.contains('down')) return; el.classList.remove('down'); schedAt = 0; core.mos_pad(i, 0); };
  el.addEventListener('pointerup', up); el.addEventListener('pointercancel', up);
  el.addEventListener('dragover', (e) => { e.preventDefault(); el.classList.add('drop'); });
  el.addEventListener('dragleave', () => el.classList.remove('drop'));
  el.addEventListener('drop', async (e) => {
    e.preventDefault(); el.classList.remove('drop');
    const f = e.dataTransfer.files[0]; if (!f || !eng) return;
    try {
      const buf = await eng.ctx.decodeAudioData(await f.arrayBuffer());
      const bank = currentBank();
      setSample(bank * 16 + i, f.name.replace(/\.[^.]+$/, ''), buf);
      note(`Loaded ${f.name} to pad ${'ABCDEFGHIJ'[bank]}${String(i + 1).padStart(2, '0')}`);
    } catch (err) { note('Could not decode that file.'); }
  });
  els.pads.push(el);
}
let bankShown = 0;
const currentBank = () => bankShown;

function bindKnob(el, idx) {
  let v = 512, startY = 0, startV = 0;
  const set = (nv) => { v = Math.max(0, Math.min(1023, nv)); el.style.setProperty('--rot', `${-135 + v / 1023 * 270}deg`); if (idx >= 0) core.mos_ctrl(idx, Math.round(v)); else if (eng) eng.master.gain.value = v / 1023; };
  el.addEventListener('pointerdown', (e) => { e.preventDefault(); el.setPointerCapture(e.pointerId); startY = e.clientY; startV = v; el.classList.add('down'); });
  el.addEventListener('pointermove', (e) => { if (el.classList.contains('down')) set(startV + (startY - e.clientY) * 4); });
  el.addEventListener('pointerup', () => el.classList.remove('down'));
  el.addEventListener('wheel', (e) => { e.preventDefault(); set(v - Math.sign(e.deltaY) * 32); }, { passive: false });
  set(idx >= 0 ? 512 : 820);
}

function bindEncoder(el) {
  let acc = 0;
  el.addEventListener('wheel', (e) => {
    e.preventDefault(); acc += e.deltaY;
    while (Math.abs(acc) >= 40) { core.mos_encoder(acc > 0 ? 1 : -1); acc -= Math.sign(acc) * 40; spin(el, acc > 0 ? 1 : -1); }
  }, { passive: false });
  let dragY = null, moved = false;
  el.addEventListener('pointerdown', (e) => { e.preventDefault(); el.setPointerCapture(e.pointerId); dragY = e.clientY; moved = false; el.classList.add('down'); key(K.VALUE_PUSH, 1); });
  el.addEventListener('pointermove', (e) => {
    if (dragY === null) return;
    const d = dragY - e.clientY;
    if (Math.abs(d) >= 14) { core.mos_encoder(d > 0 ? 1 : -1); spin(el, d > 0 ? 1 : -1); dragY = e.clientY; moved = true; }
  });
  el.addEventListener('pointerup', () => { dragY = null; el.classList.remove('down'); key(K.VALUE_PUSH, 0); });
}
let encRot = 0;
function spin(el, d) { encRot += d * 18; el.style.setProperty('--rot', `${encRot}deg`); }

const held = new Set();
function keyboard() {
  window.addEventListener('keydown', (e) => {
    if (e.metaKey || e.ctrlKey || e.altKey) return;
    const k = e.key.length === 1 ? e.key.toLowerCase() : e.key;
    if (held.has(k)) { e.preventDefault(); return; }
    const pi = PAD_KEYS.indexOf(k);
    if (k === 'ArrowLeft' || k === 'ArrowRight') { core.mos_encoder(k === 'ArrowLeft' ? -1 : 1); e.preventDefault(); return; }
    if (k === 'ArrowUp' || k === 'ArrowDown') {      // cursor = push + turn
      core.mos_key(K.VALUE_PUSH, 1); core.mos_encoder(k === 'ArrowUp' ? -1 : 1); core.mos_key(K.VALUE_PUSH, 0);
      e.preventDefault(); return;
    }
    if (pi < 0 && !(k in KEYMAP)) return;
    e.preventDefault(); held.add(k); powerOn();
    if (pi >= 0) { els.pads[pi].classList.add('down'); schedAt = 0; core.mos_pad(pi, e.repeat ? 0 : 110); }
    else { els.keys.get(KEYMAP[k])?.classList.add('down'); key(KEYMAP[k], 1); }
  });
  window.addEventListener('keyup', (e) => {
    const k = e.key.length === 1 ? e.key.toLowerCase() : e.key;
    if (!held.delete(k)) { if (k === 'Shift') key(K.SHIFT, 0); return; }
    const pi = PAD_KEYS.indexOf(k);
    if (pi >= 0) { els.pads[pi].classList.remove('down'); schedAt = 0; core.mos_pad(pi, 0); }
    else { els.keys.get(KEYMAP[k])?.classList.remove('down'); key(KEYMAP[k], 0); }
  });
}

// ---- output -------------------------------------------------------------
const OLED_S = 4;
function drawOled(ctx, fb) {
  const img = ctx.createImageData(128 * OLED_S, 64 * OLED_S), d = img.data;
  for (let y = 0; y < 64; y++) for (let x = 0; x < 128; x++) {
    const on = (fb[(y >> 3) * 128 + x] >> (y & 7)) & 1;
    for (let j = 0; j < OLED_S; j++) for (let i = 0; i < OLED_S; i++) {
      const o = (((y * OLED_S + j) * 128 * OLED_S) + x * OLED_S + i) * 4;
      const edge = i === OLED_S - 1 || j === OLED_S - 1;
      if (on) { d[o] = edge ? 120 : 214; d[o + 1] = edge ? 165 : 246; d[o + 2] = edge ? 180 : 255; }
      else { d[o] = 9; d[o + 1] = 11; d[o + 2] = 13; }
      d[o + 3] = 255;
    }
  }
  ctx.putImageData(img, 0, 0);
}

function drawLeds() {
  const p = core.mos_leds(), m = mem(), L = m.slice(p, p + core.sim_leds_size());
  for (const [k, el] of els.keys) el.style.setProperty('--led', (L[k] / 255).toFixed(3));
  for (let i = 0; i < 16; i++) {
    const o = 0x32 + i * 3, r = L[o], g = L[o + 1], b = L[o + 2];
    els.pads[i].style.setProperty('--rgb', `${r},${g},${b}`);
    els.pads[i].style.setProperty('--glow', (Math.max(r, g, b) / 255).toFixed(3));
  }
  bankShown = core.mos_bank();
  $('#bankname').textContent = 'BANK ' + 'ABCDEFGHIJ'[bankShown];
}

// The core runs on 1 ms steps; notes it emits during a step are scheduled
// at that step's audio time plus a small lookahead, so timing stays tight.
const LOOKAHEAD = 0.03;
let lastT = 0, lastPerf = performance.now();
function pump() {
  if (eng) {
    const now = eng.ctx.currentTime;
    if (!lastT) lastT = now;
    let steps = Math.min(250, Math.floor((now - lastT) * 1000));
    for (let i = 0; i < steps; i++) { schedAt = lastT + LOOKAHEAD; core.mos_tick(1000); lastT += 0.001; }
    schedAt = 0;
  } else {
    const t = performance.now(), steps = Math.min(250, Math.floor(t - lastPerf));
    for (let i = 0; i < steps; i++) core.mos_tick(1000);
    lastPerf += steps;
  }
}

function frame(ctx) {
  drawOled(ctx, mem().slice(core.mos_render(), core.mos_render() + 1024));
  drawLeds();
  requestAnimationFrame(() => frame(ctx));
}

let powered = false;
async function powerOn() {
  if (powered) return; powered = true;
  eng = new Engine();
  await eng.ctx.resume();
  eng.kit().forEach(([name, buf], i) => setSample(i, name, buf));
  $('#power').classList.add('gone');
  note('Tap SHIFT for the MODE grid. Space = PLAY/STOP. Pads: 1-4 Q-R A-F Z-V.');
}

// ---- build the panel ----------------------------------------------------
function button(k, label, role, cls = '') {
  const el = document.createElement('button');
  el.className = 'key ' + cls;
  el.innerHTML = `<span class="led"></span><span class="lab">${label}</span>${role ? `<span class="role${role === 'stock' || role === 'pad attr' ? ' dim' : ''}">${role}</span>` : ''}`;
  bindButton(el, k);
  return el;
}

export async function start() {
  await loadCore();
  const fx = $('#fx'); FX_KEYS.forEach(([k, l, r]) => fx.append(button(k, l, r, 'fxk')));
  const fn = $('#fn'); FN_KEYS.forEach(([k, l, r]) => fn.append(button(k, l, r)));
  const side = $('#side'); SIDE_KEYS.forEach(([k, l, r]) => side.append(button(k, l, r)));
  const banks = $('#banks'); BANK_KEYS.forEach(([k, l]) => banks.append(button(k, l, '', 'bank')));
  banks.append(button(K.SHIFT, 'SHIFT', 'MODE', 'shift'));
  const pads = $('#pads');
  for (let i = 0; i < 16; i++) {
    const el = document.createElement('button'); el.className = 'pad';
    el.innerHTML = `<span>${i + 1}</span><kbd>${PAD_KEYS[i].toUpperCase()}</kbd>`;
    bindPad(el, i); pads.append(el);
  }
  document.querySelectorAll('.knob').forEach((el) => bindKnob(el, +el.dataset.ctrl));
  bindEncoder($('#value'));
  keyboard();
  const cv = $('#oled'); cv.width = 128 * OLED_S; cv.height = 64 * OLED_S;
  const ctx = cv.getContext('2d');
  $('#power').addEventListener('click', powerOn);
  document.addEventListener('pointerdown', powerOn, { once: true });
  setInterval(pump, 4);
  frame(ctx);
}
