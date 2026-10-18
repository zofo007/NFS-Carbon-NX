. "$PSScriptRoot/../scripts/environment.ps1"
$env:PATH = "$Workspace/toolchains/msys64/mingw64/bin;$env:PATH"
$compiler = "$Workspace/toolchains/msys64/mingw64/bin/g++.exe"
New-Item -ItemType Directory -Force "$Workspace/build/tests" | Out-Null
Invoke-Logged 'texture-bindings-compile' $compiler @('-std=c++23', '-O2', '-msse4.1', '-Wall', '-Wextra',
    "-I$Workspace/carbon-source/renderer/src", "-I$Workspace/reference-nfsmw-nx/sdk/include",
    "-I$Workspace/reference-nfsmw-nx/sdk/thirdparty/vulkan-headers/include",
    "-I$Workspace/reference-nfsmw-nx/sdk/thirdparty/simde",
    "-I$Workspace/reference-nfsmw-nx/sdk/thirdparty/fmt/include",
    "-I$Workspace/reference-nfsmw-nx/sdk/thirdparty/spdlog/include",
    "$Workspace/tests/texture_binding_cache_test.cpp", '-o', "$Workspace/build/tests/texture-bindings-test.exe")
Invoke-Logged 'texture-bindings-test' "$Workspace/build/tests/texture-bindings-test.exe" @()
Write-Output 'PASS: real texture-binding cache bit identity, exact parameter restoration and differential frame/resolve/stage cases.'
Invoke-Logged 'texture-lookup-compile' $compiler @('-std=c++23', '-O2', '-Wall', '-Wextra',
    "-I$Workspace/carbon-source/renderer/src", "$Workspace/tests/texture_lookup_test.cpp",
    '-o', "$Workspace/build/tests/texture-lookup-test.exe")
Invoke-Logged 'texture-lookup-test' "$Workspace/build/tests/texture-lookup-test.exe" @()
Get-Content "$LogDirectory/texture-lookup-test.log"
Invoke-Logged 'texture-hasher-compile' $compiler @('-std=c++23', '-O2', '-Wall', '-Wextra', '-pthread',
    "-I$Workspace/carbon-source/renderer/src", "$Workspace/tests/texture_hasher_test.cpp",
    '-o', "$Workspace/build/tests/texture-hasher-test.exe")
Invoke-Logged 'texture-hasher-test' "$Workspace/build/tests/texture-hasher-test.exe" @()
Get-Content "$LogDirectory/texture-hasher-test.log"
Invoke-Logged 'texture-untile-compile' $compiler @('-std=c++23', '-O2', '-msse4.1', '-Wall', '-Wextra',
    '-DSPDLOG_COMPILED_LIB', '-DFMT_HEADER_ONLY',
    "-I$Workspace/carbon-source/renderer/src", "-I$Workspace/reference-nfsmw-nx/sdk/include",
    "-I$Workspace/reference-nfsmw-nx/sdk/thirdparty/vulkan-headers/include",
    "-I$Workspace/reference-nfsmw-nx/sdk/thirdparty/simde",
    "-I$Workspace/reference-nfsmw-nx/sdk/thirdparty/fmt/include",
    "-I$Workspace/reference-nfsmw-nx/sdk/thirdparty/spdlog/include",
    "$Workspace/tests/texture_untile_test.cpp", "$Workspace/carbon-source/renderer/src/texture_decode.cpp",
    '-o', "$Workspace/build/tests/texture-untile-test.exe")
Invoke-Logged 'texture-untile-test' "$Workspace/build/tests/texture-untile-test.exe" @()
Get-Content "$LogDirectory/texture-untile-test.log"
