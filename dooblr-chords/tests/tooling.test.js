// Tests for the build tooling: amxd container, patcher validator, ES5 scanner.
'use strict';
const test = require('node:test');
const assert = require('node:assert');
const fs = require('fs');
const path = require('path');
const amxd = require('../scripts/amxd');
const { validate } = require('../scripts/validate-patcher');
const { scan } = require('../scripts/check-es5');

const ROOT = path.join(__dirname, '..');
const SRC = JSON.parse(fs.readFileSync(path.join(ROOT, 'src', 'patcher', 'dooblr-chords.maxpat'), 'utf8'));
const MAIN = fs.readFileSync(path.join(ROOT, 'src', 'max', 'main.js'), 'utf8');
const clone = () => JSON.parse(JSON.stringify(SRC));

test('amxd: chunk layout and round trip', () => {
  const buf = amxd.write(SRC, { type: 'midi' });
  assert.strictEqual(buf.toString('latin1', 0, 4), 'ampf');
  assert.strictEqual(buf.readUInt32LE(4), 4);
  assert.strictEqual(buf.toString('latin1', 8, 12), 'mmmm');
  assert.strictEqual(buf.toString('latin1', 12, 16), 'meta');
  assert.strictEqual(buf.readUInt32LE(16), 4);
  assert.strictEqual(buf.readUInt32LE(20), amxd.META_VALUE);
  assert.strictEqual(buf.toString('latin1', 24, 28), 'ptch');
  assert.strictEqual(buf.readUInt32LE(28), buf.length - 32);
  assert.strictEqual(buf[buf.length - 1], 0);
  const back = amxd.read(buf);
  assert.strictEqual(back.type, 'mmmm');
  assert.deepStrictEqual(back.tags, ['ampf', 'meta', 'ptch']);
  assert.deepStrictEqual(back.patcher, SRC);
  assert.throws(() => amxd.read(buf.subarray(0, buf.length - 10)), /overruns/);
});

test('validator: the source patcher is clean', () => {
  assert.deepStrictEqual(validate(SRC, 'src', MAIN), []);
});

test('validator: catches duplicate ids, dangling lines and bad indices', () => {
  let p = clone();
  p.patcher.boxes[1].box.id = p.patcher.boxes[0].box.id;
  assert.ok(validate(p, 'x', MAIN).some((e) => /duplicate box id/.test(e)));

  p = clone();
  p.patcher.lines[0].patchline.destination[0] = 'obj-9999';
  assert.ok(validate(p, 'x', MAIN).some((e) => /does not exist/.test(e)));

  p = clone();
  p.patcher.lines[0].patchline.source[1] = 7;
  assert.ok(validate(p, 'x', MAIN).some((e) => /uses outlet 7/.test(e)));

  p = clone();
  p.patcher.lines[0].patchline.destination[1] = 5;
  assert.ok(validate(p, 'x', MAIN).some((e) => /uses inlet 5/.test(e)));

  p = clone();
  const width = p.patcher.devicewidth;
  const dial = p.patcher.boxes.find((w) => w.box.maxclass === 'live.dial').box;
  dial.presentation_rect = [width - 10, 150, 46, 58];
  assert.ok(validate(p, 'x', MAIN).some((e) => new RegExp('outside ' + width + 'x169').test(e)));

  p = clone();
  const mainDials = p.patcher.boxes.filter((w) => w.box.varname === 'color' || w.box.varname === 'chromatic').map((w) => w.box);
  mainDials[1].presentation_rect = mainDials[0].presentation_rect.slice();
  assert.ok(validate(p, 'x', MAIN).some((e) => /overlap in presentation/.test(e)));

  p = clone();
  const register = p.patcher.boxes.find((w) => w.box.varname === 'register').box;
  register.presentation_rect = [140, 84, 64, 72]; // on-screen, not parked
  assert.ok(validate(p, 'x', MAIN).some((e) => /not parked off-screen/.test(e)));

  p = clone();
  assert.ok(validate(p, 'x', MAIN.replace(/register: \[[^\]]+\]/, 'register: [700, 150, 46, 58]'))
    .some((e) => /outside 640x169/.test(e)));

  p = clone();
  assert.ok(validate(p, 'x', MAIN.replace(/spread: \[[^\]]+\]/, 'spread: [284, 84, 64, 72]'))
    .some((e) => /overlap inside the advanced panel/.test(e)));

  p = clone();
  assert.ok(validate(p, 'x', MAIN).every((e) => !/overlap/.test(e)), 'two mutually exclusive panels may occupy the same space');

  p = clone();
  const pre = p.patcher.boxes.find((w) => /^prepend param /.test(w.box.text || '')).box;
  pre.text = 'prepend param nonsense';
  assert.ok(validate(p, 'x', MAIN).some((e) => /does not handle parameter nonsense/.test(e)));

  p = clone();
  const msg = p.patcher.boxes.find((w) => w.box.maxclass === 'message' && w.box.text === 'generate').box;
  msg.text = 'explode';
  assert.ok(validate(p, 'x', MAIN).some((e) => /"explode" has no function/.test(e)));
});

test('ES5 scanner rejects ES2015+ syntax and accepts ES5', () => {
  ['let x = 1;', 'const y = 2;', 'var f = () => 1;', 'var s = `a`;', 'f(...a);', 'function g(a = 1) {}',
    'for (var k of a) {}', 'var {a} = b;', 'class A {}', 'x ** 2;', 'a?.b;', 'a ?? b;', 'f(a,);']
    .forEach((src) => assert.ok(scan(src).length > 0, 'should reject: ' + src));
  assert.deepStrictEqual(scan('var let_ = 1; var o = {let: 1, class: 2}; o.const = 3; var r = /a\\/b/g; var t = a ?.5 : 1; f(a, b);'), []);
});
