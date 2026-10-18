. "$PSScriptRoot/../scripts/environment.ps1"
Initialize-HostCompiler
$testDir = "$Workspace/build/tests/video-wmv3"
New-Item -ItemType Directory -Force $testDir | Out-Null
$sdk = "$Workspace/toolchains/rexglue-built"
$ff = "$Workspace/reference-nfsmw-nx/sdk/thirdparty/FFmpeg"
$compiler = (Get-Command clang++.exe).Source
$flags = @('-std=c++23', '-O2', '-D_DLL', '-D_MT', '-Xclang', '--dependent-lib=msvcrt',
    '-nostartfiles', '-nostdlib', '-fuse-ld=lld-link', '-lkernel32', '-loldnames', '-lbcrypt',
    "-I$Workspace/carbon-source", "-I$ff",
    "$Workspace/tests/video_wmv3_test.cpp", "$Workspace/carbon-source/src/video_wmv3.cpp",
    "$sdk/lib/libavcodec.lib", "$sdk/lib/libavutil.lib", '-o', "$testDir/video_wmv3_test.exe")
Invoke-Logged 'video-wmv3-compile' $compiler $flags
$movies = Get-ChildItem "$Workspace/game-data/Movies/*.wmv" | ForEach-Object FullName
Invoke-Logged 'video-wmv3-test' "$testDir/video_wmv3_test.exe" $movies
