# Third-party notices

H2Classic's anti-aliasing follows published code. The shaders in `layer2/shaders/` and `layer2/gfx_aa_shaders.h` are
written out from that code, and `layer2/smaa/` is SMAA's own source.

## SMAA

Used by `H2N_AA=smaa`. Source: <https://github.com/iryoku/smaa>. Files: `SMAA.hlsl`, `Textures/AreaTex.h` and
`Textures/SearchTex.h`, kept in `layer2/smaa/` with their license (`layer2/smaa/LICENSE.txt`).

```
Copyright (C) 2013 Jorge Jimenez (jorge@iryoku.com)
Copyright (C) 2013 Jose I. Echevarria (joseignacioechevarria@gmail.com)
Copyright (C) 2013 Belen Masia (bmasia@unizar.es)
Copyright (C) 2013 Fernando Navarro (fernandn@microsoft.com)
Copyright (C) 2013 Diego Gutierrez (diegog@unizar.es)

Permission is hereby granted, free of charge, to any person obtaining a copy
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software. As clarification, there is no
requirement that the copyright notice and permission be included in binary
distributions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```

## Temporal anti-aliasing (Playdead)

Used by `H2N_AA=taa`. Source: <https://github.com/playdeadgames/temporal>, files `TemporalReprojection`,
`VelocityBuffer` and `FrustumJitter`.

```
The MIT License (MIT)

Copyright (c) [2015] [Playdead]

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

## NVIDIA DLSS

Used by `H2N_AA=dlss`. Source: <https://github.com/NVIDIA/DLSS>. `H2Classic_DLSS.exe` is built against the DLSS SDK's
headers and its NGX library, which are not in this repository. `dlss/build_helper.sh` fetches them.

This software contains source code provided by NVIDIA Corporation.

The NGX library in `H2Classic_DLSS.exe` is used and passed on under the NVIDIA RTX SDKs license, in
[dlss/NVIDIA_RTX_SDKs_LICENSE.txt](dlss/NVIDIA_RTX_SDKs_LICENSE.txt). `nvngx_dlss.dll` is NVIDIA's file. It is not part
of this repository or of H2Classic's packages, and users supply it themselves.
