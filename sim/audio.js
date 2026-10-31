// Mosura simulator: sound. A synthesized 16-pad kit, sample playback with
// start/end/tune/reverse/loop/gate, and stand-ins for three of the effects.
// None of this runs on the unit; there the stock engine makes the sound.

export class Engine {
  constructor() {
    this.ctx = new (window.AudioContext || window.webkitAudioContext)({ latencyHint: 'interactive' });
    const c = this.ctx;
    this.buf = new Map();          // note -> {buf, rev, name}
    this.voices = new Map();       // note -> [{src, gain}]
    this.pad = new Map();          // note -> {level, pan, tune, decay, flags}
    this.input = c.createGain();
    // FX chain: filter+drive -> isolator -> delay send -> master
    this.filter = c.createBiquadFilter(); this.filter.type = 'lowpass'; this.filter.frequency.value = 20000;
    this.drive = c.createWaveShaper(); this.setDrive(0);
    this.iso = [c.createBiquadFilter(), c.createBiquadFilter(), c.createBiquadFilter()];
    this.iso[0].type = 'lowshelf'; this.iso[0].frequency.value = 250;
    this.iso[1].type = 'peaking'; this.iso[1].frequency.value = 1200; this.iso[1].Q.value = 0.7;
    this.iso[2].type = 'highshelf'; this.iso[2].frequency.value = 4000;
    this.delay = c.createDelay(2); this.fb = c.createGain(); this.wet = c.createGain();
    this.delay.delayTime.value = 0.333; this.fb.gain.value = 0.35; this.wet.gain.value = 0;
    this.master = c.createGain(); this.master.gain.value = 0.8;
    this.comp = c.createDynamicsCompressor(); this.comp.threshold.value = -8; this.comp.ratio.value = 4;
    this.input.connect(this.filter); this.filter.connect(this.drive); this.drive.connect(this.iso[0]);
    this.iso[0].connect(this.iso[1]); this.iso[1].connect(this.iso[2]);
    this.iso[2].connect(this.master);
    this.iso[2].connect(this.delay); this.delay.connect(this.fb); this.fb.connect(this.delay);
    this.delay.connect(this.wet); this.wet.connect(this.master);
    this.master.connect(this.comp); this.comp.connect(c.destination);
    this.analyser = c.createAnalyser(); this.analyser.fftSize = 512; this.comp.connect(this.analyser);
  }

  setDrive(amt) {
    const n = 1024, k = amt * 40, curve = new Float32Array(n);
    for (let i = 0; i < n; i++) { const x = i * 2 / n - 1; curve[i] = k ? (1 + k) * x / (1 + k * Math.abs(x)) : x; }
    this.drive.curve = curve;
  }

  fx(i, on, p1, p2, p3) {
    const t = this.ctx.currentTime, a = p1 / 1023, b = p2 / 1023, c = p3 / 1023;
    if (i === 0) {             // FILTER+DRIVE: cutoff, resonance, drive
      this.filter.frequency.setTargetAtTime(on ? 80 * Math.pow(250, a) : 20000, t, 0.02);
      this.filter.Q.setTargetAtTime(on ? 0.5 + b * 18 : 0.7, t, 0.02);
      this.setDrive(on ? c : 0);
    } else if (i === 2) {      // DELAY: time, feedback, level
      this.delay.delayTime.setTargetAtTime(0.05 + a * 0.9, t, 0.05);
      this.fb.gain.setTargetAtTime(b * 0.85, t, 0.02);
      this.wet.gain.setTargetAtTime(on ? 0.15 + c * 0.85 : 0, t, 0.02);
    } else if (i === 3) {      // ISOLATOR: low, mid, high
      const g = (v) => (on ? (v < 0.5 ? -40 + v * 80 : (v - 0.5) * 12) : 0);
      this.iso[0].gain.setTargetAtTime(g(a), t, 0.02);
      this.iso[1].gain.setTargetAtTime(g(b), t, 0.02);
      this.iso[2].gain.setTargetAtTime(g(c), t, 0.02);
    }
  }

  padParams(note) {
    if (!this.pad.has(note)) this.pad.set(note, { level: 100, pan: 50, tune: 0, decay: 100, flags: 0 });
    return this.pad.get(note);
  }

  noteOn(note, vel, tune, startPm, endPm, when) {
    const s = this.buf.get(note); if (!s) return;
    const p = this.padParams(note), c = this.ctx, t = Math.max(when || 0, c.currentTime);
    const rev = p.flags & 4, buf = rev ? s.rev : s.buf;
    this.noteOff(note, t, 0.004, true);
    const src = c.createBufferSource(); src.buffer = buf;
    src.playbackRate.value = Math.pow(2, tune / 12);
    const g = c.createGain(), pan = c.createStereoPanner ? c.createStereoPanner() : null;
    const amp = Math.pow(vel / 127, 1.6) * (p.level / 100);
    g.gain.setValueAtTime(amp, t);
    if (p.decay < 100) g.gain.setTargetAtTime(0, t + 0.01, 0.02 + p.decay / 100 * 0.6);
    if (pan) { pan.pan.value = (p.pan - 50) / 50; src.connect(g); g.connect(pan); pan.connect(this.input); }
    else { src.connect(g); g.connect(this.input); }
    let a = startPm / 1000, z = endPm / 1000;
    if (rev) [a, z] = [1 - z, 1 - a];
    const off = a * buf.duration, dur = Math.max(0.005, (z - a) * buf.duration);
    if (p.flags & 2) { src.loop = true; src.loopStart = off; src.loopEnd = off + dur; src.start(t, off); }
    else src.start(t, off, dur);
    const list = this.voices.get(note) || []; list.push({ src, g }); this.voices.set(note, list);
    src.onended = () => { const l = this.voices.get(note); if (l) this.voices.set(note, l.filter((v) => v.src !== src)); };
  }

  noteOff(note, when, rel = 0.02, force = false) {
    const p = this.pad.get(note), l = this.voices.get(note);
    if (!l || !l.length) return;
    const gated = p && (p.flags & 3);
    if (!force && !gated) return;                      // one-shots ring out
    const t = Math.max(when || 0, this.ctx.currentTime);
    for (const v of l) { v.g.gain.setTargetAtTime(0, t, rel); try { v.src.stop(t + rel * 6); } catch (e) {} }
    this.voices.set(note, []);
  }

  click(accent, when) {
    const c = this.ctx, t = Math.max(when || 0, c.currentTime), o = c.createOscillator(), g = c.createGain();
    o.frequency.value = accent ? 1800 : 1200; g.gain.setValueAtTime(0.25, t);
    g.gain.exponentialRampToValueAtTime(0.001, t + 0.04);
    o.connect(g); g.connect(this.master); o.start(t); o.stop(t + 0.05);
  }

  set(note, name, buf) {
    const rev = this.ctx.createBuffer(buf.numberOfChannels, buf.length, buf.sampleRate);
    for (let ch = 0; ch < buf.numberOfChannels; ch++) rev.getChannelData(ch).set(buf.getChannelData(ch).slice().reverse());
    this.buf.set(note, { buf, rev, name });
  }

  chop(dst, src, a, z) {
    const s = this.buf.get(src); if (!s) return;
    const b = s.buf, i0 = Math.floor(a / 1000 * b.length), i1 = Math.floor(z / 1000 * b.length);
    const out = this.ctx.createBuffer(b.numberOfChannels, Math.max(1, i1 - i0), b.sampleRate);
    for (let ch = 0; ch < b.numberOfChannels; ch++) out.getChannelData(ch).set(b.getChannelData(ch).subarray(i0, i1));
    this.set(dst, s.name, out);
  }

  static peaks(buf) {
    const d = buf.getChannelData(0), p = new Uint8Array(96), n = d.length / 96;
    let max = 1e-6;
    const raw = [];
    for (let i = 0; i < 96; i++) { let m = 0; for (let j = Math.floor(i * n); j < Math.floor((i + 1) * n); j++) m = Math.max(m, Math.abs(d[j])); raw.push(m); max = Math.max(max, m); }
    raw.forEach((m, i) => { p[i] = Math.round(255 * Math.min(1, Math.sqrt(m / max))); });
    return p;
  }

  // ---- the synthesized kit ---------------------------------------------
  kit() {
    const sr = this.ctx.sampleRate;
    const mk = (secs, fn) => {
      const n = Math.floor(secs * sr), b = this.ctx.createBuffer(1, n, sr), d = b.getChannelData(0);
      let seed = 7; const rnd = () => ((seed = (seed * 16807) % 2147483647) / 1073741823.5 - 1);
      fn(d, n, rnd); return b;
    };
    const env = (t, a, dcy) => (t < a ? t / a : Math.exp(-(t - a) / dcy));
    const hp = (d, k = 0.92) => { let px = 0, py = 0; for (let i = 0; i < d.length; i++) { const y = k * (py + d[i] - px); px = d[i]; py = y; d[i] = y; } };
    const lp = (d, k) => { let y = 0; for (let i = 0; i < d.length; i++) { y += k * (d[i] - y); d[i] = y; } };
    const sounds = [
      ['KICK', mk(0.6, (d, n) => { let ph = 0; for (let i = 0; i < n; i++) { const t = i / sr; ph += 2 * Math.PI * (48 + 130 * Math.exp(-t * 28)) / sr; d[i] = Math.tanh(1.6 * Math.sin(ph) * env(t, 0.002, 0.22)); } })],
      ['SNARE', mk(0.4, (d, n, r) => { let ph = 0; for (let i = 0; i < n; i++) { const t = i / sr; ph += 2 * Math.PI * 185 / sr; d[i] = 0.5 * Math.sin(ph) * env(t, 0.001, 0.05) + 0.7 * r() * env(t, 0.001, 0.12); } hp(d, 0.97); })],
      ['CL HAT', mk(0.12, (d, n, r) => { for (let i = 0; i < n; i++) d[i] = r() * env(i / sr, 0.0005, 0.025); hp(d, 0.6); })],
      ['OP HAT', mk(0.5, (d, n, r) => { for (let i = 0; i < n; i++) d[i] = r() * env(i / sr, 0.001, 0.18) * 0.8; hp(d, 0.6); })],
      ['CLAP', mk(0.4, (d, n, r) => { for (let i = 0; i < n; i++) { const t = i / sr, b = t < 0.03 ? (Math.floor(t / 0.01) % 2 ? 0.4 : 1) : 1; d[i] = r() * b * env(t, 0.001, 0.09); } hp(d, 0.9); lp(d, 0.5); })],
      ['RIM', mk(0.1, (d, n) => { for (let i = 0; i < n; i++) { const t = i / sr; d[i] = (Math.sin(2 * Math.PI * 1700 * t) + 0.6 * Math.sin(2 * Math.PI * 820 * t)) * env(t, 0.0005, 0.012); } })],
      ['TOM LO', mk(0.5, (d, n) => { let ph = 0; for (let i = 0; i < n; i++) { const t = i / sr; ph += 2 * Math.PI * (90 + 60 * Math.exp(-t * 20)) / sr; d[i] = Math.sin(ph) * env(t, 0.002, 0.18); } })],
      ['TOM HI', mk(0.4, (d, n) => { let ph = 0; for (let i = 0; i < n; i++) { const t = i / sr; ph += 2 * Math.PI * (150 + 80 * Math.exp(-t * 20)) / sr; d[i] = Math.sin(ph) * env(t, 0.002, 0.14); } })],
      ['SHAKER', mk(0.18, (d, n, r) => { for (let i = 0; i < n; i++) { const t = i / sr; d[i] = r() * (t < 0.04 ? t / 0.04 : Math.exp(-(t - 0.04) / 0.03)) * 0.7; } hp(d, 0.5); })],
      ['COWBELL', mk(0.35, (d, n) => { for (let i = 0; i < n; i++) { const t = i / sr; d[i] = 0.5 * (Math.sign(Math.sin(2 * Math.PI * 560 * t)) + Math.sign(Math.sin(2 * Math.PI * 845 * t))) * env(t, 0.001, 0.08) * 0.5; } lp(d, 0.3); })],
      ['SUB 808', mk(1.4, (d, n) => { let ph = 0; for (let i = 0; i < n; i++) { const t = i / sr; ph += 2 * Math.PI * (55 + 40 * Math.exp(-t * 30)) / sr; d[i] = Math.tanh(1.3 * Math.sin(ph)) * env(t, 0.003, 0.6); } })],
      ['STAB', mk(0.9, (d, n) => { const f = [261.6, 311.1, 392, 466.2]; for (let i = 0; i < n; i++) { const t = i / sr; let v = 0; for (const x of f) v += ((t * x) % 1) * 2 - 1; d[i] = v / 4 * env(t, 0.004, 0.25); } lp(d, 0.18); })],
      ['VOX', mk(0.9, (d, n, r) => { let ph = 0; for (let i = 0; i < n; i++) { const t = i / sr; ph += 2 * Math.PI * (220 * (1 + 0.01 * Math.sin(t * 30))) / sr; const s = Math.sin(ph) + 0.5 * Math.sin(2 * ph) + 0.3 * Math.sin(3 * ph) + 0.25 * Math.sin(5 * ph); d[i] = s * 0.35 * Math.min(1, t * 20) * Math.exp(-t * 2.2); } lp(d, 0.35); })],
      ['KEYS', mk(1.2, (d, n) => { const f = [349.2, 440, 523.3]; for (let i = 0; i < n; i++) { const t = i / sr; let v = 0; for (const x of f) v += Math.sin(2 * Math.PI * x * t + 1.2 * Math.sin(2 * Math.PI * x * 2 * t) * Math.exp(-t * 6)); d[i] = v / 3 * env(t, 0.002, 0.5); } })],
      null,   // BREAK: made from the kit below
      ['RISER', mk(1.6, (d, n, r) => { let ph = 0; for (let i = 0; i < n; i++) { const t = i / sr; ph += 2 * Math.PI * (200 * Math.pow(8, t / 1.6)) / sr; d[i] = (0.3 * Math.sin(ph) + 0.3 * r() * t / 1.6) * Math.min(1, t * 2) * (t > 1.5 ? (1.6 - t) * 10 : 1); } })],
    ];
    // a two-bar break at 90 BPM, rendered from the kit, for CHOP to cut up
    const spb = 60 / 90, steps = 32, len = Math.floor(spb * 8 * sr);
    const brk = this.ctx.createBuffer(1, len, sr), bd = brk.getChannelData(0);
    const put = (b, at, g) => { const s = b.getChannelData(0), o = Math.floor(at * sr); for (let i = 0; i < s.length && o + i < len; i++) bd[o + i] += s[i] * g; };
    const pat = { 0: [0, 10, 16, 19, 26], 1: [4, 12, 20, 28, 31], 2: [...Array(32).keys()].filter((x) => x % 2 === 0), 3: [14, 30], 9: [7, 23] };
    for (const [k, list] of Object.entries(pat)) for (const st of list) put(sounds[k][1], st * spb / 4 + (st % 2 ? spb * 0.04 : 0), k == 2 ? (st % 4 ? 0.35 : 0.6) : 0.8);
    for (let i = 0; i < len; i++) bd[i] = Math.tanh(bd[i] * 1.1);
    sounds[14] = ['BREAK 90', brk];
    void steps;
    return sounds;
  }
}
