# dooblr-generate: design notes

Read `design/INDEX.md` first. This file has two parts:

- **Hand-written notes** (this section): intent, behaviour, UX rules and decisions that the code
  cannot tell you. Update them in the same commit as any behaviour change.
- **Generated map** (between the markers below): rewritten by `design/tools/update-design.py`
  from the staged source on every commit that touches `dooblr-generate/`. Do not edit it by hand.

## Notes (hand-written)

- Suite-wide rules: `design/dooblr-design-system.md`. User docs: `dooblr-generate/README.md`.
- **What it is**: text prompt -> local text-to-audio model -> one-shot sample, played by a
  16-voice one-shot sampler (root key C3 = MIDI 60, note-offs ignored). Offline generation only.
- **Process model (the key decision)**: models never load inside the DAW. `GenerationService`
  (its own thread) spawns the worker (`worker/dooblr_generate_worker.py`, embedded with
  `juce_add_binary_data` and written to `<support>/runtime/`) with `posix_spawn`: own process
  group, `POSIX_SPAWN_CLOEXEC_DEFAULT` (no host fds leak), stdin `/dev/null`, stdout+stderr on one
  pipe read with `read()` so progress streams (juce::ChildProcess uses `fread`, which waits for a
  full buffer). Cancel = SIGKILL to the process group. One job at a time.
- **Worker protocol**: one JSON object per stdout line: `progress` {stage, fraction, message},
  `done` {...}, `error` {code, message}. Exit codes 0 ok, 2 args, 3 model missing, 4 HF access,
  5 runtime package missing, 6 generation failed, 7 download failed, 8 internal. The worker never
  calls `abort()`; uncaught exceptions become an `error` line + exit 8. No `done` line = "stopped
  unexpectedly (exit code / signal)", with the last 30 non-JSON lines in the report.
- **Interpreter**: settings.json "python" (exclusive) > `$DOOBLR_GENERATE_PYTHON` >
  `<support>/runtime/venv` > `~/dooblr-build/generate-venv`. System Pythons (`/usr/bin`,
  `/System`, Xcode, CommandLineTools) are always refused -> "Python runtime not set up".
- **Runtime self-check** (0.1.1): before any model work the worker imports the model's whole
  stack (`runtime_self_check`); a failure is `error` code `runtime`, exit 5, and the editor's
  progress line reads "Python runtime broken: run scripts/setup-runtime.sh". The working set is
  pinned in `worker/requirements-lock.txt` (NumPy 1.26.4 for PyWavelets 1.4.1's NumPy 1 ABI;
  pytorch_lightning 2.5.5 because the DiT imports it). One venv serves both models.
- **Pre-flight**: a generate for a real model whose folder lacks any required file fails before
  spawning anything ("not installed or incomplete. Missing in <folder>: <files>").
- **Audio thread hand-over** (`Engine/Sample.h` `SampleSlot`): writers publish under a lock and
  keep samples alive; the audio thread does load-published / store-inUse / re-check (Dekker-style,
  seq_cst) and never locks, allocates or frees. A sample swap stops voices of the old sample.
- **History** is per machine (`<support>/history/history.json`, newest first, max 500), shared by
  every instance; the plug-in never deletes WAVs. State stores the current sample by history id,
  falling back to the WAV path.
- **Models**: Stable Audio Open Small (default; gated, Stability AI Community License, < $1M
  revenue) and AudioLDM S v2 (open, CC BY-NC-SA, labelled non-commercial). Weights never in the
  repo or installer. Token file `<support>/hf_token` (0600), passed to the worker as a path.
  Test backend (not AI) hidden unless `DOOBLR_GENERATE_TEST_BACKEND=1`; always labelled.
- **UI** (one page, 940x600..2400x1400, default 1080x660): header (title, version, Model box);
  prompt bar with captions above (Prompt, Seed, Length, Generate/Cancel, Variation) and a progress
  line; SOURCE drop zone (left of the waveform): empty label "Drop sample", highlight on drag-over,
  then mini waveform / file name / length / Play / x / Amount when loaded (no hint text on the
  graph); waveform (no text on it; Start shown as a `dynamic`-coloured line; test samples drawn in
  textMuted) with Play / Drag WAV / info line; HISTORY list (right); PLAYBACK ControlPanel and
  MODEL panel (status, Download/Cancel, Token..., Folder..., Copy, read-only selectable details).
  Drag-out: waveform and Drag WAV call `performExternalDragDropOfFiles`; history rows go through
  `shouldDropFilesWhenDraggedExternally` on the editor. History rows can also be dropped on SOURCE.
- **Source Variation (0.2.0)**: a dropped sample (Finder audio, or a Live clip whose underlying
  file path is passed as a file drag) is copied into `<support>/source/` as 44.1 kHz stereo, trimmed
  to 11 s (and the UI says so). Persist the copy path, original name and trimmed flag in plugin
  state. Variation with a source loaded is audio-to-audio: the worker gets `--init-audio` and
  `--init-noise` (Amount 0–100 %, default 50 % → init_noise_level). Stable Audio Open Small uses
  `generate_diffusion_cond(..., init_audio, init_noise_level)`. AudioLDM's diffusers pipeline has
  no audio-to-audio/style-transfer API — Variation and Amount are disabled with that message,
  never faked. With no source, Variation is still a new random seed then text-to-audio. An
  Ableton `.alc` is accepted only when an absolute, still-present audio path can be read out of
  it; otherwise a short rejection. Results go into history and drag out as before.
- **Tests never start Python**: a `/bin/sh` stub speaks the protocol (ok / error / hang / SIGKILL
  crash) and the tests check no process is left behind. The audio-timing test runs its audio
  thread at QOS_CLASS_USER_INTERACTIVE like a host.
- **Known limits / next (milestone 2)**: persistent worker (keep the model loaded between
  generations; today each job reloads it plus Python start-up, ~5 s of the ~10-15 s); LiteRT C++ worker instead of Python; ship the runtime via
  the installer; in-Live validation; trim / normalise / reverse tools; root-key parameter;
  AudioLDM source variation if diffusers ever exposes a real audio-to-audio path.

<!-- BEGIN AUTO-GENERATED by design/tools/update-design.py: do not edit between these markers -->
## Generated map

- Kind: JUCE instrument VST3 (target `DooblrGenerate`, product "dooblr-generate", code Dbgn)
- Version: 0.2.0
- Last source change: 2026-11-06
- Source tree: `ee996b17c42d` (stale check: compare with `git rev-parse --short=12 HEAD:dooblr-generate`)
- Window: resize limits minWidthxminHeight..2400x1400; default size 1080x660
- Build/test: `dooblr-generate/scripts/build.sh` (build dir `~/dooblr-build/dooblr-generate`), runs the tests, then installs the VST3 to `/Library/Audio/Plug-Ins/VST3`
- Build/test: target `DooblrGenerateTests` (Tests/CMakeLists.txt)
- Build/test: target `DooblrGenerateLoadCheck` (Tools/LoadCheck/CMakeLists.txt)

### Parameters (8 rows, 8 parameters after loops; from `Source/Parameters.cpp`)

`{n}` = index or name filled in by a loop; `x` = loop condition or repeat count.

| id | type | name | range | default | x |
|---|---|---|---|---|---|
| output | float | Output | -48..12 step 0.01 | 0 |  |
| tune | int | Tune | -24..24 | 0 |  |
| fine | float | Fine | -100..100 step 0.1 | 0 |  |
| start | float | Start | 0..95 step 0.1 | 0 |  |
| attack | float | Attack | skewed(0, 1000, 50, 0.01) | 0 |  |
| decay | float | Decay | skewed(10, 20000, 1000, 0.1) | 20000 |  |
| velocity | float | Velocity | 0..100 step 0.1 | 100 |  |
| variationAmount | float | Variation Amount | 0..100 step 0.1 | 50 |  |

### Source map

| file | role |
|---|---|
| `CMakeLists.txt` | CMake build: DooblrGenerate, DooblrGenerateWorkerData |
| `README.md` | Readme: dooblr-generate |
| `Source/Engine/Sample.{h,cpp}` | One loaded generation: the audio plus where it came from. [Sample, SampleSlot] |
| `Source/Engine/Sampler.{h,cpp}` | Playback settings, read from the parameters once per block. [PlaybackSettings, Sampler, Voice] |
| `Source/Generation/GenerationService.{h,cpp}` | Runs model work in a separate worker process (worker/dooblr_generate_worker.py, embedded in the plug-in... [ModelStatus, GenerateRequest, JobStatus, GenerationService, ...] |
| `Source/Generation/History.{h,cpp}` | One past generation: a WAV in the history folder plus how it was made. [HistoryEntry, History] |
| `Source/Generation/ModelCatalog.{h,cpp}` | A model dooblr-generate can run (through the worker). [ModelInfo] |
| `Source/Generation/Paths.{h,cpp}` | Where dooblr-generate keeps its files: ~/Library/Application Support/dooblr/generate/ models/<model-id>/... [Paths] |
| `Source/Generation/SourceAudio.{h,cpp}` | A dropped source sample, copied into the plug-in's working folder as 44.1 kHz stereo (trimmed to the... [SourceAudio] |
| `Source/Parameters.{h,cpp}` | The automatable parameters (all show up in Ableton's device parameter list) and the non-automatable... |
| `Source/PluginEditor.{h,cpp}` | One page: a prompt bar (prompt, seed, length, Generate, Variation) with the job's progress under it; a... [DooblrGenerateEditor] |
| `Source/PluginProcessor.{h,cpp}` | dooblr-generate: types a description ("bright clap", "909 kick"), runs a local text-to-audio model in a... [DooblrGenerateProcessor] |
| `Source/UI/ControlPanel.{h,cpp}` | A titled group of parameter controls laid out left to right (wrapping): rotary knobs, pill toggles and... [ControlPanel, Spec, Item, SegmentedToggle] |
| `Source/UI/HistoryList.{h,cpp}` | A short label for a model id ("Stable Audio", "AudioLDM", "TEST"). [HistoryList] |
| `Source/UI/SourceDropZone.{h,cpp}` | Drop zone for a source sample used by Variation. [SourceDropZone] |
| `Source/UI/Theme.{h,cpp}` | Flat, understated look for knobs, buttons and combo boxes. [DooblrLookAndFeel] |
| `Source/UI/WaveformView.{h,cpp}` | The current sample's waveform (min/max per pixel column, both channels overlaid) with the Start point as a... [WaveformView] |
| `Tests/CMakeLists.txt` | CMake build: DooblrGenerateTests |
| `Tests/GenerateTests.cpp` | dooblr-generate console tests: the real processor, sampler, sample hand-over, history, generation service... |
| `Tools/LoadCheck/CMakeLists.txt` | CMake build: DooblrGenerateLoadCheck |
| `Tools/LoadCheck/LoadCheck.cpp` | DooblrGenerateLoadCheck: loads a built or installed dooblr-generate.vst3 the way a host does. |
| `cmake/sign-bundle.sh` | Ad-hoc signs a plug-in bundle so `codesign --verify --deep --strict` passes. |
| `scripts/build.sh` | Configure and build the dooblr-generate VST3, run the tests, and install the built bundle into the VST3... |
| `scripts/setup-runtime.sh` | Sets up the Python runtime the dooblr-generate worker runs models with: a virtual environment with PyTorch... |
| `worker/dooblr_generate_worker.py` | dooblr-generate worker: runs text-to-audio models in a separate process for the dooblr-generate plug-in,... |
| `worker/requirements-lock.txt` | dooblr-generate worker runtime: the exact, tested package set (macOS arm64, Python 3.11). |

### UI components

- `DooblrGenerateEditor` : AudioProcessorEditor, Timer (PluginEditor.h)
- `ControlPanel` : Component (ControlPanel.h)
- `SegmentedToggle` : Component (ControlPanel.h)
- `HistoryList` : Component (HistoryList.h)
- `SourceDropZone` : Component (SourceDropZone.h)
- `DooblrLookAndFeel` : LookAndFeel_V4 (Theme.h)
- `WaveformView` : Component (WaveformView.h)

### Tests

- `Tests/GenerateTests.cpp` (14): Metadata, ValueText, Sampler, ProcessorMidi, SampleSlot, History, StateRoundTrip, ModelStatus, SourceSample, WorkerScript, GenerationEndToEnd, AudioThreadDuringGeneration, EditorLayout, EditorBehaviour
<!-- END AUTO-GENERATED -->
