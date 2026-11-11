# dooblr-titan

A Max for Live **MIDI effect** for Ableton Live 11 Suite. It **tightens** a MIDI
clip (quantize, flams, tiny-note cleanup, legato, velocity, optional scale snap)
and writes **musical variations** of it. Every result is a **new** Session View
clip; the original is never overwritten.

Version 0.1.0. Built for Live 11.3 with its bundled Max 8.5 on macOS. The device
uses only the classic `js`/`jsui` objects (ES5) and `live.*` controls.

## Features

* **Load a clip.** Drop a `.mid` file onto the SOURCE drop zone, or select a
  MIDI clip in Live and press **USE CLIP**. Incoming MIDI on the track still
  passes straight through.
* **Tighten.** Quantize with strength, grid (straight and triplets) and swing;
  optional groove-preserving quantize; fix overlapping / flam notes; drop tiny
  accidental notes (min length and velocity); legato tidy (trim overlaps,
  optional gap close); compress velocities toward a target range with a
  humanize amount; optional scale snap (key + scale, Auto Key on load).
* **Variations.** Four deterministic variations of the loaded clip: Displace
  (rhythmic shift), Substitute (in-scale melody / octaves), Fill (density and
  phrase-end ghosts), Invert (inversion or retrograde, thinning). Amount,
  Rhythm, Melody, Ghosts, Accents and Seed scale them. **Generate** builds the
  list, **Another** picks a new seed, click a row, **Write** makes a clip.
* **Never destructive.** Tighten and Write always go to the next empty Session
  slot. If the source came from USE CLIP, the device tries `duplicate_clip_to`
  first (keeps clip colour / loop) and falls back to `create_clip`.
* **Save MIDI.** Writes the last Tighten or Write result as a Standard MIDI
  File. Live 11 / Max 8 cannot drag a generated file *out* of `live.drop`.
* **Seeded.** The same seed and settings always give the same notes.

## Controls

The layout is 860 × 252 px in four columns.

| Section | Control | What it does |
|---|---|---|
| Actions (left) | **Tighten** | Improve the loaded clip and write a new clip. |
| | **Generate** | Build four variations. Does not write. |
| | **Another** | New seed, rebuild the four variations. |
| | Write | Write the selected variation to the next empty slot. |
| | Save MIDI | Open a save dialog and write the last result as `.mid`. |
| | Seed | Makes variations repeatable. |
| TIGHTEN | **Q Str** | Quantize strength, 0–100. |
| | **Grid** | 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32, 1/32T. |
| | **Swing** | Delay offbeats. 100 = last triplet of the pair. |
| | **Groove** | Keep the clip's consistent micro-timing; pull outliers onto it. |
| | **Flams** | Merge same-pitch hits that start almost together. |
| | **MinLen / MinVel** | Drop notes shorter or quieter than this. |
| | **Legato / Gap** | Trim same-pitch overlaps; close small holes. |
| | **VelMin / VelMax / Comp** | Compress velocities into that range. |
| | **Human** | Seeded velocity jitter. |
| | **Snap / Root / Mode / Auto** | Scale snap; Auto Key sets Root and Mode on load. |
| VARIATIONS | **Amount** | Master mix. 0 leaves the clip unchanged. |
| | **Rhythm / Melody / Ghosts / Accent** | How much of each treatment. |
| | preview list | Click a variation, then Write. |
| SOURCE | drop zone | `.mid` files from Finder or Live's file browser. |
| | **USE CLIP** | Detail View clip, else the highlighted slot. |
| | CLEAR | Forget the source. |
| | piano roll | Loaded notes, and the last result overlaid. No hint captions. |

Changing a control updates the preview list (once Generate has run) but does
not write a clip. Only Tighten and Write create clips.

## Install

Copy the whole `device/dooblr-titan/` folder to:

```
~/Music/Ableton/User Library/Presets/MIDI Effects/Max MIDI Effect/dooblr-titan/
```

The folder holds four files that must stay together:

* `dooblr-titan.amxd`
* `dooblr_titan.js`
* `dooblr_roll.js`
* `dooblr_previews.js`

In Live, open Browser → User Library → Presets → MIDI Effects → Max MIDI
Effect → dooblr-titan. Drag `dooblr-titan.amxd` onto a MIDI track that has an
instrument after it.

## Rebuild

You need Node 18 or later.

```
npm install          # acorn, for the ES5 parse (a system acorn also works)
npm test             # engine, glue and tooling tests
npm run check:es5    # every Max-side js file must parse as ES5
npm run build        # writes device/dooblr-titan/*
npm run validate -- --built
npm run verify       # all of the above (builds before testing)
```

`node scripts/make-patcher.js` regenerates `src/patcher/dooblr-titan.maxpat`
from the layout in that script.

Source layout:

```
src/engine/      pure ES5 MIDI engine (no Max APIs); manifest.json sets the load order
  rng.js         seeded PRNG (mulberry32)
  theory.js      scales and modes
  notes.js       clip-note helpers
  midifile.js    Standard MIDI File reader and writer
  quantize.js    grid (incl. triplets), swing, groove-preserving blend
  cleanup.js     flams, overlaps, tiny notes, legato / gap close
  velocity.js    compress toward a range + humanize
  keydetect.js   Krumhansl–Kessler key detection
  scalesnap.js   snap pitches onto a scale
  vary.js        four deterministic variation characters
  engine.js      facade used by Max and the tests
src/max/main.js  Max glue: load, tighten, variations, Live API clip read/write, save MIDI
src/max/roll.js  jsui piano roll (no hint text)
src/max/previews.js  jsui variation list
src/patcher/dooblr-titan.maxpat  the device patcher (source of truth)
scripts/         build, amxd reader/writer, patcher validator, ES5 check, patcher generator
tests/           node:test suites (the engine and the built Max files in a mocked runtime)
device/dooblr-titan/  built output (generated; gitignored)
docs/amxd-format.md    the container format
```

To edit the patcher in Max, open the built device, unlock it and make your
changes. Then save the patcher JSON over `src/patcher/dooblr-titan.maxpat`
(File → Save As… with a `.maxpat` name) and run `npm run build`. Do not edit
the files in `device/`, because they are generated.

## Live 11 clip drag-in

Live 11.3 / Max 8 does **not** deliver Session View or Arrangement MIDI clips
onto a Max for Live device. `live.drop` receives **files**:

* `.mid` / `.midi` from Finder (macOS) or Explorer (Windows)
* files from Live's browser when they are real files on disk (Places, User
  Library, a folder)

It does **not** receive:

* Session View clip slots
* Arrangement clips
* clips dragged from Live's clip/sample browser that are not files

**USE CLIP** is the Live-clip path: `live_set view detail_clip`, falling back
to `live_set view highlighted_clip_slot clip`. That covers a clip shown in
Detail View or the highlighted slot, including arrangement clips when they are
the Detail View clip. Live 12-only drop APIs are not used.

Drag-out of a generated `.mid` from inside the device is not supported by Max 8
`live.drop` (drop-in only). Use **Save MIDI**.

## Manual test checklist in Live

1. Drop the device on a MIDI track with an instrument. The device is 252 px
   tall with the teal bar and title, and there are no errors in the Max window.
2. Play the keyboard. Notes pass through to the instrument.
3. Drag a `.mid` file onto the drop zone. Status names the file and a key; no
   clip is written.
4. Press **Tighten**. A new clip appears in the first empty slot, named like
   `riff · TIGHT · 1/16 Q100`. The original clip (or file) is untouched.
5. Press **Generate**. Four rows appear (Displace, Substitute, Fill, Invert).
   Click Fill, press **Write**. A second clip is named `riff · VAR Fill · …`.
6. Press **Another**. The seed changes and the four rows rebuild. Write again.
7. Select a MIDI clip in Session View and press **USE CLIP**. The source is the
   looped region if looping. Select an audio clip: the status says so.
8. Turn Snap on, set Root/Mode, Tighten. Out-of-scale notes move.
9. Turn Auto off, change Root, load another file: Root stays until you press
   Auto on and load again (Auto applies on load).
10. Fill every slot and press Tighten. Status: every slot is full. Nothing else
    happens; no scene is created.
11. Save MIDI: pick a path, open the file in Live or a MIDI editor.
12. Put the device in a MIDI Effect Rack and Tighten. It still writes to the
    track's slots.

## Known limitations

* Not tested inside Live yet. The build was produced and checked without Max
  or Live (see "Unverified" below).
* The device writes only to Session View, never to the Arrangement. It never
  creates scenes.
* Tempo and time signature changes in a MIDI file are ignored. Times are in
  beats, and the length is rounded up to whole 4/4 bars.
* A USE CLIP source is not saved with the Set. A dropped file is saved as a
  path, so moving or deleting the file loses it on reload.
* The device is unfrozen: the four files must stay in one folder.
* Key detection can confuse relative major/minor when the clip has no cadence.
  Set Root and Mode by hand, or leave Snap off.

### Unverified without Max/Live

* The `meta` value in the `.amxd` header (see docs/amxd-format.md).
* The exact colour attribute names of the `live.*` objects. Max ignores names
  it does not know, so the worst case is a default colour.
* Whether Live converts the JS object passed to `add_new_notes` into a
  dictionary. If it does not, the device falls back to `set_notes`.
* `duplicate_clip_to` on a source clip from USE CLIP. If Live rejects it, the
  device falls back to `create_clip`.
* Whether a `live.text` button sends `bang` or `1`. The patcher accepts both.
* `live.drop` path format (Max `Volume:/path` style is assumed and handled).
* `savedialog mid` returning a path without an extension (the glue appends
  `.mid`).
* Max `File` writing with `writebytes` in 1024-byte chunks.
* Fonts and the 860 px device sitting in Live's device chain.
