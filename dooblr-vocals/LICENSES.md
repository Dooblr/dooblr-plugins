# Licences

dooblr-vocals is original software by Daniel Feinstein. The look, DSP, presets and
UI are written for the dooblr suite. No FabFilter, D16, Antares, Celemony or Sonic
Academy branding, artwork, samples or code is used.

## This plug-in

All original source in this repository (DSP, UI, presets, tests, build scripts) is
copyright Daniel Feinstein. See the project README for use.

The design tokens (palette, typography, layout constants) are shared with
dooblr-q and dooblr-kick and are original to the suite.

There are no embedded third-party samples or bitmaps.

## JUCE

JUCE 8.0.15 is fetched at build time from https://github.com/juce-framework/JUCE
under the JUCE Licence / AGPLv3, depending on how you obtain it. This project
pins that release via CMake FetchContent; it does not vendor JUCE sources.

## Public-domain / CC0 code ideas

Pitch detection follows the YIN algorithm (de Cheveigné & Kawahara, 2002). The
implementation here is original C++ and is not a copy of any particular library.
The shifter is a TD-PSOLA / resampled-grain implementation written for this
plug-in.
