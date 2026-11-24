# Third-party notices

These notices are separate from the license for Hyprimate's own code and
documentation. See [Credits](CREDITS.md) for research references and artwork
provenance. When distributing a prebuilt `hyprimate.so`, include `LICENSE`, this
file, and `CREDITS.md` with it. If you build against different dependency
versions, preserve the notices from the versions you used as well.

## MIT-licensed code

**[nlohmann/json](https://github.com/nlohmann/json)** is the header-only JSON
parser used by the shader package loader. Its implementation is compiled into
`hyprimate.so`. The [v3.12.0 license](https://github.com/nlohmann/json/blob/v3.12.0/LICENSE.MIT)
and the copyright notices in its C++ headers identify:

```text
Copyright (c) 2013-2025 Niels Lohmann <https://nlohmann.me>
Copyright (c) 2008-2009 Björn Hoehrmann <bjoern@hoehrmann.de>
Copyright (c) 2009 Florian Loitsch <https://florian.loitsch.com/>
Copyright (c) 2016-2021 Evan Nemerson <evan@nemerson.com>
Copyright (c) 2018 The Abseil Authors
```

**[Hash without Sine](https://www.shadertoy.com/view/4djSRW)** by David Hoskins
is the reference for the hash variants in Burn (including ground debris), Lava,
and Planet Hop. The [LYGIA reference](https://github.com/patriciogonzalezvivo/lygia/blob/main/generative/random.glsl)
and a [published port](https://gist.github.com/davidar/5f9677a0ccfbd63d7a8657ad9af3a856)
credit Hoskins and identify the original implementation as MIT. The exact path
by which the variants reached this project is not recorded.

```text
Copyright (c) 2014 David Hoskins
```

The following MIT terms apply to the notices above:

```text
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

## Hyprland SDK (BSD 3-Clause)

Hyprimate builds against the [Hyprland](https://github.com/hyprwm/Hyprland)
headers and runs inside the user's compositor. The compositor is not bundled.
The SDK notice below is from [Hyprland v0.57.2](https://github.com/hyprwm/Hyprland/blob/v0.57.2/LICENSE).

```text
BSD 3-Clause License

Copyright (c) 2022-2026, vaxerski
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

## Other system libraries and older builds

libpng, OpenGL ES/EGL, Lua, and the other shared libraries supplied by the host
or package manager are not bundled in `hyprimate.so`. See their own packages
for license notices.

Older builds of Cloth statically linked [Jolt Physics 5.6.0](https://github.com/jrouwe/JoltPhysics/tree/v5.6.0).
Jolt is not in current builds. Distributions of those older binaries should
include [Jolt's original MIT notice](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/LICENSE).
