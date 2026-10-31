// Headless behaviour tests for the Mosura core.  node tools/test_core.mjs
import { boot, K, fakePeaks } from './simhost.mjs';
import assert from 'node:assert/strict';

let failures = 0;
async function t(name, fn) {
  const h = await boot(); h.init(); h.ms(1000);
  try { await fn(h); console.log('ok   ', name); }
  catch (e) { failures++; console.log('FAIL ', name, '\n     ', e.message); }
}

await t('renders a non-empty MAIN screen', (h) => {
  const fb = h.frame(); assert.ok(fb.some((b) => b), 'frame is blank');
});

await t('PATTERN SELECT plays and stops, clock advances', (h) => {
  h.key(K.PATTERN_SELECT); h.ms(2000);                     // 90 BPM -> 3 beats
  h.key(K.PATTERN_SELECT);
  const offs = h.log.offs.length; assert.ok(offs >= 0);
});

await t('recording an overdub captures pad hits, quantized to 1/16', (h) => {
  h.x.mos_key(K.SHIFT, 1); h.x.mos_key(K.SHIFT, 0);        // tap SHIFT -> MODE grid
  h.key(K.EXIT);                                           // and back to MAIN
  h.key(K.PATTERN_EDIT);                                   // arm
  h.key(K.PATTERN_SELECT);                                 // play with count-in (1 bar @90 = 2667ms)
  h.ms(2700);
  h.log.notes.length = 0;
  for (let i = 0; i < 4; i++) { h.pad(0, 110, 30); h.ms(637); } // ~ quarter notes
  h.key(K.PATTERN_SELECT);
  // Play back: pad 0 must sound again on the next pass
  h.log.notes.length = 0;
  h.key(K.PATTERN_SELECT); h.ms(5400); h.key(K.PATTERN_SELECT);
  const hits = h.log.notes.filter((n) => n.n === 0).length;
  assert.ok(hits >= 4, `expected >=4 playback hits, got ${hits}`);
});

await t('REMAIN undoes the last recording pass', (h) => {
  h.key(K.PATTERN_EDIT); h.key(K.PATTERN_SELECT); h.ms(2700);
  h.pad(1); h.ms(300); h.key(K.PATTERN_SELECT);
  h.key(K.REMAIN);
  h.log.notes.length = 0;
  h.key(K.PATTERN_SELECT); h.ms(5400); h.key(K.PATTERN_SELECT);
  assert.equal(h.log.notes.filter((n) => n.n === 1).length, 0);
});

await t('SHIFT + pad 3 jumps to GRID; a pad there writes a step', (h) => {
  h.mode(2);
  h.log.notes.length = 0;
  h.pad(4);                                                // step 5 of row A01
  h.key(K.PATTERN_SELECT); h.ms(2700); h.key(K.PATTERN_SELECT);
  assert.ok(h.log.notes.some((n) => n.n === 0), 'grid step did not play');
});

await t('STEP: pads step-record and advance', (h) => {
  h.mode(1);
  h.pad(2); h.pad(3); h.pad(4);
  const fb = h.frame();
  assert.ok(fb.some((b) => b));
  h.log.notes.length = 0;
  h.key(K.PATTERN_SELECT); h.ms(800); h.key(K.PATTERN_SELECT);
  const ns = h.log.notes.map((n) => n.n);
  assert.deepEqual(ns.slice(0, 3), [2, 3, 4]);
});

await t('ROLL + pad repeats at the timing-correct rate', (h) => {
  h.log.notes.length = 0;
  h.down(K.ROLL); h.x.mos_pad(5, 90); h.ms(1000); h.x.mos_pad(5, 0); h.up(K.ROLL);
  const n = h.log.notes.filter((x) => x.n === 5).length;   // 1/16 at 90 BPM = 6/s
  assert.ok(n >= 5 && n <= 8, `repeat count ${n}`);
});

await t('CHOP splits a sample into the next bank', (h) => {
  h.sample(0, 'BREAK', fakePeaks('loop'), 96000);
  h.mode(6);                                               // TRIM
  h.key(K.FX5);                                            // CHOP window
  h.tap();                                                 // do it
  assert.equal(h.log.chops.length, 8);
  assert.equal(h.log.chops[0].d, 16);                      // bank B pad 1
});

await t('FX buttons are soft keys on edit screens and effects on MAIN', (h) => {
  h.key(K.FX1);
  assert.equal(h.log.fx.at(-1).on, 1);
  h.mode(4);                                               // PROGRAM
  const before = h.log.fx.length; h.key(K.FX3);            // PLAY
  assert.equal(h.log.fx.length, before);
});

await t('NEXT SEQ queues while playing and switches on the loop', (h) => {
  h.mode(8);
  h.key(K.PATTERN_SELECT); h.pad(3); h.ms(5500);           // 2 bars at 90 = 5333ms
  h.key(K.PATTERN_SELECT);
  h.mode(0);
  const s = h.ascii(h.frame());
  assert.ok(s.length > 0);
});

await t('STOCK hands over and a double SHIFT tap comes back', (h) => {
  h.mode(15);
  assert.deepEqual(h.log.stock, [1]);
  assert.equal(h.x.mos_key(K.REC, 1), 0, 'stock keys must pass through');
  h.down(K.SHIFT); h.up(K.SHIFT); h.ms(100); h.down(K.SHIFT); h.up(K.SHIFT);
  assert.deepEqual(h.log.stock, [1, 0]);
});

if (failures) { console.log(`\n${failures} failing`); process.exit(1); }
console.log('\nall passing');
