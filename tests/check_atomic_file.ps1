. "$PSScriptRoot/../scripts/environment.ps1"
$compiler = "$Workspace/toolchains/msys64/mingw64/bin/g++.exe"
$env:PATH = "$Workspace/toolchains/msys64/mingw64/bin;$env:PATH"
New-Item -ItemType Directory -Force "$Workspace/build/tests" | Out-Null
Invoke-Logged 'atomic-file-compile' $compiler @('-std=c++23', '-O3', '-Wall', '-Wextra', '-Werror',
    "-I$Workspace/reference-nfsmw-nx/sdk/include", "$Workspace/tests/atomic_file_test.cpp",
    '-o', "$Workspace/build/tests/atomic-file-test.exe")
$fixture = "$Workspace/build/tests/atomic-file-$([guid]::NewGuid().ToString('N'))"
Invoke-Logged 'atomic-file-test' "$Workspace/build/tests/atomic-file-test.exe" @($fixture)
Get-Content "$LogDirectory/atomic-file-test.log"
