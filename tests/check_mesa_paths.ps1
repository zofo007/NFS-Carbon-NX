$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
. "$root/scripts/environment.ps1"
$devkit = "$root/toolchains/msys64/opt/devkitpro".Replace('\','/')
$directory = "$devkit/portlibs/switch/lib/pkgconfig"
$fixture = "$directory/carbon-pkg-path-smoke.pc"
if (Test-Path -LiteralPath $fixture) { throw 'Preserving existing path-test metadata; inspect before retrying.' }
New-Item -ItemType Directory -Force $directory | Out-Null
Copy-Item -LiteralPath "$PSScriptRoot/fixtures/carbon-pkg-path-smoke.pc" -Destination $fixture
$env:DEVKITPRO = $devkit
$env:MSYSTEM = 'MINGW64'
try {
    Invoke-Logged 'mesa-package-path-smoke' "$root/toolchains/msys64/usr/bin/bash.exe" @('--login',
        "$($root.Replace('\','/'))/toolchains/mesa-switch/switch-pkg-config-wrapper.sh", '--cflags', '--libs', 'carbon-pkg-path-smoke')
    $output = Get-Content "$LogDirectory/mesa-package-path-smoke.log" -Raw
    if (-not $output.Contains("-I$devkit/portlibs/switch/include") -or
        -not $output.Contains("-L$devkit/portlibs/switch/lib") -or $output.Contains('C:/msys64')) {
        throw "Incorrect portable pkg-config paths: $output"
    }
} finally { Remove-Item -LiteralPath $fixture }
$native = Get-Content "$root/build/mesa-config/switch_native_tools_msys2.txt" -Raw
foreach ($compiler in @('gcc', 'g++')) {
    if (-not $native.Contains("$($root.Replace('\','/'))/toolchains/msys64/mingw64/bin/$compiler.exe")) {
        throw "Missing explicit native $compiler compiler in the generated Mesa machine file."
    }
}
Write-Output 'PASS: local SDK pkg-config paths and explicit GNU native compiler paths.'
