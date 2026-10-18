. "$PSScriptRoot/../scripts/environment.ps1"
$env:PATH = "$Workspace/toolchains/msys64/mingw64/bin;$env:PATH"
$compiler = "$Workspace/toolchains/msys64/mingw64/bin/g++.exe"
New-Item -ItemType Directory -Force "$Workspace/build/tests" | Out-Null
Invoke-Logged 'xma-despertar-compile' $compiler @('-std=c++23', '-O2', '-Wall', '-Wextra', '-pthread',
    "-I$Workspace/reference-nfsmw-nx/sdk/src/audio", "$Workspace/tests/xma_despertar_test.cpp",
    '-o', "$Workspace/build/tests/xma-despertar-test.exe")
Invoke-Logged 'xma-despertar-test' "$Workspace/build/tests/xma-despertar-test.exe" @()
Get-Content "$LogDirectory/xma-despertar-test.log"
