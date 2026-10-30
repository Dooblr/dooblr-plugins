// Minimal reader/writer for an unfrozen Max for Live device (.amxd).
//
// Layout (little-endian lengths):
//   "ampf" u32 4  <device type: "mmmm" MIDI effect, "aaaa" audio effect, "iiii" instrument>
//   "meta" u32 4  u32 0
//   "ptch" u32 N  <patcher JSON, UTF-8, NUL-terminated; N includes the NUL>
// An unfrozen device keeps its dependencies (here the jsui .js) as separate files; Max finds
// them because they sit in the same folder as the .amxd.
'use strict';

const TYPES = { midi: 'mmmm', audio: 'aaaa', instrument: 'iiii' };

function chunk(tag, payload) {
  const head = Buffer.alloc(8);
  head.write(tag, 0, 'ascii');
  head.writeUInt32LE(payload.length, 4);
  return Buffer.concat([head, payload]);
}

function write(patcher, type) {
  const code = TYPES[type];
  if (!code) throw new Error('unknown device type ' + type);
  const meta = Buffer.alloc(4); // u32 0
  const json = Buffer.concat([Buffer.from(JSON.stringify(patcher, null, '\t'), 'utf8'), Buffer.from([0])]);
  return Buffer.concat([chunk('ampf', Buffer.from(code, 'ascii')), chunk('meta', meta), chunk('ptch', json)]);
}

function read(buffer) {
  const chunks = {};
  let at = 0;
  while (at + 8 <= buffer.length) {
    const tag = buffer.toString('ascii', at, at + 4);
    const size = buffer.readUInt32LE(at + 4);
    if (at + 8 + size > buffer.length) throw new Error('chunk ' + tag + ' runs past the end of the file');
    chunks[tag] = buffer.subarray(at + 8, at + 8 + size);
    at += 8 + size;
  }
  if (at !== buffer.length) throw new Error('trailing bytes after the last chunk');
  for (const tag of ['ampf', 'meta', 'ptch']) if (!chunks[tag]) throw new Error('missing ' + tag + ' chunk');
  const type = Object.keys(TYPES).find((k) => TYPES[k] === chunks.ampf.toString('ascii'));
  if (!type) throw new Error('unknown device type ' + chunks.ampf.toString('ascii'));
  const ptch = chunks.ptch;
  if (ptch[ptch.length - 1] !== 0) throw new Error('patcher JSON is not NUL-terminated');
  return { type, patcher: JSON.parse(ptch.subarray(0, ptch.length - 1).toString('utf8')) };
}

module.exports = { read, write };
