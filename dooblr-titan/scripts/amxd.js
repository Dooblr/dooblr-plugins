// Reader/writer for the unfrozen Max for Live device container (.amxd).
// See docs/amxd-format.md for the layout and what is uncertain about it.
//
//   'ampf' u32le(4)  'mmmm'          device type: mmmm = MIDI effect
//   'meta' u32le(4)  u32le(META)     meta value (we write 0, see docs)
//   'ptch' u32le(N)  <patcher JSON> 0x00
'use strict';

const DEVICE_TYPES = { midi: 'mmmm', audio: 'aaaa', instrument: 'iiii' };
const META_VALUE = 0;

function chunk(tag, payload) {
  const head = Buffer.alloc(8);
  head.write(tag, 0, 4, 'latin1');
  head.writeUInt32LE(payload.length, 4);
  return Buffer.concat([head, payload]);
}

function write(patcher, options) {
  const type = DEVICE_TYPES[(options && options.type) || 'midi'];
  const meta = Buffer.alloc(4);
  meta.writeUInt32LE(options && options.meta !== undefined ? options.meta : META_VALUE, 0);
  const json = typeof patcher === 'string' ? patcher : JSON.stringify(patcher, null, '\t');
  const ptch = Buffer.concat([Buffer.from(json, 'utf8'), Buffer.from([0])]);
  return Buffer.concat([chunk('ampf', Buffer.from(type, 'latin1')), chunk('meta', meta), chunk('ptch', ptch)]);
}

function read(buf) {
  const chunks = [];
  let off = 0;
  while (off + 8 <= buf.length) {
    const tag = buf.toString('latin1', off, off + 4);
    const len = buf.readUInt32LE(off + 4);
    if (off + 8 + len > buf.length) { throw new Error('chunk ' + tag + ' overruns the file'); }
    chunks.push({ tag, payload: buf.subarray(off + 8, off + 8 + len) });
    off += 8 + len;
  }
  if (off !== buf.length) { throw new Error('trailing bytes after the last chunk'); }
  const byTag = {};
  chunks.forEach((c) => { byTag[c.tag] = c.payload; });
  if (!byTag.ampf || !byTag.ptch) { throw new Error('missing ampf or ptch chunk'); }
  const ptch = byTag.ptch;
  if (ptch.subarray(0, 4).toString('latin1') === 'mx@c') { throw new Error('frozen device (mx@c); not supported by this reader'); }
  let end = ptch.length;
  while (end > 0 && ptch[end - 1] === 0) { end--; }
  return {
    type: byTag.ampf.toString('latin1'),
    meta: byTag.meta ? byTag.meta.readUInt32LE(0) : null,
    tags: chunks.map((c) => c.tag),
    patcher: JSON.parse(ptch.subarray(0, end).toString('utf8'))
  };
}

module.exports = { write, read, DEVICE_TYPES, META_VALUE };
