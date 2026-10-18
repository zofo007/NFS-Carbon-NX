. "$PSScriptRoot/../scripts/environment.ps1"
$env:PATH = "$Workspace/toolchains/msys64/mingw64/bin;$env:PATH"
$compiler = "$Workspace/toolchains/msys64/mingw64/bin/g++.exe"
New-Item -ItemType Directory -Force "$Workspace/build/tests" | Out-Null
Invoke-Logged 'upload-memory-compile' $compiler @('-std=c++23', '-O2', '-ffunction-sections', '-fdata-sections',
    '-Wl,--gc-sections', "-I$Workspace/reference-nfsmw-nx/sdk/thirdparty/vulkan-memory-allocator/include",
    "-I$Workspace/reference-nfsmw-nx/sdk/thirdparty/vulkan-headers/include",
    "$Workspace/tests/upload_memory_test.cpp", '-o', "$Workspace/build/tests/upload-memory-test.exe")
Invoke-Logged 'upload-memory-test' "$Workspace/build/tests/upload-memory-test.exe" @()
Write-Output 'PASS: actual VMA uncached/cached host-access preferences and coherent fallback.'
