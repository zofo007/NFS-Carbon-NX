. "$PSScriptRoot/../scripts/environment.ps1"
$compiler = "$Workspace/toolchains/msys64/mingw64/bin/g++.exe"
if (-not (Test-Path -LiteralPath $compiler)) { throw 'The provisioned native GNU compiler is missing.' }
$env:PATH = "$Workspace/toolchains/msys64/mingw64/bin;$env:PATH"
$output = "$Workspace/build/tests"
New-Item -ItemType Directory -Force -Path $output | Out-Null
Invoke-Logged 'draw-profile-compile' $compiler @('-std=c++23', '-O2', '-Wall', '-Wextra', '-Werror',
    "-I$Workspace/carbon-source/renderer/src", "-I$Workspace/reference-nfsmw-nx/sdk/thirdparty/vulkan-headers/include",
    "$Workspace/tests/draw_profile_test.cpp", '-o', "$output/draw-profile-test.exe")
Invoke-Logged 'draw-profile-test' "$output/draw-profile-test.exe" @()
Write-Output 'PASS: draw-stage sampling, ready-pipeline cache and descriptor-bind batch/gap/fallback cases.'
