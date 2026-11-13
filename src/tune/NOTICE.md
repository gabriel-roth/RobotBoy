# Tune: code from CV Funk

`FrequencyTracker.hpp` is the pitch tracking and scope trigger of the CV Funk **Tuner** module, written by Cody Geary. The scope capture in `../Tune.cpp` follows the same module's `process()`.

- Upstream: <https://github.com/codygeary/CVfunk-Modules>, commit `25718429e90c7c4402eb3188a1bc93249dfe1295`, `src/Tuner.cpp`.
- Licence: MIT, reproduced below.

## Local changes

1. `FrequencyTracker.hpp` is `Tuner.cpp` up to the end of `PhaseAccumulatorTrigger`, unchanged apart from its header comment and includes. With `-Isrc`, Rack's own `#include <plugin.hpp>` resolves to RobotBoy's header, so that has to come first.
2. The module and widget are RobotBoy's (`../Tune.cpp`): one channel instead of two, CV Funk's OFFSET, GAIN and WIDTH knobs fixed at their defaults, an audio thru output, and new displays. CV Funk's panels, `digital_display.hpp` and font are not used.

## Licence

MIT License

Copyright (c) 2024 Cody Geary / CV funk 

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
