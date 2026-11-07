# dooblr-plugins

## **[⬇ Download the dooblr installer for macOS](https://github.com/Dooblr/dooblr-plugins/releases/latest/download/dooblr-installer-mac.dmg)**

## **[⬇ Download the dooblr installer for Windows](https://github.com/Dooblr/dooblr-plugins/releases/latest/download/dooblr-installer-windows.exe)**

One app installs and updates every dooblr plug-in: tick the ones you want, and it shows what's
installed, what's new, and installs the VST3s (and the Max for Live devices) for you.
First launch on a Mac: right-click the app and choose **Open** (macOS 15+: System Settings >
Privacy & Security > **Open Anyway**); on Windows: **More info > Run anyway**. Details:
[`dooblr-installer/`](dooblr-installer/).

**The repository is private for now**, so these links work only while you are signed in to GitHub,
and on first launch the installer asks for a GitHub token (a fine-grained token with read-only
access to this repository) before it can get the plug-in list: **Get access on GitHub** opens GitHub's
token page already filled in, and the installer picks up the copied token. It keeps the token in the macOS
Keychain / Windows Credential Manager. Steps: [`dooblr-installer/` > First launch](dooblr-installer/#first-launch-private-repository).

Monorepo for the **dooblr** audio plug-in suite by Dan Feinstein: VST3 plug-ins for macOS
(arm64, macOS 11+), built with C++20, CMake and JUCE 8.0.15. Each plug-in is self-contained
in its own folder and has its own README.

| Folder | Plug-in | Version | Type | Status |
| --- | --- | --- | --- | --- |
| [`dooblr-q/`](dooblr-q/) | dooblr-q | 0.9.5 | Dynamic parametric EQ (VST3 effect) | Mature; most features, see its README |
| [`dooblr-kick/`](dooblr-kick/) | dooblr-kick | 0.3.8 | Layered kick drum designer (VST3 instrument) | Active |
| [`dooblr-vocals/`](dooblr-vocals/) | dooblr-vocals | 0.2.0 | Pitch correction and vocal chain (VST3 effect + Standalone) | In development; limiter moved to a fixed last stage after the time FX (2026-10-31) |
| [`dooblr-prophet/`](dooblr-prophet/) | dooblr-prophet | 0.3.1 | Analog-modelled polyphonic synth, Prophet-5 / 6 / 10 style (VST3 instrument) with a Max for Live panel **0.3.2** | Active; panel 0.3.2 pairs without MIDI when Live drops the token CCs |
| [`dooblr-bass/`](dooblr-bass/) | dooblr-bass | 0.1.0 | Analog-modelled monophonic bass synth (VST3 instrument), one-page Mini-style board: two main oscillators, sub, optional third/lo-freq osc, mixer drive/overload, 24 dB/oct transistor ladder | New; its tests pass in this repo |
| [`dooblr-generate/`](dooblr-generate/) | dooblr-generate | 0.1.1 | Text-to-sample generator (VST3 instrument): type "909 kick", a local model (Stable Audio Open Small, or AudioLDM for non-commercial use) runs in a separate worker process on the Mac's GPU, then the result plays as a one-shot sampler from MIDI; drag the WAV into Ableton; history of every generation. Model weights are downloaded on request, never bundled | New; tests and LoadCheck pass; Stable Audio Open Small and AudioLDM generate on the Mac (Stable Audio needs your own Hugging Face access); not yet tested in Live; not in the installer yet |
| [`dooblr-soothe/`](dooblr-soothe/) | dooblr-soothe | 0.1.3 | Dynamic resonance reducer (VST3 effect): Amount hero knob, weighting curve over the spectrum, A / B, Advanced panel, smooth low-end display, Analyzer speed setting | Early; its tests and LoadCheck pass, not yet tested in a DAW |
| [`dooblr-feedback/`](dooblr-feedback/) | dooblr-feedback | 0.1.1 | Automatic live feedback suppressor (VST3 effect, optional reference-mic sidechain): zero-latency notches (one per howl, up to 20 kHz, Max cut to ∞), calm markers on the spectrum with lock / clear, Safety duck, notch table, room notes and room presets | Early; its tests and LoadCheck pass, not yet tested live or in a DAW |
| [`dooblr-distressor/`](dooblr-distressor/) | dooblr-distressor | 0.1.0 | EL8-style compressor (VST3 effect): eight ratio curves, Dist 2 / Dist 3, detector HP / band / link, audio HP, headroom, phase-aligned mix; independently designed approximation, not an emulation | New; its tests pass in this repo |
| [`dooblr-0coast/`](dooblr-0coast/) | dooblr-0coast | 0.1.0 | No-coast monophonic synth (VST3 instrument): triangle-core oscillator, Overtone / Multiply, cycling Slope, Contour, LPG Dynamics, host-tempo arp, virtual patch bay; independently designed approximation of the 0-Coast architecture, not an emulation | New; its tests pass in this repo |
| [`dooblr-chords/`](dooblr-chords/) | dooblr-chords | 0.3.0 | Chord progression generator (Max for Live MIDI effect, Live 11.3 / Max 8.5) | Active; `npm test` suite |
| [`dooblr-installer/`](dooblr-installer/) | dooblr installer | 0.1.4 | Installer / updater app for the whole suite (macOS dmg, Windows exe): picks what to install or update (All / None / Updates), one progress wheel with a summary at the end, sha256-checked downloads from the latest GitHub Release; while the repository is private, "Get access on GitHub" opens a pre-filled token page and the copied token is checked with GitHub before it is saved; errors can be selected and copied | New; Mac tests and a local end-to-end install pass; Windows built and tested in CI (not yet on a real PC) |
| [`design/`](design/) | Design files | — | Shared visual design system for the suite, including reusable patterns (§12), dooblr-feedback (§13) and the installer (§14) | Updated 2026-11-01 |

Versions come from each plug-in's `project(... VERSION ...)` in its `CMakeLists.txt` (dooblr-chords:
its README / device). Status is as of the last commit; see each folder's README and ROADMAP.

## Building

VST3 plug-ins (each with `scripts/build.sh`): dooblr-q, dooblr-kick, dooblr-vocals,
dooblr-prophet, dooblr-bass, dooblr-soothe, dooblr-feedback, dooblr-distressor, dooblr-0coast, dooblr-generate. Max for Live devices: dooblr-chords (see its README) and the
dooblr-prophet panel 0.3.2 (`dooblr-prophet/m4l/`).

Each VST3 plug-in has a `scripts/build.sh` that configures, builds, runs the tests, runs a
host-style LoadCheck, and installs the VST3 bundle (never with sudo). Build directories live
**outside** the repo (and outside Dropbox), in `~/dooblr-build/<plugin>`:

```sh
dooblr-q/scripts/build.sh        # builds in ~/dooblr-build/dooblr-q
dooblr-kick/scripts/build.sh     # builds in ~/dooblr-build/dooblr-kick
dooblr-vocals/scripts/build.sh   # builds in ~/dooblr-build/dooblr-vocals
dooblr-prophet/scripts/build.sh  # builds in ~/dooblr-build/dooblr-prophet
dooblr-bass/scripts/build.sh     # builds in ~/dooblr-build/dooblr-bass
dooblr-soothe/scripts/build.sh   # builds in ~/dooblr-build/dooblr-soothe
dooblr-feedback/scripts/build.sh # builds in ~/dooblr-build/dooblr-feedback
dooblr-distressor/scripts/build.sh # builds in ~/dooblr-build/dooblr-distressor
dooblr-0coast/scripts/build.sh   # builds in ~/dooblr-build/dooblr-0coast
dooblr-generate/scripts/build.sh # builds in ~/dooblr-build/dooblr-generate
```

Useful environment variables (see the top of each `build.sh`):

- `BUILD_DIR`: build directory (default `~/dooblr-build/<plugin>`)
- `CONFIG`: CMake configuration (default `Release`)
- `RUN_TESTS=0`: skip the tests; `INSTALL_VST3=0`: don't install; `LOAD_CHECK=0`: skip LoadCheck
- `JUCE_SOURCE_DIR` (dooblr-vocals, dooblr-prophet, dooblr-bass, dooblr-soothe,
  dooblr-feedback, dooblr-distressor, dooblr-0coast): JUCE 8.0.15 source; defaults to
  `~/dooblr-build/_deps/juce-src` when it exists

Without the script, the build is plain CMake. JUCE 8.0.15 is fetched into the build directory:

```sh
cmake -S dooblr-q -B ~/dooblr-build/dooblr-q -DCMAKE_BUILD_TYPE=Release
cmake --build ~/dooblr-build/dooblr-q --config Release
```

Requirements: Xcode command-line tools, CMake 3.22+, and optionally Ninja.

## Design files

- [`design/dooblr-design-system.md`](design/dooblr-design-system.md): the suite's design
  system (layout, controls, colour, typography and interaction rules).
- [`design/dooblr-design-tokens.json`](design/dooblr-design-tokens.json): design tokens
  (colours, sizes, spacing) used by the plug-in UIs.
- Reusable patterns (design system §12): left-column hero knob, vertical dot-slider strip,
  toolbar with preset / A-B / undo, weighting curve overlaid on the spectrum with pill / dot
  handles, smooth log-frequency spectrum drawing, value-text formatting.

These are copies; the working originals are in `~/Dropbox/grok_plugin_design_files`.

## History

Each plug-in's git history was imported into its subfolder.

- **dooblr-q**: full history.
- **dooblr-vocals**: full history.
- **dooblr-kick**: partial history. Commits before v0.3.0 were lost when an old iCloud copy
  of the repo was damaged, so v0.3.0 is the root commit here. v0.3.2 could not be recovered
  (its source tree was missing), so its changes are folded into the v0.3.3 commit.

All original design, artwork and sounds. No third-party plug-in artwork, branding, samples or
presets are included.
