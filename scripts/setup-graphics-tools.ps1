. "$PSScriptRoot/environment.ps1"
$env:MSYSTEM = 'MINGW64'
$bash = "$Workspace/toolchains/msys64/usr/bin/bash.exe"
$packages = @('pkgconf', 'mingw-w64-clang-x86_64-clang', 'mingw-w64-clang-x86_64-llvm',
    'mingw-w64-clang-x86_64-libclc', 'mingw-w64-clang-x86_64-spirv-tools',
    'mingw-w64-clang-x86_64-spirv-llvm-translator', 'mingw-w64-clang-x86_64-pkgconf',
    'mingw-w64-clang-x86_64-cmake', 'mingw-w64-clang-x86_64-rust-bindgen',
    'mingw-w64-clang-x86_64-cbindgen', 'mingw-w64-x86_64-meson',
    'mingw-w64-x86_64-python-mako', 'mingw-w64-x86_64-python-yaml', 'mingw-w64-x86_64-python-packaging')
Invoke-Logged 'graphics-packages' $bash @('--login', '-c', ('pacman -S --needed --noconfirm ' + ($packages -join ' ')))
Invoke-Logged 'graphics-package-versions' $bash @('--login', '-c', 'pacman -Q')
& "$PSScriptRoot/setup-rust.ps1"
