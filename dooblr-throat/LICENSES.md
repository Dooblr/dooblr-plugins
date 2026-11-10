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
bit-identical to the Rust original.

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

This plugin does not bundle CMUDICT, fonts, or any neural model weights from
that project.

## JUCE

The plugin is built against [JUCE 8.0.15](https://github.com/juce-framework/JUCE).
See the JUCE license for terms that apply to that framework.
