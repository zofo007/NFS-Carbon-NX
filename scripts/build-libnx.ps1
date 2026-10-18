param([ValidateRange(1,8)][int]$Jobs = 2)
. "$PSScriptRoot/environment.ps1"
$pins = Get-Content "$Workspace/dependencies.json" -Raw | ConvertFrom-Json
$source = "$Workspace/toolchains/libnx-source"
$devkit = "$Workspace/toolchains/msys64/opt/devkitpro"
foreach ($file in @("$devkit/devkitA64/bin/aarch64-none-elf-g++.exe",
    "$devkit/devkitA64/aarch64-none-elf/lib/libc.a", "$devkit/devkitA64/aarch64-none-elf/lib/libstdc++.a")) {
    if (-not (Test-Path -LiteralPath $file)) { throw "Complete build-devkita64.ps1 first; missing $file" }
}
if (-not (Test-Path "$source/.git")) {
    & git clone --no-checkout $pins.libnx.url $source
    if ($LASTEXITCODE) { throw 'Cannot acquire libnx.' }
    & git -c "safe.directory=$($source.Replace('\','/'))" -C $source checkout $pins.libnx.revision
    if ($LASTEXITCODE) { throw 'Cannot select pinned libnx revision.' }
}
$revision = & git -c "safe.directory=$($source.Replace('\','/'))" -C $source rev-parse HEAD
if ($LASTEXITCODE -or $revision -ne $pins.libnx.revision) { throw 'Unexpected libnx revision.' }
$env:MSYSTEM = 'MINGW64'
$env:DEVKITPRO = '/opt/devkitpro'
$env:CARBON_WORKSPACE = $Workspace.Replace('\','/')
$env:CARBON_BUILD_JOBS = "$Jobs"
Invoke-Logged 'libnx-build' "$Workspace/toolchains/msys64/usr/bin/bash.exe" @('--login',
    "$($PSScriptRoot.Replace('\','/'))/build-libnx.sh")
$env:DEVKITPRO = $devkit.Replace('\','/')
$env:PATH = "$Workspace/toolchains/msys64/mingw64/bin;$env:PATH"
Invoke-Logged 'switch-smoke-configure' $CMake @('-S', "$Workspace/tests/switch-smoke", '-B', "$Workspace/build/switch-smoke",
    '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', "-DCMAKE_MAKE_PROGRAM=$Ninja", "-DDEVKITPRO=$env:DEVKITPRO",
    "-DCMAKE_TOOLCHAIN_FILE=$Workspace/reference-nfsmw-nx/tools/switch/cmake/switch-devkitA64.cmake")
Invoke-Logged 'switch-smoke-build' $CMake @('--build', "$Workspace/build/switch-smoke", '-j', "$Jobs")
Invoke-Logged 'switch-smoke-nacp' "$devkit/tools/bin/nacptool.exe" @('--create', 'Carbon SDK Smoke', 'Local test', '0.0.0',
    "$Workspace/build/switch-smoke/switch-smoke.nacp")
Invoke-Logged 'switch-smoke-package' "$devkit/tools/bin/elf2nro.exe" @("$Workspace/build/switch-smoke/switch-smoke",
    "$Workspace/build/switch-smoke/switch-smoke.nro", "--nacp=$Workspace/build/switch-smoke/switch-smoke.nacp")
Invoke-Logged 'switch-smoke-validate' (Get-Command python).Source @("$PSScriptRoot/validate_build.py",
    "$Workspace/build/switch-smoke/switch-smoke.nro", "$Workspace/build/switch-smoke/switch-smoke")
Write-Output 'SDK/linker/packager smoke test only; not nfscarbon.nro or proof of hardware boot.'
