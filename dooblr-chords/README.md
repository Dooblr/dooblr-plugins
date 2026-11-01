# dooblr-chords

A Max for Live **MIDI effect** for Ableton Live 11 Suite. It generates
functional chord progressions and writes each one as a new MIDI clip into the
next empty Session View clip slot on its own track, so variations pile up
down the track.

Version 0.3.0. Built for Live 11.3.43 with its bundled Max 8.5.8 on macOS
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
* **Source**: drop a `.mid` file on the SOURCE drop zone, or select a MIDI
  clip in Live and press **USE CLIP**. The device detects the source's chords
  (block chords and arpeggios, bass inversions, up to 16 chords) and its key,
  shows both, and sets Root, Mode and Length to match.
* **VAR / B**: with a source loaded, Generate, Reroll and Write make
  * **VAR**, a variation: same key, length and harmonic rhythm, the first and
    last chords kept, and some inner chords swapped for diatonic neighbours,
    substitutes or (with Chromatic) borrowed chords and secondary dominants.
    Changed chords get an orange dot.
  * **B**, a contrasting B section: it moves to a related key area (for
    example vi or IV in major, III or iv in minor, ♭VI or ♭II with
    Chromatic), may change the harmonic rhythm (same, faster, slower or a
    push into the last bar) and ends on a chord that leads back to the
    source's first chord.
  Setting Length to anything other than the source's chord count drops the
  source rhythm and uses Bars per Chord instead. Register, Spread and Bass
  still set the voicing; the number of upper voices follows the source's
  thickness (3 to 5).
* Incoming MIDI passes straight through.

## Controls

The layout is 724 x 169 px in five columns. The controls you use most are
large and bright; the ones you set once are small and grey but always there.

| Section | Control | What it does |
|---|---|---|
| Actions (left) | **Generate** | The largest button. Picks a new seed, clears locks and writes a clip to the next empty slot. |
| | **Reroll** | Picks a new seed, keeps locked chords and writes a clip. It is dimmed until a chord is locked. |
| | Write (small) | Writes the progression shown into the next empty slot again, unchanged. |
| | Seed (small) | The current seed. Type a number to recall a progression. It is drawn at 60% when every chord is locked, because it then only affects Humanize. |
| KEY | **Root**, **Mode**, **Length**, Bars per Chord | Key, scale, number of chords and chord length. |
| TENSION | **Color**, **Chromatic** (large dials) | Extensions and chromatic substitutions. |
| ADVANCED | Register, Spread, Bass | Lowest upper note, voicing width and the bass note on or off. Small grey dials. |
| | Velocity, Humanize | Note velocity and human feel. Small grey dials. |
| SOURCE (right) | drop zone | Drop a `.mid` file to load it as the source. The path is saved with the Set. |
| | **USE CLIP** | Loads the MIDI clip shown in the Detail View (else the clip in the highlighted slot). Uses the loop if looping, else start to end marker. |
| | CLEAR (small) | Forgets the source. Generate makes fresh progressions again. |
| | **VAR / B** | Variation or B section of the source. Dimmed without a source. |
| | info line | Source name, detected key, chord and bar count, or a hint while there is no source. |
| PROGRESSION | chord blocks | Each block shows the chord name over its Roman numeral. Click a block to lock or unlock it. The header shows the heading (VARIATION or B SECTION with a source, plus the B key area), and the last status message on the right; hover over the blocks to read a long status in full. With a source a SOURCE strip lists the source's chords. |

Changing a control updates the preview but does not write anything. Only
Generate, Reroll and Write create clips. Clips are named like
`C min · 4 · Col35 Chr10`, or `riff · VAR · A min · Col35 Chr10` and
`riff · B III · A min · Col35 Chr10` when made from a source. Loading a source
never writes a clip.

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
npm run verify       # all of the above (builds before testing)
```

`node scripts/render-layout.js | python3 scripts/render-layout.py out.png`
draws a mockup of the panel (needs Pillow). The chord blocks come from the
real jsui paint code; Live's own widgets are approximated.

Source layout:

```
src/engine/      pure ES5 music engine (no Max APIs); manifest.json sets the load order
  rng.js         seeded PRNG (mulberry32) with independent per-slot streams
  theory.js      notes, spelling, modes
  chords.js      chord model, extensions, naming, Roman numerals
  generator.js   functional Markov chain, cadences, chromatic substitutions, locks
  voicing.js     voice leading, register, spread, bass
  render.js      notes for the clip (rhythm, humanize, uneven slots, strum extension point)
  midifile.js    Standard MIDI File reader (format 0/1, running status)
  analysis.js    chord segmentation, chord naming and key detection for a source
  derive.js      VAR (variation) and B (B section) from an analysed source
  engine.js      the facade used by Max and the tests
src/max/main.js  Max glue: parameters, buttons, locks, source loading, Live API clip reading/writing
src/max/blocks.js  jsui chord display
src/patcher/dooblr-chords.maxpat  the device patcher (source of truth)
scripts/         build, amxd reader/writer, patcher validator, ES5 check, layout mockup
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
14. The device is 724 px wide with five columns: actions, KEY/TENSION/
    ADVANCED over the chord blocks, and SOURCE. Generate, Reroll, Root, Mode,
    Length, Color, Chromatic, USE CLIP and VAR/B are the large, bright
    controls; Write, Seed, CLEAR and the ADVANCED dials are small and grey.
    Every label is readable, nothing is clipped or overlapping, and the
    ADVANCED dials can be dragged and double-clicked to type a value. VAR/B
    and CLEAR are dimmed. Hover over the blocks: the hint ends with the full
    status text.
15. Drag a `.mid` file with block chords (for example Am F C G, one bar each)
    from Finder onto the drop zone. The status reads "Loaded <name>: A Natural
    Minor, 4 chords…", Root/Mode/Length change to A, Natural Minor and 4, the
    SOURCE strip shows `Am F C G`, the heading reads VARIATION, and no clip is
    written. Drop a `.wav` and a damaged `.mid`: each gives a readable warning
    and the old source stays.
16. Press **Generate**. The clip is named `<name> · VAR · A min · …`, has the
    source's length and rhythm, starts on Am and ends on G. Changed chords
    have an orange dot. Generate a few times: different inner chords.
17. Switch to **B** and press Generate. The heading reads B SECTION with a key
    area such as "in C major (III)", the chords differ from the source, and
    playing the B clip then the source clip sounds like a return home.
18. In B, lock a chord and press Reroll several times. The locked chord and
    the chord count stay the same.
19. Select a MIDI clip in Session View (Detail View showing its notes),
    including one with a loop that does not start at 1.1.1, and press **USE
    CLIP**. The source is the looped part. Select an audio clip: the status
    says so. Deselect all clips: "No clip is selected…".
20. Press **CLEAR**. The heading returns to PROGRESSION and VAR/B dims.
21. Load a file, save the Set, reopen it. The file source comes back (if the
    file still exists) and the saved Root/Mode/Length are kept. A USE CLIP
    source is not saved.
22. Try a source with arpeggios or a melody on top of the chords, and one in
    3/4. Check the detected chords make sense; fix the key with Root/Mode if
    not (detection reads relative major/minor wrongly in about 1 in 5 plain
    diatonic loops).

## Known limitations

* Not tested inside Live yet. The build was produced and checked without Max
  or Live (see "Unverified" below).
* Locks only last for the session. They are not saved with the Live Set.
* The device writes only to Session View, never to the Arrangement. It never
  creates scenes.
* Only one rhythm (sustained chords). Strum and audition are extension points
  only. VAR and B keep the source's harmonic rhythm, not its note rhythm:
  each chord is written as a sustained block.
* Source analysis reads harmony on a quarter-beat grid, at most 16 chords. It
  reads a melody note as a chord tone if it is long and loud enough, and it
  cannot tell relative major from minor when the clip gives no cadence clue.
  Root and Mode can be changed after loading to read the source in another
  key.
* Tempo and time signature changes in a MIDI file are ignored. Times are in
  beats, and the length is rounded up to whole 4/4 bars.
* A USE CLIP source is not saved with the Set. A dropped file is saved as a
  path, so moving or deleting the file loses it on reload.
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
* `live.drop`: the saved attributes (parameter mode, `legend`, colours), that
  it re-sends its saved path on load before `live.thisdevice` fires, that
  `clear` empties it without sending a path, and the path format it outputs
  (Max `Volume:/path` style is assumed and handled).
* Max `File` reading of binary data: `readbytes` in 1024-byte chunks, `eof`
  as the file size, and signed or unsigned values (both are masked).
* `live_set view detail_clip` and `live_set view highlighted_clip_slot clip`
  as LiveAPI paths, the `is_midi_clip`/`looping`/`loop_start`/`loop_end`/
  `start_marker`/`end_marker` names, and `get_notes_extended` returning a
  JSON string (an array of atoms is also joined and parsed).
* `set` on the `srcinfo` comment and on the Root, Mode and Length controls
  (changes them without output; the js updates its own settings), and
  `active 0` dimming the `live.tab` VAR/B.
* The 0.3.0 layout: how `live.dial` draws at 42 x 36 px with its name and
  value shown (the arc may be very small), the 8.5 pt and 9 pt fonts on
  `live.*` objects, `textcolor` dimming on the grey Seed box, and how the
  724 px device sits in Live's device chain. `scripts/render-layout.py` only
  approximates Live's widgets.
