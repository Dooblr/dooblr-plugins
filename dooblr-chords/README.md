# dooblr-chords

A Max for Live **MIDI effect** for Ableton Live 11 Suite. It generates
functional chord progressions and writes each one as a new MIDI clip into the
next empty Session View clip slot on its own track, so variations pile up
down the track.

Version 0.1.0. Built for Live 11.3.43 with its bundled Max 8.5.8 on macOS
(Apple Silicon). The device uses only the classic `js`/`jsui` objects (ES5) and
`live.*` controls.

## Features

* **Key**: root C to B. Modes: Major, Natural Minor, Dorian, Phrygian,
  Lydian, Mixolydian and Harmonic Minor.
* **Length** of 2 to 16 chords (default 4). **Bars per Chord** of ½, 1 or 2
  (default 1).
* **Color** (0 to 100) adds sevenths, then ninths, elevenths and thirteenths,
  skipping avoid notes. At 0 every chord is a triad.
* **Chromatic** (0 to 100) adds borrowed chords, secondary dominants, tritone
  substitutions and the Neapolitan. At 0 the progression is strictly
  diatonic.
* **Generation** walks a functional Markov chain (tonic, pre-dominant,
  dominant) and ends on a cadence: an authentic V–I, or a turnaround that
  loops back to the start.
* **Voicing** uses voice leading between chords, with Register, Spread (close,
  drop-2 or open) and an optional separate bass note.
* **Seed**: every result comes from the seed shown on the device. The same
  seed and settings always give the same notes.
* **Lock and Reroll**: click a chord block to lock it. Reroll keeps the locked
  chords and regenerates the rest.
* **Velocity**, plus **Humanize** for small timing and velocity changes. Humanize
  is off by default.
* Incoming MIDI passes straight through.

## Controls

| Section | Control | What it does |
|---|---|---|
| Header | **Generate** | Picks a new seed, clears locks and writes a clip to the next empty slot. |
| | **Reroll** | Picks a new seed, keeps locked chords and writes a clip. It is dimmed until a chord is locked. |
| | **Write** | Writes the progression shown into the next empty slot again, unchanged. |
| | **Seed** | The current seed. Type a number to recall a progression. It is drawn at 60% when every chord is locked, because it then only affects Humanize. |
| KEY | Root, Mode, Length, Bars per Chord | Key, scale, number of chords and chord length. |
| TENSION | Color, Chromatic | Extensions and chromatic substitutions. |
| VOICING | Register, Spread, Bass | Lowest upper note, voicing width and the bass note on or off. |
| OUTPUT | Velocity, Humanize | Note velocity and human feel. |
| PROGRESSION | chord blocks | Each block shows the chord name and Roman numeral. Click a block to lock or unlock it. |

Changing a control updates the preview but does not write anything. Only
Generate, Reroll and Write create clips. Clips are named like
`C min · 4 · Col35 Chr10`.

## Install

Copy the whole `device/dooblr-chords/` folder to:

```
~/Music/Ableton/User Library/Presets/MIDI Effects/Max MIDI Effect/dooblr-chords/
```

The folder holds three files that must stay together:

* `dooblr-chords.amxd`
* `dooblr_chords.js`
* `dooblr_blocks.js`

In Live, open Browser → User Library → Presets → MIDI Effects → Max MIDI
Effect → dooblr-chords. Drag `dooblr-chords.amxd` onto a MIDI track that has
an instrument after it.

## Rebuild

You need Node 18 or later.

```
npm install          # acorn, for the ES5 parse (a system acorn also works)
npm test             # engine, glue and tooling tests
npm run check:es5    # every Max-side js file must parse as ES5
npm run build        # writes device/dooblr-chords/*
npm run validate -- --built
npm run verify       # all of the above
```

Source layout:

```
src/engine/      pure ES5 music engine (no Max APIs); manifest.json sets the load order
  rng.js         seeded PRNG (mulberry32) with independent per-slot streams
  theory.js      notes, spelling, modes
  chords.js      chord model, extensions, naming, Roman numerals
  generator.js   functional Markov chain, cadences, chromatic substitutions, locks
  voicing.js     voice leading, register, spread, bass
  render.js      notes for the clip (rhythm, humanize and strum extension points)
  engine.js      the facade used by Max and the tests
src/max/main.js  Max glue: parameters, buttons, locks, Live API clip writing
src/max/blocks.js  jsui chord display
src/patcher/dooblr-chords.maxpat  the device patcher (source of truth)
scripts/         build, amxd reader/writer, patcher validator, ES5 check
tests/           node:test suites (the engine and the built Max files in a mocked runtime)
device/dooblr-chords/  built output (committed)
docs/amxd-format.md    the container format and its uncertainties
```

To edit the patcher in Max, open the built device, unlock it and make your
changes. Then save the patcher JSON over `src/patcher/dooblr-chords.maxpat`
(File → Save As… with a `.maxpat` name) and run `npm run build`. Do not edit
the files in `device/`, because they are generated.

### Extending

* **Rhythms**: add an entry to `PATTERNS` in `src/engine/render.js`. A pattern
  maps a chord slot to note events. `sustain` is the default.
* **Strum**: `render.notes(..., { strum: beats })` already offsets each voice.
  Wire a control to it with a `prepend param strum`, add it to `PARAM_NAMES`
  and `DEFAULTS`, and pass it through in `engine.build`.
* **Audition**: send the current `notes` out of a new js outlet into
  `[makenote]` → `[midiout]`.
* **New modes or chord colours**: `MODES` in `theory.js`, `MODE_RULES` in
  `generator.js`, and `applyColor` in `chords.js`.

## Manual test checklist in Live

1. Drop the device on a MIDI track with an instrument. The device is 169 px
   tall with the teal bar and title, and there are no errors in the Max
   window.
2. Play the keyboard. Notes pass through to the instrument.
3. Press **Generate**. A clip appears in the first empty slot, named like
   `C min · 4 · Col35 Chr10`, 4 bars long. It plays the chords shown in the
   blocks, and the Seed changes.
4. Press **Generate** again. The next empty slot gets a new clip, and earlier
   clips are untouched.
5. Set Length 8 with ½ bar per chord. The clip is 4 bars with 8 chords. With
   Length 12 the blocks wrap onto two rows.
6. Set Chromatic to 0 and generate several times. Only diatonic chords
   appear. At 80 or more, secondary dominants (V/x), subV/x, borrowed chords
   and N show up.
7. Color 0 gives triads only. Color 100 gives 9ths, 11ths and 13ths.
8. Click a block. It gets a coloured outline and a padlock, and Reroll
   lights up. Press **Reroll**. Locked chords stay, the others change, and a
   new clip is written.
9. Type a Seed you saw earlier, then press **Write**. It writes the identical
   progression.
10. Fill every slot on the track and press Generate. The status reads "Every
    clip slot on this track is full…" and nothing else happens.
11. Put the device in a MIDI Effect Rack and press Generate. It still writes
    to the track's slots.
12. Save the Set, reopen it and check that the controls are restored. Locks
    are not restored.
13. Check that the ♭ and ♯ signs show in the blocks. If they show as boxes,
    send the js the message `ascii 1`.

## Known limitations

* Not tested inside Live yet. The build was produced and checked without Max
  or Live (see "Unverified" below).
* Locks only last for the session. They are not saved with the Live Set.
* The device writes only to Session View, never to the Arrangement. It never
  creates scenes.
* Only one rhythm (sustained chords). Strum and audition are extension points
  only.
* The device is unfrozen: the three files must stay in one folder.

### Unverified without Max/Live

* The `meta` value in the `.amxd` header (see docs/amxd-format.md).
* The exact colour attribute names of the `live.*` objects (for example
  `activedialcolor` and `panelcolor` on `live.dial`). Max ignores names it
  does not know, so the worst case is a default colour.
* Whether Live converts the JS object passed to `add_new_notes` into a
  dictionary. If it does not, the device falls back to the older `set_notes`
  calls and checks the result with `get_notes_extended`.
* Whether a `live.text` button sends `bang` or `1`. The patcher accepts both.
* Font fallback for ♭ and ♯ in jsui, and Max finding the js files next to an
  unfrozen `.amxd` in the User Library.
