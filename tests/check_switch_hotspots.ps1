. "$PSScriptRoot/../scripts/environment.ps1"
$compiler = "$Workspace/toolchains/msys64/mingw64/bin/g++.exe"
$env:PATH = "$Workspace/toolchains/msys64/mingw64/bin;$env:PATH"
New-Item -ItemType Directory -Force "$Workspace/build/tests" | Out-Null
Invoke-Logged 'switch-hotspots-compile' $compiler @('-std=c++23', '-O3', '-Wall', '-Wextra', '-Werror',
    "-I$Workspace/reference-nfsmw-nx/sdk/src/ui", "$Workspace/tests/switch_hotspot_policy_test.cpp",
    '-o', "$Workspace/build/tests/switch-hotspots-test.exe")
Invoke-Logged 'switch-hotspots-test' "$Workspace/build/tests/switch-hotspots-test.exe" @()
Write-Output 'PASS: opt-in mode precedence, exact target names/handle reuse, disabled/legacy intervals and jitter bounds.'
