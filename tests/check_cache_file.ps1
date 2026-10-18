. "$PSScriptRoot/../scripts/environment.ps1"
$compiler = "$Workspace/toolchains/msys64/mingw64/bin/g++.exe"
$env:PATH = "$Workspace/toolchains/msys64/mingw64/bin;$env:PATH"
New-Item -ItemType Directory -Force "$Workspace/build/tests" | Out-Null
Invoke-Logged 'cache-file-compile' $compiler @('-std=c++23', '-O3', '-Wall', '-Wextra', '-Werror',
    "-I$Workspace/carbon-source/renderer/src", "$Workspace/tests/cache_file_test.cpp",
    '-o', "$Workspace/build/tests/cache-file-test.exe")
$fixture = "$Workspace/build/tests/cache-file-$([guid]::NewGuid().ToString('N'))"
Invoke-Logged 'cache-file-test' "$Workspace/build/tests/cache-file-test.exe" @($fixture)
Get-Content "$LogDirectory/cache-file-test.log"
