. "$PSScriptRoot/../scripts/environment.ps1"
$env:PATH = "$Workspace/toolchains/msys64/mingw64/bin;$env:PATH"
$compiler = "$Workspace/toolchains/msys64/mingw64/bin/g++.exe"
New-Item -ItemType Directory -Force "$Workspace/build/tests" | Out-Null
Invoke-Logged 'audio-server-wait-compile' $compiler @('-std=c++23', '-O2', '-Wall', '-Wextra', '-pthread',
    "-I$Workspace/carbon-source/src", "$Workspace/tests/audio_server_wait_test.cpp",
    '-o', "$Workspace/build/tests/audio-server-wait-test.exe")
Invoke-Logged 'audio-server-wait-test' "$Workspace/build/tests/audio-server-wait-test.exe" @()
Get-Content "$LogDirectory/audio-server-wait-test.log"
