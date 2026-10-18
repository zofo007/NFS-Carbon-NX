. "$PSScriptRoot/../scripts/environment.ps1"
$env:PATH = "$Workspace/toolchains/msys64/mingw64/bin;$env:PATH"
$compiler = "$Workspace/toolchains/msys64/mingw64/bin/g++.exe"
New-Item -ItemType Directory -Force "$Workspace/build/tests" | Out-Null
Invoke-Logged 'float-constants-compile' $compiler @('-std=c++23', '-O2', '-msse4.1', '-Wall', '-Wextra',
    "-I$Workspace/carbon-source/renderer/src", "-I$Workspace/reference-nfsmw-nx/sdk/include",
    "-I$Workspace/reference-nfsmw-nx/sdk/thirdparty/simde",
    "-I$Workspace/reference-nfsmw-nx/sdk/thirdparty/fmt/include",
    "-I$Workspace/reference-nfsmw-nx/sdk/thirdparty/spdlog/include",
    "$Workspace/tests/float_constants_test.cpp", '-o', "$Workspace/build/tests/float-constants-test.exe")
Invoke-Logged 'float-constants-test' "$Workspace/build/tests/float-constants-test.exe" @()
Write-Output 'PASS: actual constant-word bit identity and differential upload snapshots/prefix/chunk/frame cases.'
