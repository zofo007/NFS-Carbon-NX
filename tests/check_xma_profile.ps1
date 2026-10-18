. "$PSScriptRoot/../scripts/environment.ps1"
$compiler = "$Workspace/toolchains/msys64/mingw64/bin/g++.exe"
$env:PATH = "$Workspace/toolchains/msys64/mingw64/bin;$env:PATH"
New-Item -ItemType Directory -Force "$Workspace/build/tests" | Out-Null
Invoke-Logged 'xma-profile-compile' $compiler @('-std=c++23', '-O2', '-Wall', '-Wextra', '-Werror', '-pthread',
    "-I$Workspace/reference-nfsmw-nx/sdk/include", "$Workspace/tests/xma_profile_test.cpp",
    '-o', "$Workspace/build/tests/xma-profile-test.exe")
Invoke-Logged 'xma-profile-test' "$Workspace/build/tests/xma-profile-test.exe" @()
Write-Output 'PASS: real XMA sampled timer disabled/unsampled paths, stage deltas, unavailable clocks and concurrent counters.'
