# dooblr-generate

Type what you want to hear ("bright clap", "909 kick", "ragtime piano note"), press **Generate**,
and a text-to-audio model running on your own Mac makes a one-shot sample. The plug-in then plays
it as a sampler from MIDI, and you can drag the WAV straight into Ableton. Every generation is kept
in a history you can go back to.

- VST3 instrument (MIDI in, stereo out), version **0.1.0**, plug-in code `Dbgn`, bundle
  `com.dooblr.dooblrgenerate`.
- Generation is **offline**: press Generate, wait a few seconds, then play. It never runs on, or
  waits for, the audio thread.
- The model runs in a **separate worker process** (`worker/dooblr_generate_worker.py`), never inside
  Live. If the model crashes, hangs or runs out of memory, the job fails with a copyable error and
  Live keeps running. **Cancel** kills the worker (and anything it started) at once.
- No model weights are part of this repository, the plug-in or the installer. The plug-in downloads
  them on request, or you point it at a folder you downloaded yourself.
- Not in the dooblr installer or the release workflow yet.

## Models

| model (id) | licence | access | output | notes |
|---|---|---|---|---|
| **Stable Audio Open Small** (`stable-audio-open-small`), [stabilityai/stable-audio-open-small](https://huggingface.co/stabilityai/stable-audio-open-small) | [Stability AI Community License](https://huggingface.co/stabilityai/stable-audio-open-small/blob/main/LICENSE): free for research, non-commercial and commercial use while you (and affiliates) make **under US $1M a year**; commercial use requires [registering with Stability AI](https://stability.ai/community-license); above $1M you need an enterprise licence. If you distribute it, ship the licence, a "Notice" file and show "Powered by Stability AI". You own your outputs. | **Gated** on Hugging Face (accept the terms with a Hugging Face account; approval is automatic), so a token is needed to download | stereo 44.1 kHz, up to 11 s, 8 steps | ~0.5 B parameters; the download is `model.safetensors` (1.68 GB) plus the config; the T5-base text encoder (~0.9 GB, open) is fetched on first use. The default model, built for short sounds. Wired up but **not yet run here**: the weights need Dan's Hugging Face access (see below). |
| **AudioLDM S v2** (`audioldm-s-full-v2`), [cvssp/audioldm-s-full-v2](https://huggingface.co/cvssp/audioldm-s-full-v2) | CC BY-NC-SA 4.0: **non-commercial use only** | open, no account | mono 16 kHz, up to 10 s, 25 steps | ~1.7 GB (safetensors only). Works today without any account; lower fidelity (16 kHz). Labelled "(non-commercial)" in the plug-in. |
| Test backend (`test-backend`) | — | — | stereo 44.1 kHz | **Not AI.** A seeded synthetic burst for checking the plug-in without a model. Hidden unless `DOOBLR_GENERATE_TEST_BACKEND=1`; its results are marked "TEST BACKEND (not AI)" everywhere. |

Why a worker process with PyTorch (MPS) for now: it is the reference way to run both models, it
uses the Apple Silicon GPU through Metal, and a separate process is the only way to make a
multi-GB model crash-proof for the DAW. Stability and Arm also publish LiteRT (TFLite) exports of
Stable Audio Open Small (in the same gated repo, `tflite-arm-path/`), which is the candidate for a
lighter, Python-free C++ worker later; ONNX Runtime (CoreML) and MLX would need our own
conversion of the diffusion transformer and are not used.

## Setup

1. **Python runtime** (once, no sudo):
   `dooblr-generate/scripts/setup-runtime.sh` creates a virtual environment in
   `~/Library/Application Support/dooblr/generate/runtime/venv` with PyTorch, diffusers and
   stable-audio-tools. The plug-in looks for a Python in this order: `"python"` in
   `settings.json` (if set, only that one), `$DOOBLR_GENERATE_PYTHON`, that venv, then
   `~/dooblr-build/generate-venv`. The macOS / Xcode / Command Line Tools Python
   (`/usr/bin/python3` and friends) is **never** used; without a runtime the MODEL panel reads
   "Python runtime not set up" and nothing is launched.
2. **AudioLDM** (no account): choose it in the Model box and press **Download** (~1.7 GB).
3. **Stable Audio Open Small** (gated). Only you can do this part:
   1. Sign in at huggingface.co, open
      <https://huggingface.co/stabilityai/stable-audio-open-small> and accept the Stability AI
      Community License.
   2. Create a **read-only** token at <https://huggingface.co/settings/tokens>.
   3. In dooblr-generate press **Token...**, paste it, **Save**, then **Download**.
   The token is stored only in `~/Library/Application Support/dooblr/generate/hf_token`
   (permissions 0600) and handed to the worker as a file path, never on a command line.
   Alternatively download `model_config.json` and `model.safetensors` yourself and pick their
   folder with **Folder...**.

Files live in `~/Library/Application Support/dooblr/generate/`: `models/<id>/` (weights),
`hf-cache/` (the T5 text encoder Stable Audio fetches on first use), `history/` (every generated
WAV plus `history.json`), `runtime/` (the worker script, rewritten from the plug-in when it changes)
and `settings.json`. The plug-in never deletes a WAV.

## Using it

- **Prompt**, **Seed**, **Length** (0.25–10 s, capped at the model's maximum), **Generate**
  (Return also works; becomes **Cancel** while running) and **Variation** (a new random seed, then
  generate). Same prompt + seed + model gives the same result.
- The line under the prompt bar shows the stage and elapsed time, then "Generated in x s".
- **Waveform**: the current sample, with the Start point as a line. **Drag the waveform** (or the
  **Drag WAV** button) into an Ableton track, Simpler or Finder to drop a copy of the WAV; a click
  on Drag WAV shows the file in Finder. **Play** auditions it at the root key.
- **HISTORY**: newest first; click to load (restores its prompt and seed), double-click to load and
  play, drag a row out to drag its WAV.
- **MODEL**: status (Ready / Not installed / Downloading x% / Needs Hugging Face access / Python
  runtime not set up / Download failed), Download (Cancel while downloading), Token..., Folder...,
  and **Copy**, which copies the details box: a report with the plug-in version, OS, step, model
  and message for any failure, or the missing files for a partial model. The details text is also
  selectable.

## Parameters

All seven are normal VST3 parameters: they appear in Ableton's device parameter list, can be
automated, mapped to Macro / MIDI controls and are saved with the set.

| name | id | range | default | notes |
|---|---|---|---|---|
| Tune | `tune` | -24..+24 st (integer) | 0 st | Transposes every note. |
| Fine | `fine` | -100..+100 ct | 0.0 ct | |
| Start | `start` | 0..95 % | 0.0 % | Where playback starts in the sample (shown on the waveform). |
| Attack | `attack` | 0..1000 ms (skewed, centre 50 ms) | 0 ms | Linear fade-in. |
| Decay | `decay` | 10 ms..20 s (skewed, centre 1 s); max = **Off** | Off | Time to fall 60 dB after the attack; Off plays the sample out at full level. |
| Velocity | `velocity` | 0..100 % | 100 % | Velocity sensitivity (0 %: every hit at full level). |
| Output | `output` | -48 (= -inf)..+12 dB | 0.0 dB | Smoothed. |

Playback: 16-voice one-shot sampler (note-offs are ignored, like a drum sampler; All Notes Off
stops everything; the oldest voice is stolen). The root key is **C3 (MIDI 60)**: C3 plays the
sample at its own pitch and speed, other keys repitch it. Sample-rate conversion is automatic.

Saved with the set but not automatable: prompt, seed, length, model, and the current sample
(by its history id, falling back to the WAV path). If the WAV is gone the settings still restore.

## Build and install

```sh
dooblr-generate/scripts/build.sh   # builds in ~/dooblr-build/dooblr-generate, runs the tests,
                                   # installs /Library/Audio/Plug-Ins/VST3/dooblr-generate.vst3,
                                   # then loads the installed bundle like a host (LoadCheck)
```

Same environment variables as the other plug-ins' `build.sh` (`BUILD_DIR`, `JUCE_SOURCE_DIR`,
`CONFIG`, `RUN_TESTS`, `INSTALL_VST3`, `LOAD_CHECK`, `JOBS`). JUCE 8.0.15.

Tests (`Tests/GenerateTests.cpp`, 13 tests) cover metadata and parameters, value text, the
sampler (timing, pitch, sample-rate conversion, Start, Attack, Decay, Velocity, Output, one-shot
behaviour, voice stealing), MIDI playback through the processor, the lock-free sample hand-over,
the history, state save / restore, model status states, token storage, worker-script embedding,
generation end to end, worker crash / error / hang / cancel, a partial model, the audio thread
during generation (no allocation, timing) and the editor at 940x600, 1080x660 and 2400x1400.
**The tests never start Python**: a stub shell script stands in for the worker. Set
`DOOBLRGENERATE_EDITOR_SNAPSHOT=/path/prefix` to write editor PNGs, and
`DOOBLRGENERATE_LIVE_PROMPTS="a|b"` to generate with the real model (not counted as a test).

Not tested yet: inside Ableton Live (loading, MIDI, drag-out into a track), and Stable Audio Open
Small itself (needs the gated weights).

No third-party artwork or logos are used. Model names are used only to identify the models.
