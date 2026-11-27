# dooblr-chords

A Max for Live **MIDI effect** for Ableton Live 11 Suite. It generates
functional chord progressions and writes each one as a new MIDI clip into the
next empty Session View clip slot on its own track, so variations pile up
down the track.

Version 0.8.1. Built for Live 11.3.43 with its bundled Max 8.5.8 on macOS
(Apple Silicon). The device uses only the classic `js`/`jsui` objects (ES5) and
`live.*` controls.

## Features

* **Key**: root C to B. Modes: Major, Natural Minor, Dorian, Phrygian,
  Lydian, Mixolydian and Harmonic Minor.
* **Length** of 2 to 16 chords (default 4). **Bars per Chord** of ½, 1 or 2
  (default 1).
* **Color** (0 to 100) colours the tones of the chord type Notes has chosen:
  sus or add variants, and (when Notes is 9th or 11th) altered tensions,
  skipping avoid notes. It does not change how many notes each chord has.
* **Spice** is a new 0–100 macro (default 35) on the main view. It sets
  **Color** and **Chromatic** together along a fixed curve through the
  factory default (Spice 35 = Color 35, Chromatic 10). The original Color
  and Chromatic parameters keep their names, varnames and saved values, and
  remain individually available in the Advanced window. Older Sets that
  have no Spice parameter recall Color and Chromatic unchanged; the Spice
  dial is then derived from those two so it does not smash a recalled Set.
  Chromatic itself still adds borrowed chords, secondary dominants, tritone
  substitutions and the Neapolitan. At Chromatic 0 the progression is
  strictly diatonic.
* **Generation** walks a functional Markov chain (tonic, pre-dominant,
  dominant) and ends on a cadence: an authentic V–I, or a turnaround that
  loops back to the start.
* **Notes** sets the chord type for every generated chord: **TRI** (triad,
  3 notes: 1–3–5 or sus), **7TH** (4 notes, +7), **9TH** (5 notes, +9) or
  **11TH** (6 notes, +11). A natural 11 that clashes with a major third is
  replaced by #11 in Lydian contexts, or otherwise by 13 (so Cmaj13 or G13,
  but Fmaj9#11 and Am11). The default is Triad. Color and Spice still colour
  the tones (alterations, borrowed chords and substitutions) but they do not
  change how many notes each chord has. Notes N is the number of MIDI notes
  written for each chord, including the bass. When Bass is on, the bass is
  one of those N notes and takes the root out of the upper voicing, so a
  triad still has three sounding voices. A slash bass is also one of the N
  (a triad inversion stays three notes). Octave doublings are not added, and
  notes end at or before the next chord starts. The blocks and clip names
  show the resulting type (Cmaj7, Am9, …). Notes is dimmed while Lead is on,
  because a lead line is monophonic.
* **Voicing** uses voice leading between chords, with Register, Spread (close,
  drop-2 or open) and an optional separate bass note.
* **Seed**: every result comes from the seed shown on the device. The same
  seed and settings always give the same notes.
* **Lock and Reroll**: click a chord block to lock it. Reroll keeps the locked
  chords and regenerates the rest.
* **Velocity**, plus **Humanize** for small timing and velocity changes. Humanize
  is off by default.
* **Source**: press **ADVANCED** and use the Source section in the floating
  window. Drop a `.mid` file on the drop zone, or
  select a MIDI clip in Live and press **USE CLIP**. The device detects the source's chords
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
  still set the voicing. Notes forces every derived chord to the same type
  (triad, 7th, 9th or 11th).
* Incoming MIDI passes straight through.
* **CLIP / LIVE**: a tab at the top of the left rail. **CLIP** (the default)
  is the original behaviour: Generate, Reroll and Write put a MIDI clip in
  the next empty Session slot. **LIVE** plays the generated progression as
  MIDI notes in real time out of the device, synced to Live's transport. It
  waits for the next bar line after you press play (or starts immediately if
  you are already on one), then stays in phase with the Set: the loop
  position is the song time modulo the progression length, and Bars per
  Chord is honoured. The currently playing chord is outlined in teal in the
  blocks (gold if Lead is also on). Generate is labelled **New Progression**
  (or **New Lead**) and starts the new sequence on the next bar; it does
  not write a clip. Write is dimmed. Knob changes (Key, Mode, Color, Spice,
  Notes, Register, Spread, Velocity, Humanize, Bass, Density, and Lead)
  apply at the next chord boundary, with note-offs sent before the next
  chord's note-ons so nothing overlaps or sticks. Stopping the transport,
  switching back to CLIP, disabling or deleting the device, all send
  note-offs for every pitch the device turned on (never CC 123, so
  notes you are holding through the device are left alone).
* **Evolve** (Advanced, LIVE only): Off, every cycle, every 2 cycles, or
  every 4 cycles. At those cycle boundaries one or two inner unlocked
  chords are mutated to a neighbour or substitute; the first and last
  chords stay put. The same seed and cycle always give the same mutation.
* LEAD, Source, VAR and B all play in LIVE the same way they write in CLIP:
  a monophonic lead line, or the derived progression, clocked to the
  transport.

## First use

The main view only shows what you need to make a progression. Advanced
sound settings and the variation / B-section tools live in pop-up panels
inside the device (not separate Max windows).

1. Drop the device on a MIDI track that has an instrument after it.
2. Pick a **Root** and **Mode**, set **Length**, and optionally turn **Notes**
   (triad, 7th, 9th or 11th; triad is the default), **Color** (how those
   tones are coloured) or **Spice** (more unexpected chords).
3. Leave the rail tab on **CLIP** and press **Generate**. A clip is written
   to the next empty slot, and the chords appear in the blocks.
4. Or switch the tab to **LIVE**, press play in Live, and the same
   progression is performed in real time from the next bar. Press
   **New Progression** for a new seed starting on the next bar, or
   **TO CLIP** to write the sounding progression into a Session slot
   without stopping playback.
5. Click a block to lock it, then press **Reroll** to change the others.
6. Press **LEAD** (the large gold toggle) and Generate / New Lead again
   for a melody over those chords. The blocks stay, with a LEAD header, so
   you can see what the line follows. Lead works in LIVE too.
7. Press **ADVANCED** to open a floating Max window with Color, Chromatic,
   Notes, Bars per Chord, voicing, velocity, humanize, Density, Seed, Write,
   Evolve, Bus Channel and the Source section (drop a `.mid` file, USE CLIP,
   CLEAR, VAR/B). Density is dimmed unless Lead is on. Evolve is dimmed in
   CLIP mode. Those parameters stay saved and automatable; they just live
   in the subpatcher window.

## Controls

The device is 400 × 169 px. The left rail holds the CLIP / LIVE tab,
Generate (or New Progression), TO CLIP, LEAD, Reroll and ADVANCED. The main
pane is built around two large knobs — **SPICE** (how adventurous) and
**LENGTH** (how many chords) — plus a compact Key / Mode selector (kept on
the main view because changing key is a primary musical choice) and the
chord blocks with the playhead. **ADVANCED** opens a real floating Max
window (`p advanced`) for everything else, including Source. live.*
parameters in that window remain device parameters: they are saved with the
Set and can be automated.

| Section | Control | What it does |
|---|---|---|
| Actions (left) | **CLIP / LIVE** | Mode switch, teal when LIVE is selected. CLIP writes Session clips. LIVE plays the progression as MIDI in real time. |
| | **Generate** | In CLIP: picks a new seed, clears locks and writes a clip to the next empty slot. In LIVE the label reads **New Progression** (or **New Lead**) and the new sequence starts on the next bar; no clip is written. |
| | **TO CLIP** | LIVE only. Writes the sounding progression (Evolve included) to the next empty Session slot, with a `· LIVE` name, without stopping playback. Dimmed in CLIP. |
| | **Reroll** | Picks a new seed, keeps locked chords and writes a clip (CLIP) or starts them on the next bar (LIVE). It is dimmed until a chord is locked. |
| | source badge | When a source is loaded, a small chip such as `VAR · my riff` appears under Reroll, so that state is not hidden in the Source panel. |
| Main | **Root**, **Mode**, **Length** | Key, scale and number of chords. |
| | **Notes** | Chord type: TRI (triad, 3 notes), 7TH (4), 9TH (5) or 11TH (6). The default is Triad. Color and Spice colour the tones but do not change the count. Bass, when on, is one of the N sounding notes. Dimmed while Lead is on. |
| | **Color**, **Spice** | Color adds extensions. Spice is the Chromatic parameter: borrowed chords and substitutions. The Live parameter name is still Chromatic. |
| | **ADVANCED …** | Opens the Advanced overlay. |
| | **SOURCE …** | Opens the Source overlay. |
| | chord blocks | Each block shows the chord name over its Roman numeral. Click a block to lock or unlock it. The header shows the heading (VARIATION or B SECTION with a source, plus the B key area) and the last status message; hover over the blocks to read a long status in full. With a source a SOURCE strip lists the source's chords. |
| ADVANCED panel | Bars per Chord | How long each chord lasts: half a bar, one bar or two bars. |
| | Register, Spread, Bass | Lowest upper note, voicing width and the bass note on or off. |
| | Velocity, Humanize | Note velocity and human feel. |
| | Seed | The current seed. Type a number to recall a progression. It is drawn at 60% when every chord is locked, because it then only affects Humanize. |
| | Write | Writes the progression shown into the next empty slot again, unchanged. Dimmed in LIVE mode, with a tooltip explaining why. |
| | **Evolve** | LIVE only: Off, every cycle, every 2 cycles, or every 4 cycles. Lightly mutates one or two inner unlocked chords at those boundaries. Dimmed in CLIP mode. |
| | **Bus Channel** | A, B, C or D. The global harmony bus letter that dooblr-chord-receiver devices listen to. |
| SOURCE panel | drop zone | Drop a `.mid` file to load it as the source. The path is saved with the Set. |
| | **USE CLIP** | Loads the MIDI clip shown in the Detail View (else the clip in the highlighted slot). Uses the loop if looping, else start to end marker. |
| | CLEAR | Forgets the source. Generate makes fresh progressions again. |
| | **VAR / B** | Variation or B section of the source. Dimmed without a source. |
| | info line | Source name, detected key, chord and bar count, or a hint while there is no source. |

In CLIP mode, changing a control updates the preview but does not write
anything. Only Generate, Reroll and Write create clips. In LIVE mode those
same changes apply at the next chord (Generate / Reroll at the next bar)
and no clip is written. Clips are named like
`C min · 4 · TRI · Col35 Chr10`, or `riff · VAR · A min · 9th · Col35 Chr10`
and `riff · B III · A min · TRI · Col35 Chr10` when made from a source. Loading a source
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
  lead.js        monophonic melody over a progression
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
   `C min · 4 · TRI · Col35 Chr10`, 4 bars long. It plays the chords shown in the
   blocks, and the Seed changes.
4. Press **Generate** again. The next empty slot gets a new clip, and earlier
   clips are untouched.
5. Set Length 8 with ½ bar per chord. The clip is 4 bars with 8 chords. With
   Length 12 the blocks wrap onto two rows.
6. Set Chromatic to 0 and generate several times. Only diatonic chords
   appear. At 80 or more, secondary dominants (V/x), subV/x, borrowed chords
   and N show up.
7. Color colours the tones (sus, alterations) but does not change how many
   notes each chord has. With Notes on TRI, Color 100 still gives 3-note
   chords; Spice still adds borrowed chords and substitutions.
8. Notes defaults to **TRI**. Every block is a triad (or sus), and the clip
   name includes `TRI`. With Bass on, a triad still has three sounding notes. Set it to **7TH**: every chord has four sounding notes
   and names such as Am7 or Cmaj7. **9TH** gives five (Am9, Cmaj9, G9).
   **11TH** gives six (Am11, Fmaj9#11, G13 — a clashing natural 11 becomes
   #11 or 13). Turn Lead on: Notes dims, with a tooltip saying why.
9. Click a block. It gets a coloured outline and a padlock, and Reroll
   lights up. Press **Reroll**. Locked chords stay, the others change, and a
   new clip is written.
10. Press **LEAD**. It lights up gold and Generate reads **Generate Lead**.
   Press Generate: the clip is named `Lead · …`, it is monophonic, and the
   blocks still show the underlying chords under a LEAD header. Open
   ADVANCED: Density is available. Turn Lead off: Density dims and Generate
   writes chords again.
11. Type a Seed you saw earlier, then press **Write**. It writes the identical
   progression.
12. Fill every slot on the track and press Generate. The status reads "Every
    clip slot on this track is full…" and nothing else happens.
13. Put the device in a MIDI Effect Rack and press Generate. It still writes
    to the track's slots.
14. Save the Set, reopen it and check that the controls are restored. Locks
    are not restored.
15. Check that the ♭ and ♯ signs show in the blocks. If they show as boxes,
    send the js the message `ascii 1`.
16. The device is 400 px wide. The main view shows the left rail (CLIP/LIVE, Generate, TO CLIP, LEAD,
    Reroll), Key, Length, Notes, Color, Spice and the chord blocks, with ADVANCED …
    and SOURCE … in the top-right. The Advanced and Source controls are not
    on the main view. Press ADVANCED …: a panel covers the chord display,
    with a title and a close (×). The Max window posts "dooblr-chords: panel
    open advanced". Bars per Chord, the voicing and output dials, Bass, Seed
    and Write are all inside it and none overlap. The key, Notes, tension knobs and
    chord blocks underneath are gone (moved off-screen), not just covered.
    Close it with × or by pressing ADVANCED … again; the Max window posts
    "dooblr-chords: panel closed". Press SOURCE …: the drop zone, USE CLIP,
    CLEAR, VAR/B and the info line are in that panel. VAR/B and CLEAR are
    dimmed. Close it, reload the Set: both panels start closed. Hover over
    the blocks: the hint ends with the full status text.
17. Drag a `.mid` file with block chords (for example Am F C G, one bar each)
    from Finder onto the drop zone. The status reads "Loaded <name>: A Natural
    Minor, 4 chords…", Root/Mode/Length change to A, Natural Minor and 4, the
    SOURCE strip shows `Am F C G`, the heading reads VARIATION, and no clip is
    written. Drop a `.wav` and a damaged `.mid`: each gives a readable warning
    and the old source stays.
18. Press **Generate**. The clip is named `<name> · VAR · A min · …`, has the
    source's length and rhythm, starts on Am and ends on G. Changed chords
    have an orange dot. Generate a few times: different inner chords.
19. Switch to **B** and press Generate. The heading reads B SECTION with a key
    area such as "in C major (III)", the chords differ from the source, and
    playing the B clip then the source clip sounds like a return home.
20. In B, lock a chord and press Reroll several times. The locked chord and
    the chord count stay the same.
21. Select a MIDI clip in Session View (Detail View showing its notes),
    including one with a loop that does not start at 1.1.1, and press **USE
    CLIP**. The source is the looped part. Select an audio clip: the status
    says so. Deselect all clips: "No clip is selected…".
22. Press **CLEAR**. The heading returns to PROGRESSION and VAR/B dims.
23. Load a file, save the Set, reopen it. The file source comes back (if the
    file still exists) and the saved Root/Mode/Length are kept. A USE CLIP
    source is not saved.
24. Try a source with arpeggios or a melody on top of the chords, and one in
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
* The 0.8.1 restore/LiveAPI hardening: saved live.* parameters stay in the
  parent patcher (the Advanced window uses `parameter_enable 0` UI clones),
  `live.thisdevice` defers observer bind, clock start and any `set` on
  parameters by 250ms (and retries bind if the Live Set is not yet available),
  LiveAPI `set`/`call` never runs inside an observer callback (`defer` /
  `flushDefer` / `inNotification` guard, including clip writing), and init
  no longer writes Spice from Color/Chromatic into the Set. Replacing a
  loaded `.amxd` on disk while Live is running is still unsafe; quit Live
  first.
* The 0.8.0 harmony bus and TO CLIP in Live: global `send`/`receive`
  `dooblr_chords_bus` across devices on different tracks, hello/request
  latency, CLIP-mode chord-from-transport tracking, Bus Channel in the
  Advanced panel, and TO CLIP writing the sounding cycle (after Evolve)
  without glitching live MIDI. Global send/receive is the standard Max
  path between M4L devices in one Live set; `---` names would stay local
  to one device and were not used.
* The 0.7.0 LIVE mode in Live: a 10ms `Task` plus `LiveAPI` `live_set`
  (`is_playing`, `current_song_time`, `tempo`, `signature_numerator`,
  `signature_denominator`) staying in phase with the transport, starting on
  a bar line, looping over Length × Bars per Chord, applying knob changes
  at the next chord and Generate/Reroll at the next bar, Evolve mutations
  at cycle wraps, note-offs before note-ons with no stuck notes on stop /
  mode switch / device off or delete (`notifydeleted`, `closebang`,
  `is_active`), the teal (or gold-with-Lead) playhead, the New Progression
  label, Write dimming, LEAD / VAR / B playing live, incoming MIDI still
  passing through `midiin` → `midiout` alongside outlet 2 of the js object,
  and `Task` availability in Max 8.5.8's classic `js` object. Same-pitch
  collisions between a user-held passthrough note and a generated note are
  possible; the device never sends CC 123 so it will not flush notes you
  are holding. Hold-a-note-to-transpose was skipped as too risky.
* The 0.6.2 Notes count in Live: each chord writing exactly N sounding MIDI
  notes (Bass included), triad inversions staying three notes, notes not
  overlapping the next chord, plus the 0.6.1 tab labels, default Triad,
  Color not changing the count, block and clip names (Cmaj7, Am9, Fmaj9#11,
  G13), and Notes dimming when Lead is on.
* The 0.5.0 Lead toggle in Live: the large gold on-state, the Generate label
  changing to "Generate Lead", Density dimming when Lead is off, and the
  monophonic lead clip sounding over the shown chords.
* The 0.4.1 overlays in Live: moving objects with `presentation_rect` from
  js (`obj.message("presentation_rect", …)` and `thispatcher script sendbox`)
  so a panel comes on-screen and the main-view controls it covers go to x=2000,
  `patcher.bringtofront` / `sendtoback` for the backdrop versus its members,
  `live.text` mode-1 openers (`set` may echo; the js ignores repeats),
  `live.drop` only while the Source panel is open, `live.text` buttons with
  `parameter_enable` 0, the × and ellipsis glyphs, Color/Spice captions with
  `showname` off, 8.5 pt button labels, and how the 400 px device sits in
  Live's device chain. `scripts/render-layout.py` only approximates Live's
  widgets.
