// Render every Mosura screen to docs/img/*.png with a small demo project.
import { boot, K, fakePeaks } from './simhost.mjs';
import { writeFileSync, mkdirSync } from 'node:fs';

const out = new URL('../docs/img/', import.meta.url);
mkdirSync(out, { recursive: true });
const h = await boot(); h.init(); h.ms(1200);

const kit = [['KICK', 'kick'], ['SNARE', 'snare'], ['CL HAT', 'hat'], ['OP HAT', 'hat'], ['CLAP', 'snare'],
  ['RIM', 'hat'], ['TOM LO', 'kick'], ['TOM HI', 'kick'], ['SHAKER', 'hat'], ['PERC', 'hat'],
  ['SUB', 'pad'], ['STAB', 'pad'], ['VOX', 'pad'], ['KEYS', 'pad'], ['BREAK', 'loop'], ['FX', 'pad']];
kit.forEach(([n, k], i) => h.sample(i, n, fakePeaks(k, i + 3), 48000));

// a beat, entered on the GRID like a user would
h.mode(2);
const beat = { 0: [0, 6, 8, 11], 1: [4, 12], 2: [0, 2, 4, 6, 8, 10, 12, 14], 3: [7, 15] };
for (const [row, steps] of Object.entries(beat)) {
  for (const s of steps) { h.x.mos_ctrl(0, s % 4 === 0 ? 1023 : 500); h.pad(s, 100, 5); }
  h.key(K.FX2);                                       // ROW down
}
for (let i = 0; i < 4; i++) h.key(K.FX1);
h.cursor(0);
const shot = (name) => { h.ms(260); writeFileSync(new URL(name + '.png', out), h.png(h.frame(), 4)); console.log('wrote', name); };

h.key(K.EXIT);
h.key(K.PATTERN_SELECT); h.ms(1900);
h.turn(0); shot('main');
shot('main');
h.mode(2); h.pad(10); h.pad(10); h.key(K.FX2); shot('grid');
h.mode(1); shot('step');
h.key(K.PATTERN_SELECT);
h.mode(5); h.pad(2); h.x.mos_ctrl(0, 0); shot('mixer');
h.mode(4); h.pad(11); shot('program');
h.mode(6); h.pad(14); shot('trim');
h.key(K.FX5); shot('chop');
h.key(K.EXIT);
h.mode(0); h.cursor(4); h.tap(); shot('window-tc'); h.key(K.EXIT);
h.mode(8); shot('nextseq');
h.mode(9); h.pad(2); h.pad(12); shot('trackmute');
h.mode(10); h.pad(0); shot('16levels');
h.mode(11); h.x.mos_pad(3, 100); h.ms(100); shot('repeat'); h.x.mos_pad(3, 0);
h.mode(3); shot('song');
h.mode(7); shot('setup');
h.mode(14); shot('help');
h.x.mos_key(K.SHIFT, 1); h.x.mos_key(K.SHIFT, 0); h.ms(10); shot('modes');
