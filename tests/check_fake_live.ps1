. "$PSScriptRoot/../scripts/environment.ps1"
$compiler = "$Workspace/toolchains/msys64/mingw64/bin/g++.exe"
$env:PATH = "$Workspace/toolchains/msys64/mingw64/bin;$env:PATH"
New-Item -ItemType Directory -Force "$Workspace/build/tests" | Out-Null
Invoke-Logged 'fake-live-compile' $compiler @('-std=c++23', '-O3', '-Wall', '-Wextra', '-Werror',
    "-I$Workspace/reference-nfsmw-nx/sdk/include", "$Workspace/tests/fake_live_test.cpp",
    '-o', "$Workspace/build/tests/fake-live-test.exe")
$fixture = "$Workspace/build/tests/fake-live-$([guid]::NewGuid().ToString('N'))"
Invoke-Logged 'fake-live-test' "$Workspace/build/tests/fake-live-test.exe" @($fixture)
Get-Content "$LogDirectory/fake-live-test.log"
