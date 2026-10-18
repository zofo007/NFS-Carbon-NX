. "$PSScriptRoot/../scripts/environment.ps1"
$compiler = "$Workspace/toolchains/msys64/mingw64/bin/g++.exe"
$env:PATH = "$Workspace/toolchains/msys64/mingw64/bin;$env:PATH"
New-Item -ItemType Directory -Force "$Workspace/build/tests" | Out-Null
# Asserts stay enabled (no NDEBUG): the fakes check every replayed argument.
Invoke-Logged 'command-recorder-compile' $compiler @('-std=c++23', '-O2', '-msse4.1', '-pthread', '-Wall', '-Wextra', '-Werror',
    "-I$Workspace/carbon-source/renderer/src", "-I$Workspace/reference-nfsmw-nx/sdk/include",
    "-I$Workspace/reference-nfsmw-nx/sdk/thirdparty/vulkan-headers/include",
    "-I$Workspace/reference-nfsmw-nx/sdk/thirdparty/simde",
    "-I$Workspace/reference-nfsmw-nx/sdk/thirdparty/fmt/include",
    "-I$Workspace/reference-nfsmw-nx/sdk/thirdparty/spdlog/include",
    "$Workspace/tests/command_recorder_test.cpp", '-o', "$Workspace/build/tests/command-recorder-test.exe")
Invoke-Logged 'command-recorder-test' "$Workspace/build/tests/command-recorder-test.exe" @()
Get-Content "$LogDirectory/command-recorder-test.log"
Write-Output 'PASS: real command recorder replays every record type identically on the worker (wrap, backpressure, drains, start/stop).'
