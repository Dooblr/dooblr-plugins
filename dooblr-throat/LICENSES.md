# Licenses

dooblr-throat is original dooblr plugin code (processor, editor, tests, and the
real-time C++ port of the synthesis core). Factory presets and the editor UI
are original; they do not use third-party brand names.

## Physical-modelling vocal engine (port)

The real-time C++ synthesis core under `Source/DSP/` (airway FDTD, glottis,
articulatory geometry, vowel targets, decimator) is a port of the human-voice
engine from [CHIMERA](https://github.com/jamesondh/chimera-vocal-synth)
(crates/chimera), Copyright (c) 2026 Jameson Hodge, licensed under the MIT
License.

The original engine is not compiled or linked; this plugin is a C++
reimplementation of that technique for a VST3 instrument. The C++ code is not
bit-identical to the Rust original. Version 0.2.0 also ports the English
grapheme-to-phoneme, syllable, duration and gestural-planner path from the
same project (still MIT, same copyright).

The MIT License text of the original engine follows.

```
MIT License

Copyright (c) 2026 Jameson Hodge

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

This plugin does not bundle fonts or any neural model weights from that project.

## CMU Pronouncing Dictionary (CMUdict)

`Resources/cmudict.txt.gz` is the CMU Pronouncing Dictionary, used at run time
to map English words to phonemes. A copy of its license is in
`Resources/CMUDICT-LICENSE.txt`. The license text follows.

```
Copyright (C) 1993-2015 Carnegie Mellon University. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
   The contents of this file are deemed to be source code.

2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in
   the documentation and/or other materials provided with the
   distribution.

This work was supported in part by funding from the Defense Advanced
Research Projects Agency, the Office of Naval Research and the National
Science Foundation of the United States of America, and by member
companies of the Carnegie Mellon Sphinx Speech Consortium. We acknowledge
the contributions of many volunteers to the expansion and improvement of
this dictionary.

THIS SOFTWARE IS PROVIDED BY CARNEGIE MELLON UNIVERSITY ``AS IS'' AND
ANY EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL CARNEGIE MELLON UNIVERSITY
NOR ITS EMPLOYEES BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```


## JUCE

The plugin is built against [JUCE 8.0.15](https://github.com/juce-framework/JUCE).
See the JUCE license for terms that apply to that framework.

## Formant synthesizer (Klatt cascade/parallel)

`Source/DSP/Klatt.cpp` is an original implementation of a cascade/parallel
formant synthesizer. The structure and parameterisation follow the published
descriptions in:

- Dennis H. Klatt, "Software for a cascade/parallel formant synthesizer,"
  *Journal of the Acoustical Society of America* 67(3), 1980, 971–995.
- D. H. Klatt and L. C. Klatt, "Analysis, synthesis, and perception of voice
  quality variations among female and male talkers," *JASA* 87(2), 1990
  (KLSYN88 / KLGLOTT88 voicing).
- G. Fant, J. Liljencrants and Q. Lin, "A four-parameter model of glottal
  flow," STL-QPSR 4, 1985, and later Rd mappings in Fant 1997 and Gobl 2017.

Vowel formant targets are taken from published tables in Peterson and Barney
(1952) and Hillenbrand, Getty, Clark and Wheeler (1995). Consonant F2/F3
onset loci follow English locus-equation results of the kind reported by
Sussman and colleagues, used here as numeric targets only.

No source code was copied from eSpeak, VocalTractLab, Klatt's FORTRAN
distribution, or any other synthesizer. Those references are papers and
public scientific data only.


## Third-party synthesizers (personal use)

These components are included only because dooblr-throat is for the author's
personal use. Fetched artefacts live under `third_party/` at build time (see
`scripts/fetch-third-party.sh` and `third_party/checksums.txt`) and are bundled
into the `.vst3` at `Contents/Frameworks` (ONNX Runtime) and `Contents/Resources`
(the Piper voice and a trimmed English `espeak-ng-data`). They are not committed
to git, aside from the small WORLD source tree.

The default 0.3.0 engine is Natural (Piper + WORLD retune). The 0.2.x waveguide
remains available as the Engine = Tract fallback. Empty phrase text still uses
the waveguide vowel instrument.

### espeak-ng (GPL-3.0)

Source fetched from [espeak-ng/espeak-ng](https://github.com/espeak-ng/espeak-ng)
(commit `688cb60368d6e0998f3632af8d1c3c2ad4191fc2`). See `COPYING` (GPL-3.0) and
the accompanying COPYING.APACHE, COPYING.BSD2, and COPYING.UCD files for bundled
pieces (ucd-tools, etc.). Linked as a static library (`libespeak-ng`, `libucd`,
`libspeechPlayer`) with a trimmed English `espeak-ng-data` (en / en-US). Used as
Piper's phonemizer, and as a hidden env-selected formant voice
(`DOOBLRTHROAT_CLARITY=E`).

### ONNX Runtime (MIT)

Prebuilt binary fetched from
[microsoft/onnxruntime](https://github.com/microsoft/onnxruntime) v1.31.1
(osx-arm64). See `LICENSE` and `ThirdPartyNotices.txt` in the unpacked tree and
inside the plug-in bundle. The dSYM is not fetched or shipped.

### Piper software (MIT) and en_US-lessac-low voice

Piper itself is MIT ([rhasspy/piper](https://github.com/rhasspy/piper)).
The `en_US-lessac-low` VITS ONNX voice is fetched from
[rhasspy/piper-voices](https://huggingface.co/rhasspy/piper-voices)
(`en/en_US/lessac/low`, v1.0.0). Some redistributors label the packaged files
MIT; the model's own `MODEL_CARD` points at the Lessac Blizzard 2013 corpus
licence, which limits use of those recordings to "research and exploration of
speech technologies" and excludes commercial use. The Piper project states it
holds no permission from Lessac Technologies or CSTR and makes no representation
about rights flowing from those recordings
([OHF-Voice/piper1-gpl, docs/LICENSING.md](https://github.com/OHF-Voice/piper1-gpl/blob/main/docs/LICENSING.md)).
This plugin embeds the voice only for the author's personal, non-commercial use.
See `third_party/piper-voices/MODEL_CARD` and
https://www.cstr.ed.ac.uk/projects/blizzard/2013/lessac_blizzard2013/license.html.

### WORLD vocoder (BSD-3-Clause)

Source vendored in `third_party/World/` from [mmorise/World](https://github.com/mmorise/World)
(commit `c794856b537758a3d300757db2e5e31685a822c2`). See
`third_party/World/LICENSE.txt`. Used off the audio thread to flatten Piper's F0
to a C4 reference; the audio thread then resamples that buffer to the MIDI note.
