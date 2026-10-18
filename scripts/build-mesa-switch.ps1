param([ValidateRange(1,8)][int]$Jobs = 2)
. "$PSScriptRoot/environment.ps1"
$mesa = "$Workspace/toolchains/mesa-switch".Replace('\','/')
$msys = "$Workspace/toolchains/msys64".Replace('\','/')
$devkit = "$msys/opt/devkitpro"
$config = "$Workspace/build/mesa-config"
foreach ($file in @("$devkit/devkitA64/bin/aarch64-none-elf-g++.exe", "$devkit/libnx/lib/libnx.a",
    "$devkit/portlibs/switch/lib/libexpat.a", "$devkit/portlibs/switch/lib/libzstd.a", "$devkit/portlibs/switch/lib/libz.a",
    "$mesa/builddir-native-x64-host/src/compiler/clc/mesa_clc.exe", "$config/switch_cross_file_msys2.txt")) {
    if (-not (Test-Path -LiteralPath $file)) { throw "Mesa prerequisite missing: $file" }
}
$pins = Get-Content "$Workspace/dependencies.json" -Raw | ConvertFrom-Json
$env:CARGO_HOME = "$Workspace/toolchains/rust/cargo"
$env:RUSTUP_HOME = "$Workspace/toolchains/rust/rustup"
$env:RUSTUP_TOOLCHAIN = $pins.rust_toolchain
$env:MESA_SWITCH_RUST_TARGET = 'aarch64-unknown-linux-gnu'
$env:MESA_SWITCH_RUSTC = "$env:CARGO_HOME/bin/rustc.exe".Replace('\','/')
$env:MESA_SWITCH_NATIVE_RUSTC = $env:MESA_SWITCH_RUSTC
$env:MESA_SWITCH_NATIVE_LINKER = "$msys/mingw64/bin/gcc.exe"
$env:MESA_SWITCH_BINDGEN = "$msys/clang64/bin/bindgen.exe"
$env:LIBCLANG_PATH = "$msys/clang64/bin"
$env:DEVKITPRO = $devkit
$env:MSYSTEM = 'MINGW64'
$env:PATH = "$msys/mingw64/bin;$msys/clang64/bin;$msys/usr/bin;$env:CARGO_HOME/bin;$env:PATH"
$env:NINJA = $Ninja
$build = "$mesa/builddir-unified"
$arguments = @('setup', $build, $mesa, '--cross-file', "$config/switch_cross_file_msys2.txt",
    '--native-file', "$config/switch_native_tools_msys2.txt", '--default-library=static',
    '--prefix=/opt/devkitpro/portlibs/switch', '--libdir=lib', '--buildtype=release',
    '-Doptimization=2', '-Db_lto=false', '-Db_ndebug=true', '-Dvulkan-drivers=nouveau',
    '-Dgallium-drivers=', '-Dplatforms=switch', '-Dglx=disabled', '-Degl=disabled', '-Dopengl=false',
    '-Dgles1=disabled', '-Dgles2=disabled', '-Dshader-cache=enabled', '-Dxmlconfig=enabled',
    '-Dexpat=enabled', '-Dtools=[]', '-Dllvm=disabled', '-Dshared-glapi=disabled', '-Dshared-llvm=disabled',
    '-Dmesa-clc=system', '-Dprecomp-compiler=system', '-Dcpp_rtti=false', '-Dbuild-tests=false')
if (Test-Path "$build/meson-private/coredata.dat") { $arguments += '--reconfigure' }
Invoke-Logged 'mesa-switch-configure' "$msys/mingw64/bin/meson.exe" $arguments
Invoke-Logged 'mesa-switch-build' $Ninja @('-C', $build, '-j', "$Jobs")
# Keep the SDK path expected by the reference integration, but build only Vulkan.
$stage = "$mesa/mesa-unified-install"
Invoke-Logged 'mesa-switch-install' "$msys/mingw64/bin/meson.exe" @('install', '-C', $build, '--destdir', $stage)
$archive = "$stage/opt/devkitpro/portlibs/switch/lib/libvulkan.a"
if (-not (Test-Path -LiteralPath $archive)) { throw 'Mesa did not install the static Vulkan archive.' }
Invoke-Logged 'mesa-switch-architecture' "$devkit/devkitA64/bin/aarch64-none-elf-objdump.exe" @('-f', $archive)
$formats = @(Get-Content "$LogDirectory/mesa-switch-architecture.log" | Select-String 'file format ')
if (-not $formats.Count -or @($formats | Where-Object { $_ -notmatch 'file format elf64-littleaarch64$' }).Count) {
    throw 'Mesa Vulkan archive members are not all AArch64 ELF.'
}
Write-Output 'Static NVK built and staged; Switch hardware validation remains separate.'
