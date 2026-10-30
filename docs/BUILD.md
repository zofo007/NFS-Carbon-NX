# Building

You build the NRO yourself from your own `default.xex`. The translated game code is generated on your machine and is
never part of this repository. The procedure is verified on **Windows 10/11 with PowerShell** only. Plan for several
hours the first time (the compiler and the graphics driver are built from source) and a few tens of GB of disk.

All pinned versions are in `dependencies.json`. Run the scripts from the repository root.

## 1. Get the sources and tools

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/setup-environment.ps1 -InstallHostCompiler -FetchThirdParty
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/apply-source-patches.ps1
```

`setup-environment.ps1` clones the pinned upstream repositories (the Carbon project, the Switch SDK reference and
mesa-switch) next to the scripts and installs CMake and Ninja. `apply-source-patches.ps1` applies this project's changes
from `patches/` to those clones. Nothing is overwritten if a patch does not apply cleanly.

## 2. Toolchain (once)

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-devkita64.ps1 -SourcesOnly
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/fix-devkita64-limits.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-switch-tools.ps1 -Jobs 2
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/setup-graphics-tools.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-mesa-host.ps1 -Jobs 2
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-libnx.ps1 -Jobs 2
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-portlibs.ps1 -Jobs 2
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-mesa-switch.ps1 -Jobs 2
```

This builds devkitA64 (GCC for the Switch), libnx, the support libraries and the NVK graphics driver. It only has to
be done once; later builds reuse it. `scripts/check-dependencies.ps1` verifies the result.

## 3. Your game files

Put your extracted Xbox 360 files in `game-data/`:

```text
game-data/default.xex
game-data/NFS/
game-data/Movies/
```

`scripts/check_game.py game-data` checks the files against the tested edition and refuses others. Use dumps of original
discs you own. The game files are only read; nothing is written to them.

## 4. Generate the code and build

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-host.ps1 -CodegenOnly
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-switch.ps1 -Jobs 2
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/validate-build.ps1
```

`build-host.ps1` builds ReXGlue's code generator and translates `default.xex` into C++ (this takes a while and produces
many large files under `carbon-source/generated/`). `build-switch.ps1` compiles everything for the Switch.
The result is `artifacts/nfscarbon.nro`. Raise `-Jobs` if you have the memory; the generated files compile slowly.

Since 1.1.0 the translated game code reads guest memory with plain loads (`scripts/fast_loads_scan.py` and `fast_loads_apply.py`
rewrite the generated files for the build and undo it afterwards; the functions that wait for another thread, use barriers,
atomics or the global lock keep strict volatile loads). Add `-NoFastLoads` to `build-switch.ps1` to build with volatile loads
everywhere. `tests/test_fast_loads.py` covers the scanner and the transform.

## 5. Put it on the SD card

Copy `nfscarbon.nro` and `config/nfscarbon.release.toml` (as `nfscarbon.toml`) to `sdmc:/switch/nfscarbon/`, together
with your `game-data/` folder. See the README for launching.

### Optional: Collector's Edition (beta)

Put your Collector's Edition dump in `game-data-collectors/` (`default.xex`, `NFS/`, `Movies/`), then run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-switch.ps1 -Edition collectors-rl -Jobs 4
```

`collectors-rl` is the 1.1.0 Collector's translation (see below). `-Edition collectors` builds the older translation, kept as a fallback.

The accepted CE executable has SHA-256 `922c7a1a2fc77405c43aac5159ad67fb0414c872084abc3335c68dbb4bec1815`
(media `1F8BEB88`, version 7, USA / NTSC-U). The script uses the existing host code generator; it does not build a Windows game.
Generated code, CMake glue, build files and artifacts are separate from standard edition output.
The renderer and runtime source are shared. Plain `build-switch.ps1` still builds the standard edition.
Re-run the edition's build script after updates; it also runs the required code generation.

Output: `artifacts/collectors-rl/nfscarbon-collectors-rl.nro` (install it as `nfscarbon-collectors.nro`) and
`artifacts/collectors-rl/nfscarbon.toml` (for `-Edition collectors`: `artifacts/collectors/nfscarbon-collectors.nro`).
Install both in `sdmc:/switch/nfscarbon-collectors/`, with the CE dump in that folder's `game-data/`.
Use a fresh, separate `user/` folder for the first test. The standard installation stays in `switch/nfscarbon/`.

The older `-Edition collectors` build uses context-preserving translated guest routines (every register lives in the guest
context between calls), which is why its game code was 14% heavier in instructions than the standard edition's in the hottest
functions (docs/PERFORMANCE.md). Both Collector's builds share the standard edition's native hooks: the GPU ring-space wait, the frame hand-off, the audio helpers, the render-pass timers
and cube refresh, the audio server wait and the car-detail hook. Every address in the Collector's branches of those hooks
was matched instruction for instruction against the standard edition (`src/edition.h`; the tests in
`tests/test_editions.py` re-check them against the generated code), and the audio helpers have differential tests
(`tests/check_audio_collectors.ps1`).

`-Edition collectors-rl` is translated the way the standard edition is: the long-lived registers are local variables.
It needs the list of functions that read each other's registers (`config/collectors_rl_share_registers.txt`, 150 entries;
`scripts/check_register_sharing.py` checks it and `0 function(s) need share_registers` is the expected result; all 151
Standard entries that have an identical Collector's function are in the list). It is 20% smaller than the older translation
and has run for one 34-minute session on hardware; if it ever crashes, the log reports the guest address that needs a new entry.
### Optional: DLC

Copy your Xbox 360 DLC packages (the STFS files, from your own copy of the game) into
`sdmc:/switch/nfscarbon/dlc/`, in any subfolder. At the next start the game extracts them into its content folder
and the log shows `[dlc] N package(s) installed`. Nothing in `game-data/` is changed. Which pack unlocks what is
still being tested; see KNOWN_ISSUES.


## Checks

```powershell
python -m unittest discover -s tests -v
powershell -NoProfile -ExecutionPolicy Bypass -File tests/check_native_leaf.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tests/check_carbon_callbacks.ps1
```

`check_native_leaf.ps1` compares the hand-written audio helpers with the generated originals. It needs the generated
code from step 4.

## Troubleshooting

- A patch does not apply: you changed the cloned sources. Re-run from a clean clone or export your own changes first
  with `scripts/export-source-patches.ps1`.
- The build stops with "Run build-host.ps1 -CodegenOnly first": the generated code is missing.
- Shell wrappers under `toolchains/` must not be edited while a build is running.
