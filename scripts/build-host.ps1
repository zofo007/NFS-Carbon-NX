param([int]$Jobs = 2, [switch]$CodegenOnly)
. "$PSScriptRoot/environment.ps1"
Initialize-HostCompiler
& python "$PSScriptRoot/check_game.py" "$Workspace/game-data"
if ($LASTEXITCODE) { throw 'Game-data validation failed.' }
$sdk = "$Workspace/reference-nfsmw-nx/sdk"
Invoke-Logged 'host-configure' $CMake @('-S', $sdk, '-B', "$Workspace/build/host-sdk", '-G', 'Ninja',
    '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_C_COMPILER=clang', '-DCMAKE_CXX_COMPILER=clang++',
    "-DCMAKE_INSTALL_PREFIX=$Workspace/toolchains/rexglue-built",
    "-DCMAKE_MAKE_PROGRAM=$Ninja", '-DREXGLUE_USE_VULKAN=ON', '-DREXGLUE_USE_D3D12=OFF',
    '-DREXGLUE_ENABLE_TRACY=OFF', '-DREXGLUE_ENABLE_PERF_COUNTERS=OFF')
Invoke-Logged 'host-tool-build' $CMake @('--build', "$Workspace/build/host-sdk", '--target', 'rexglue', '-j', "$Jobs")
$gameCopy = "$Workspace/carbon-source/game/default.xex"
New-Item -ItemType Directory -Force (Split-Path $gameCopy) | Out-Null
if (-not (Test-Path $gameCopy)) {
    Copy-Item -LiteralPath "$Workspace/game-data/default.xex" -Destination $gameCopy
} elseif ((Get-FileHash $gameCopy).Hash -ne (Get-FileHash "$Workspace/game-data/default.xex").Hash) {
    throw 'Existing working XEX differs from game-data. Resolve edition selection before codegen.'
}
Invoke-Logged 'carbon-codegen' $HostTool @('codegen', "$Workspace/carbon-source/nfscarbon_manifest.toml")
if ($CodegenOnly) { return }
Invoke-Logged 'host-sdk-build' $CMake @('--build', "$Workspace/build/host-sdk", '-j', "$Jobs")
Invoke-Logged 'host-sdk-install' $CMake @('--install', "$Workspace/build/host-sdk")
Invoke-Logged 'carbon-host-configure' $CMake @('-S', "$Workspace/carbon-source", '-B', "$Workspace/build/carbon-host", '-G', 'Ninja',
    '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_C_COMPILER=clang', '-DCMAKE_CXX_COMPILER=clang++',
    "-DCMAKE_MAKE_PROGRAM=$Ninja", "-DCMAKE_PREFIX_PATH=$Workspace/toolchains/rexglue-built", "-DCARBON_GPU_SDK_SOURCE_DIR=$sdk",
    '-DREXGLUE_ENABLE_TRACY=OFF', '-DREXGLUE_ENABLE_PERF_COUNTERS=OFF')
Invoke-Logged 'carbon-host-build' $CMake @('--build', "$Workspace/build/carbon-host", '-j', "$Jobs")
