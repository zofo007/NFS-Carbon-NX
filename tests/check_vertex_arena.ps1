. "$PSScriptRoot/../scripts/environment.ps1"
$compiler = "$Workspace/toolchains/msys64/mingw64/bin/g++.exe"
$env:PATH = "$Workspace/toolchains/msys64/mingw64/bin;$env:PATH"
New-Item -ItemType Directory -Force "$Workspace/build/tests" | Out-Null
Invoke-Logged 'vertex-arena-compile' $compiler @('-std=c++23', '-O3', '-Wall', '-Wextra', '-Werror',
    "-I$Workspace/carbon-source/renderer/src", "$Workspace/tests/vertex_arena_metadata_test.cpp",
    '-o', "$Workspace/build/tests/vertex-arena-test.exe")
Invoke-Logged 'vertex-arena-test' "$Workspace/build/tests/vertex-arena-test.exe" @()
Get-Content "$LogDirectory/vertex-arena-test.log"
Write-Output 'PASS: actual arena metadata initialization/move, old/new page-state model and allocation/free counts.'
Invoke-Logged 'index-cache-compile' $compiler @('-std=c++23', '-O3', '-Wall', '-Wextra', '-Werror',
    "-I$Workspace/carbon-source/renderer/src", "$Workspace/tests/index_cache_test.cpp",
    '-o', "$Workspace/build/tests/index-cache-test.exe")
Invoke-Logged 'index-cache-test' "$Workspace/build/tests/index-cache-test.exe" @()
Get-Content "$LogDirectory/index-cache-test.log"
