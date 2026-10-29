// ES5 check for every file that runs inside Max's classic [js] / [jsui].
//
// Primary: acorn with ecmaVersion 5 (npm install, or a system-wide acorn). If acorn is not installed
// the script falls back to a built-in scanner that rejects ES2015+ syntax
// tokens (let/const/class, arrow functions, template literals, spread,
// destructuring declarations, default parameters, for-of, **, ?. and ??,
// import/export, async/await, trailing commas in calls). Either way every
// file must also compile, and use no ES2015+ library APIs.
'use strict';
const fs = require('fs');
const path = require('path');
const vm = require('vm');

const ROOT = path.join(__dirname, '..');
const DIRS = ['src/engine', 'src/max', 'device/dooblr-chords'];

let acorn = null;
try { acorn = require('acorn'); } catch (e) { acorn = null; }

// ES2015+ library APIs that Max 8's js engine does not provide.
const FORBIDDEN_APIS = [
  /\bMath\.(imul|sign|trunc|log2|log10|hypot|cbrt|fround|clz32)\s*\(/,
  /\bNumber\.(isInteger|isNaN|isFinite|parseFloat|parseInt|EPSILON|MAX_SAFE_INTEGER)\b/,
  /\bObject\.(assign|entries|values|is|setPrototypeOf|getOwnPropertySymbols)\s*\(/,
  /\bArray\.(from|of)\s*\(/,
  /(?<!mgraphics)\.(includes|find|findIndex|fill|startsWith|endsWith|repeat|padStart|padEnd|flat|flatMap|copyWithin)\s*\(/,
  /\b(Symbol|Promise|Map|Set|WeakMap|WeakSet|Proxy|Reflect)\b\s*[.(]/,
  /\bnew\s+(Map|Set|WeakMap|WeakSet|Promise|Proxy)\b/
];

// ---- fallback scanner -----------------------------------------------------

const REGEX_PREV = /^(?:[=(,:[!&|?{};+\-*%<>~^]|return|typeof|instanceof|in|of|new|delete|void|throw|case|do|else)$/;

function tokenize(src) {
  const toks = [];
  let i = 0, line = 1;
  const push = (type, value) => toks.push({ type, value, line });
  while (i < src.length) {
    const c = src[i];
    if (c === '\n') { line++; i++; continue; }
    if (/\s/.test(c)) { i++; continue; }
    if (c === '/' && src[i + 1] === '/') { while (i < src.length && src[i] !== '\n') { i++; } continue; }
    if (c === '/' && src[i + 1] === '*') {
      const end = src.indexOf('*/', i + 2);
      const stop = end < 0 ? src.length : end + 2;
      for (let k = i; k < stop; k++) { if (src[k] === '\n') { line++; } }
      i = stop; continue;
    }
    if (c === '"' || c === "'") {
      let j = i + 1;
      while (j < src.length && src[j] !== c) { if (src[j] === '\\') { j++; } if (src[j] === '\n') { break; } j++; }
      push('str', src.slice(i, j + 1)); i = j + 1; continue;
    }
    if (c === '`') { push('punct', '`'); i++; continue; }
    if (/[A-Za-z_$]/.test(c)) {
      let j = i; while (j < src.length && /[\w$]/.test(src[j])) { j++; }
      push('name', src.slice(i, j)); i = j; continue;
    }
    if (/[0-9]/.test(c) || (c === '.' && /[0-9]/.test(src[i + 1]))) {
      let j = i; while (j < src.length && /[\w.]/.test(src[j])) { j++; }
      push('num', src.slice(i, j)); i = j; continue;
    }
    if (c === '/') {
      const prev = toks.length ? toks[toks.length - 1] : null;
      if (!prev || (prev.type !== 'num' && prev.type !== 'str' && REGEX_PREV.test(prev.value) && prev.value !== ')' && prev.value !== ']')) {
        let j = i + 1, inClass = false;
        while (j < src.length) {
          if (src[j] === '\\') { j += 2; continue; }
          if (src[j] === '[') { inClass = true; } else if (src[j] === ']') { inClass = false; }
          else if (src[j] === '/' && !inClass) { break; } else if (src[j] === '\n') { break; }
          j++;
        }
        j++; while (j < src.length && /[a-z]/.test(src[j])) { j++; }
        push('regex', src.slice(i, j)); i = j; continue;
      }
    }
    const three = src.substr(i, 3), two = src.substr(i, 2);
    if (three === '...' || three === '**=' || three === '===' || three === '!==' || three === '>>>' || three === '??=') { push('punct', three); i += 3; continue; }
    if (['=>', '**', '?.', '??', '==', '!=', '<=', '>=', '&&', '||', '++', '--', '+=', '-=', '*=', '/=', '%=', '<<', '>>', '&=', '|=', '^='].indexOf(two) >= 0) {
      if (two === '?.' && /[0-9]/.test(src[i + 2])) { push('punct', '?'); i++; continue; }
      push('punct', two); i += 2; continue;
    }
    push('punct', c); i++;
  }
  return toks;
}

function scan(src) {
  const problems = [];
  const toks = tokenize(src);
  const bad = (t, what) => problems.push('line ' + t.line + ': ' + what);
  for (let k = 0; k < toks.length; k++) {
    const t = toks[k], next = toks[k + 1] || {}, prev = toks[k - 1] || {};
    const isProp = prev.value === '.';
    if (t.type === 'punct') {
      if (t.value === '`') { bad(t, 'template literal'); }
      else if (t.value === '=>') { bad(t, 'arrow function'); }
      else if (t.value === '...') { bad(t, 'spread/rest'); }
      else if (t.value === '**' || t.value === '**=') { bad(t, 'exponent operator'); }
      else if (t.value === '?.') { bad(t, 'optional chaining'); }
      else if (t.value === '??' || t.value === '??=') { bad(t, 'nullish coalescing'); }
      else if (t.value === ',' && next.value === ')') { bad(t, 'trailing comma in a call or parameter list'); }
    } else if (t.type === 'name' && !isProp && next.value !== ':') {
      if (t.value === 'const' || t.value === 'class' || t.value === 'import' || t.value === 'export' || t.value === 'await' || t.value === 'yield') {
        bad(t, t.value);
      } else if (t.value === 'let' && (next.type === 'name' || next.value === '[' || next.value === '{')) {
        bad(t, 'let');
      } else if (t.value === 'async' && (next.value === 'function' || next.type === 'name' || next.value === '(')) {
        bad(t, 'async');
      } else if (t.value === 'var' && (next.value === '{' || next.value === '[')) {
        bad(t, 'destructuring declaration');
      } else if (t.value === 'for' && next.value === '(') {
        let j = k + 2; if (toks[j] && toks[j].value === 'var') { j++; }
        if (toks[j] && toks[j].type === 'name' && toks[j + 1] && toks[j + 1].value === 'of') { bad(t, 'for...of'); }
      } else if (t.value === 'function') {
        // Default parameters: an '=' at depth 1 inside the parameter list.
        let j = k + 1; if (toks[j] && toks[j].type === 'name') { j++; }
        if (toks[j] && toks[j].value === '(') {
          let depth = 0;
          for (; j < toks.length; j++) {
            if (toks[j].value === '(' ) { depth++; } else if (toks[j].value === ')') { depth--; if (depth === 0) { break; } }
            else if (toks[j].value === '=' && depth === 1) { bad(toks[j], 'default parameter'); break; }
          }
        }
      }
    }
  }
  return problems;
}

// ---- main -----------------------------------------------------------------

function filesIn(dir) {
  const abs = path.join(ROOT, dir);
  if (!fs.existsSync(abs)) { return []; }
  return fs.readdirSync(abs).filter((f) => /\.js$/.test(f)).map((f) => path.join(dir, f));
}

function checkFile(rel) {
  const src = fs.readFileSync(path.join(ROOT, rel), 'utf8');
  const problems = [];
  try { new vm.Script(src, { filename: rel }); } catch (e) { problems.push('does not compile: ' + e.message); }
  if (acorn) {
    try { acorn.parse(src, { ecmaVersion: 5, sourceType: 'script', allowReserved: true }); }
    catch (e) { problems.push('acorn (ecmaVersion 5): ' + e.message); }
  } else {
    scan(src).forEach((p) => problems.push(p));
  }
  src.split('\n').forEach((text, i) => {
    const code = text.replace(/\/\/.*$/, '').replace(/'(?:\\.|[^'\\])*'|"(?:\\.|[^"\\])*"/g, "''");
    FORBIDDEN_APIS.forEach((re) => { if (re.test(code)) { problems.push('line ' + (i + 1) + ': ES2015+ API ' + code.match(re)[0].trim()); } });
  });
  return problems;
}

function main() {
  const files = [].concat.apply([], DIRS.map(filesIn));
  let failed = 0;
  files.forEach((rel) => {
    const problems = checkFile(rel);
    if (problems.length) { failed++; problems.forEach((p) => console.error('x ' + rel + ' ' + p)); }
  });
  const how = acorn ? 'acorn ecmaVersion 5' : 'built-in ES5 scanner (acorn not installed; run npm install for the full parse)';
  if (failed) { console.error(failed + ' file(s) are not ES5 [' + how + ']'); process.exit(1); }
  console.log('ES5 ok: ' + files.length + ' files [' + how + ']');
}

if (require.main === module) { main(); }
module.exports = { scan, checkFile };
