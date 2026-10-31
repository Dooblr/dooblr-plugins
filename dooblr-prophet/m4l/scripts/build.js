// Builds the Max for Live device from readable sources:
//   src/dooblr-prophet.maxpat  +  src/dooblr-prophet-ui.js  ->  device/dooblr-prophet.amxd (+ the .js beside it)
//   node scripts/build.js [--install]
// --install copies both files into the Live User Library folder named below (creating only that
// folder). It never touches Live's preferences.
'use strict';
const fs = require('fs');
const os = require('os');
const path = require('path');
const amxd = require('./amxd');

const root = path.join(__dirname, '..');
const outDir = path.join(root, 'device');
const installDir = path.join(os.homedir(), 'Music', 'Ableton', 'User Library', 'Presets', 'MIDI Effects', 'Max MIDI Effect', 'dooblr-prophet');

const patcher = JSON.parse(fs.readFileSync(path.join(root, 'src', 'dooblr-prophet.maxpat'), 'utf8'));
const js = fs.readFileSync(path.join(root, 'src', 'dooblr-prophet-ui.js'));
const device = amxd.write(patcher, 'midi');

const back = amxd.read(device); // verify before writing anything
if (back.type !== 'midi' || JSON.stringify(back.patcher) !== JSON.stringify(patcher)) throw new Error('round trip failed');

fs.mkdirSync(outDir, { recursive: true });
fs.writeFileSync(path.join(outDir, 'dooblr-prophet.amxd'), device);
fs.writeFileSync(path.join(outDir, 'dooblr-prophet-ui.js'), js);
console.log('built ' + path.join(outDir, 'dooblr-prophet.amxd') + ' (' + device.length + ' bytes)');

if (process.argv.includes('--install')) {
  fs.mkdirSync(installDir, { recursive: true });
  fs.writeFileSync(path.join(installDir, 'dooblr-prophet.amxd'), device);
  fs.writeFileSync(path.join(installDir, 'dooblr-prophet-ui.js'), js);
  const same = Buffer.compare(fs.readFileSync(path.join(installDir, 'dooblr-prophet.amxd')), device) === 0
    && Buffer.compare(fs.readFileSync(path.join(installDir, 'dooblr-prophet-ui.js')), js) === 0;
  if (!same) throw new Error('installed files do not match the build');
  console.log('installed to ' + installDir + ' (verified byte for byte)');
}
