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

## 5. Put it on the SD card

Copy `nfscarbon.nro` and `config/nfscarbon.release.toml` (as `nfscarbon.toml`) to `sdmc:/switch/nfscarbon/`, together
with your `game-data/` folder. See the README for launching.

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
