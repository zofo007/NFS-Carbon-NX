. "$PSScriptRoot/../scripts/environment.ps1"
$env:PATH = "$Workspace/toolchains/msys64/mingw64/bin;$env:PATH"
$compiler = "$Workspace/toolchains/msys64/mingw64/bin/g++.exe"
$testDir = "$Workspace/build/tests/index-reuse"
New-Item -ItemType Directory -Force $testDir | Out-Null
$source = Get-Content "$Workspace/carbon-source/renderer/src/renderer.cpp" -Raw
$converters = [regex]::Match($source, '(?s)template <bool kSwap, bool kRestart>.*?(?=\r?\ndouble NowSeconds\()')
$draw = [regex]::Match($source, '(?s)  if \(info.indexed && expand == Expand::kNone && count\) \{.*?(?= else if \(!info.indexed\))')
if (-not $converters.Success -or -not $draw.Success) { throw 'Actual indexed draw/converters not found.' }
foreach ($part in @(@('converters', $converters.Value), @('draw', $draw.Value))) {
    [IO.File]::WriteAllText("$testDir/$($part[0]).inc", $part[1], (New-Object Text.UTF8Encoding($false)))
}
Invoke-Logged 'index-reuse-compile' $compiler @('-std=c++23', '-O3', '-msse4.1', '-Wall', '-Wextra', '-Werror',
    "-I$testDir", "-I$Workspace/carbon-source/renderer/src",
    '-isystem', "$Workspace/reference-nfsmw-nx/sdk/include",
    '-isystem', "$Workspace/reference-nfsmw-nx/sdk/thirdparty/vulkan-headers/include",
    '-isystem', "$Workspace/reference-nfsmw-nx/sdk/thirdparty/simde",
    '-isystem', "$Workspace/reference-nfsmw-nx/sdk/thirdparty/fmt/include",
    '-isystem', "$Workspace/reference-nfsmw-nx/sdk/thirdparty/spdlog/include",
    "$Workspace/tests/index_reuse_test.cpp", '-o', "$testDir/index-reuse-test.exe")
Invoke-Logged 'index-reuse-test' "$testDir/index-reuse-test.exe" @()
Get-Content "$LogDirectory/index-reuse-test.log"
