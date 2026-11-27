# dooblr-chord-receiver 0.1.0

A Max for Live MIDI effect that snaps incoming notes to the harmony a
[dooblr-chords](../dooblr-chords) sender is currently playing. Drop it in front
of an instrument on another track, set the same Channel as the sender, and
play — the receiver conforms the MIDI in real time.

Requires **Live 11.3.43** and **Max 8.5.8**. The classic `[js]` / `[jsui]`
objects only accept ES5.

## How it talks to the sender

Both devices share a global Max send/receive named `dooblr_chords_bus`.
Messages are a single JSON string (`harmony`, `hello`, or `bye`) and are
tagged with a Channel **A–D**, so several senders can live in one Set without
colliding. A receiver only listens to a sender on the same channel.

The sender broadcasts on every chord change and on a short keep-alive, and
answers a receiver’s `hello` immediately so a newly loaded device syncs
without waiting for the next chord.

If no matching sender has been heard for about a second, the receiver falls
back to passthrough (notes leave unchanged) and shows **NO SENDER**.

## Controls

| Control | What it does |
| --- | --- |
| **Channel** A–D | Bus channel. Must match the sender’s Bus Channel. |
| **Status** | Teal light and the current chord name (for example `Am9`) when a sender is connected; dim **NO SENDER** otherwise. |
| **Role** | **TONES** — nearest chord tone (including extensions). **SCALE** — nearest scale degree. **BASS** — the advertised bass / slash pitch class, in the incoming note’s MIDI octave window. **FOLLOW** — transpose a riff 0–11 semitones upward from the Reference root to the current chord root. |
| **Snap** | **NEAREST** (ties go down), **UP**, or **DOWN**. The original octave is kept unless Range is on. |
| **Retrig** | When **ON**, held notes are re-snapped (old pitch off, new pitch on) when the chord or a mapping setting changes. **OFF** leaves held notes until you release them. |
| **Ref** | Reference root used only by FOLLOW. |
| **Range** | When **ON**, snapped pitches wrap into one inclusive octave starting at Register. |
| **Register** | Bottom of that octave window (MIDI note name). |

Velocity is never changed. Non-note MIDI (CC, pitch bend, program change, and
so on) is passed through by the patcher and never reaches the snapper.
Incoming MIDI that is not a note is not remapped.

## Note bookkeeping

The receiver always turns off the pitch it actually sent, not the input pitch.
A reference count keeps two incoming notes that land on the same output from
cutting each other off. Held notes are flushed (all-notes-off of sent pitches
only, never CC 123) when you change Channel, disable or delete the device, or
stop the transport, and when the sender goes stale.

## Building

```
npm run verify
```

That runs the ES5 check, builds `device/dooblr-chord-receiver/` (the `.amxd`
plus `dooblr_chord_receiver.js` and `dooblr_chord_receiver_ui.js`), runs the
tests, and validates the patcher. The snap and protocol engines are shared
with dooblr-chords (`../dooblr-chords/src/engine`) and are concatenated into
the built controller at build time.

The device is 360 × 149 px and follows the dooblr design system (teal on the
dark Live rack background).
