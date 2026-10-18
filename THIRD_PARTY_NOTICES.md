# Third-party notices

NFS Carbon NX is built on the work of other projects. Their licenses apply to their code and to the parts of this
repository derived from it. Exact revisions are in `dependencies.json`. This repository holds patches and scripts, not
copies of these projects.

## Projects this work is derived from

| Project | Used for | License |
|---|---|---|
| [nfs-carbon-recomp](https://github.com/Sampreeth004/nfs-carbon-recomp) by Sampreeth004 | The Carbon recompilation project and native Vulkan renderer this project is built on (`carbon-source`, pinned in `dependencies.json`) | **No license grant found** in the upstream repository. See `docs/RELEASE_BLOCKERS.md` |
| [nfsmw-nx](https://github.com/StevensND/nfsmw-nx) by StevensND | The Horizon (Switch) layer of the ReXGlue SDK and the overall port design | GPL-3.0 (root); `sdk/` under BSD-3-Clause |
| [NFSMW Recompiled](https://github.com/madelrandel-blip/NFSMW-Recompiled) by madelrandel-blip and [NFSMW-Recompiled-Mobile](https://github.com/victorgbd/NFSMW-Recompiled-Mobile) by victorgbd | Origin of the hooks in Carbon's `ring_wait_hook.cpp` and `thread_affinity.cpp` | GPL-3.0 |
| [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) by Tom Clay, with portions from [Xenia](https://xenia.jp) (Ben Vanik and the Xenia contributors) | Code generator, Xbox 360 kernel and runtime, GPU command processing | BSD-3-Clause |
| [mesa-switch](https://github.com/danfromtico/mesa-switch) by danfromtico (also NaGaa95's fork), on top of [Mesa](https://mesa3d.org) | The Vulkan driver (NVK) and shader compiler (NAK) for Horizon, linked into the NRO | MIT (Mesa's licenses per file) |

Approaches studied but not copied: the Most Wanted port's audio packet wait, frame hand-off, FFmpeg cutscene decoder
and game-function hooks were read as design references; the Carbon code for them uses Carbon's own addresses and was
written separately. No Most Wanted assets, addresses, generated code or profile data are used.

## Build tools

[devkitPro](https://devkitpro.org) (devkitA64: GCC, binutils, newlib; GPL with the runtime exceptions of each
component), [libnx](https://github.com/switchbrew/libnx) (ISC), switch-tools, CMake, Ninja, Python, MSYS2, LLVM/Clang
and the Rust toolchain used to build Mesa. Each keeps its own license; the compilers' outputs are not covered by the
compilers' licenses beyond the runtime library exceptions.

## Libraries linked into the NRO

Fetched from the submodules of ReXGlue SDK v0.10.0 unless noted.

| Library | License |
|---|---|
| [FFmpeg](https://ffmpeg.org) (libavcodec, libavutil: WMV3 video for cutscenes, WMA Pro for XMA audio) | LGPL-2.1-or-later (configured without GPL parts) |
| [libmspack](https://www.cabextract.org.uk/libmspack/) by Stuart Caie | LGPL-2.1 |
| [glslang](https://github.com/KhronosGroup/glslang) | BSD-3-Clause and others (see its LICENSE.txt) |
| [SPIRV-Tools](https://github.com/KhronosGroup/SPIRV-Tools) | Apache-2.0 |
| [SPIRV-Headers](https://github.com/KhronosGroup/SPIRV-Headers), [Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers) | MIT, Apache-2.0 |
| [Vulkan Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) | MIT |
| [fmt](https://github.com/fmtlib/fmt), [spdlog](https://github.com/gabime/spdlog) | MIT |
| [xxHash](https://github.com/Cyan4973/xxHash) | BSD-2-Clause |
| [SIMDe](https://github.com/simd-everywhere/simde) | MIT |
| [toml++](https://github.com/marzer/tomlplusplus) | MIT |
| [Dear ImGui](https://github.com/ocornut/imgui) | MIT |
| [o1heap](https://github.com/pavel-kirienko/o1heap) | MIT |
| [utfcpp](https://github.com/nemtrif/utfcpp) | BSL-1.0 |
| [CLI11](https://github.com/CLIUtils/CLI11) | BSD-3-Clause |
| AES-128 by LuoPeng | MIT |
| [libnx](https://github.com/switchbrew/libnx) and the devkitA64 runtime (newlib, libstdc++) | ISC; newlib and GCC runtime licenses |

### FFmpeg and libmspack (LGPL)

The NRO links FFmpeg statically. The configuration used is the SDK's (`FFmpeg/config_nfsmw_wmv3.h` and the Switch
AArch64 configuration, limited to the WMV3 and WMA Pro decoders). Everything needed to rebuild and relink the NRO with
a modified FFmpeg or libmspack is the build procedure in `docs/BUILD.md` plus the pinned sources in
`dependencies.json`. Modified files of those libraries, if any, are carried as patches in `patches/`.

## ReXGlue SDK license (sdk/LICENSE)

Copyright (c) 2026, Tom Clay <tomc@tctechstuff.com>

Portions of this software are derived from the Xenia project:
Copyright (c) 2022, Ben Vanik and Xenia project contributors
https://xenia.jp

All rights reserved. Redistribution and use in source and binary forms, with or without modification, are permitted
provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following
   disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following
   disclaimer in the documentation and/or other materials provided with the distribution.
3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote
   products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

## Trademarks

Need for Speed and Need for Speed: Carbon are trademarks of Electronic Arts Inc. Nintendo Switch is a trademark of
Nintendo. Xbox 360 is a trademark of Microsoft. This project is not affiliated with or endorsed by any of them.
