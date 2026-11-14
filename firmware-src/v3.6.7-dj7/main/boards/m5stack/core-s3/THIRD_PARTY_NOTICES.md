# Third-party code in the headphone-girl CoreS3 board (v3.5)

## m5unified-avatar-mic (vowel detector)

Files: `main/audio/hg_vowel.h` (ported, 24 kHz front end added),
`main/audio/hg_vowel_model.inc` (Harper voice centers).
Source: https://github.com/mongonta0716/m5unified-avatar-mic (commit 8b6f79f),
VowelModel.h, LpcFormants.h, VowelAnalyzer.h.

MIT License

Copyright (c) 2026 Takao Akaki

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

## M5Stack StackChan firmware (MIT)

The head touch recognizer in `stackchan_sensor.cc` (weighted pad position,
swipe at a change of more than 40) follows M5Stack's StackChan firmware. The
geometry face in `stackchan_avatar/` is M5Stack's code (MIT), unchanged here.
The floating music notes follow the idea of its FloatingHearts decorator.
