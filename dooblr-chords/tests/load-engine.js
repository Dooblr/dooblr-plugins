// Loads the ES5 engine into a fresh VM context, in manifest order, with the
// post-ES5 built-ins removed so an accidental ES2015+ API call fails in tests
// the same way it would in Max's classic `js` object.
'use strict';
const fs = require('fs');
const path = require('path');
const vm = require('vm');

const ENGINE_DIR = path.join(__dirname, '..', 'src', 'engine');

const STRIP = `
(function () {
  var g = this;
  var drop = function (o, names) { for (var i = 0; i < names.length; i++) { try { delete o[names[i]]; } catch (e) {} } };
  drop(Array.prototype, ['includes', 'find', 'findIndex', 'fill', 'copyWithin', 'keys', 'values', 'entries', 'flat', 'flatMap', 'at']);
  drop(Array, ['from', 'of']);
  drop(Object, ['assign', 'entries', 'values', 'fromEntries', 'is', 'setPrototypeOf', 'getOwnPropertySymbols']);
  drop(String.prototype, ['includes', 'startsWith', 'endsWith', 'repeat', 'padStart', 'padEnd', 'codePointAt', 'normalize', 'trimStart', 'trimEnd', 'at', 'replaceAll']);
  drop(String, ['fromCodePoint', 'raw']);
  drop(Math, ['imul', 'sign', 'trunc', 'log2', 'log10', 'hypot', 'clz32', 'fround', 'cbrt', 'expm1', 'log1p', 'sinh', 'cosh', 'tanh']);
  drop(Number, ['isInteger', 'isFinite', 'isNaN', 'isSafeInteger', 'parseFloat', 'parseInt', 'EPSILON']);
  drop(g, ['Map', 'Set', 'WeakMap', 'WeakSet', 'Promise', 'Reflect', 'Proxy']);
}).call(this);
`;

function manifest() {
  return JSON.parse(fs.readFileSync(path.join(ENGINE_DIR, 'manifest.json'), 'utf8'));
}

function loadEngine() {
  const ctx = {};
  vm.createContext(ctx);
  vm.runInContext(STRIP, ctx, { filename: 'strip-es2015.js' });
  for (const f of manifest()) {
    vm.runInContext(fs.readFileSync(path.join(ENGINE_DIR, f), 'utf8'), ctx, { filename: f });
  }
  const DC = vm.runInContext('DC', ctx);
  // Values coming out of the VM have the VM's prototypes; round-trip through
  // JSON when comparing structurally with deepStrictEqual.
  DC.plain = function (x) { return JSON.parse(JSON.stringify(x)); };
  return DC;
}

module.exports = { loadEngine, manifest, ENGINE_DIR, STRIP };
