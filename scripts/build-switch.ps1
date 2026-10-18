param([string]$DevkitPro = $env:DEVKITPRO, [string]$MesaSdk = $env:REXGLUE_SWITCH_NVK_SDK, [int]$Jobs = 2)
. "$PSScriptRoot/environment.ps1"
if (-not $DevkitPro) { $DevkitPro = "$Workspace/toolchains/msys64/opt/devkitpro" }
if (-not $MesaSdk) { $MesaSdk = "$Workspace/toolchains/mesa-switch/mesa-unified-install/opt/devkitpro/portlibs/switch" }
& "$PSScriptRoot/check-dependencies.ps1" -DevkitPro $DevkitPro -MesaSdk $MesaSdk
& python "$PSScriptRoot/check_game.py" "$Workspace/game-data"
if ($LASTEXITCODE) { throw 'Game-data validation failed.' }
if (-not (Test-Path "$Workspace/carbon-source/generated/default/sources.cmake")) { throw 'Run build-host.ps1 -CodegenOnly first.' }
# Register-local copies of the listed leaf functions, from the generated code (never committed).
& python "$PSScriptRoot/localize_leaf.py" "$Workspace/carbon-source/generated/default" `
    "$Workspace/carbon-source/generated/default/native_leaf.h" @(Get-Content "$PSScriptRoot/native_leaf_list.txt")
if ($LASTEXITCODE) { throw 'Native leaf generation failed.' }
$Icon = & "$PSScriptRoot/make-nro-icon.ps1" "$Workspace/game-data" "$Workspace/build/nro-icon"
$env:DEVKITPRO = $DevkitPro.Replace('\', '/')
if ($env:DEVKITPRO -eq "$($Workspace.Replace('\','/'))/toolchains/msys64/opt/devkitpro") {
    $env:PATH = "$Workspace/toolchains/msys64/mingw64/bin;$env:PATH"
}
Invoke-Logged 'switch-configure' $CMake @('-S', "$Workspace/carbon-source", '-B', "$Workspace/build/switch", '-G', 'Ninja',
    '-DCMAKE_BUILD_TYPE=Release', "-DCMAKE_MAKE_PROGRAM=$Ninja", "-DDEVKITPRO=$env:DEVKITPRO",
    "-DCMAKE_TOOLCHAIN_FILE=$Workspace/reference-nfsmw-nx/tools/switch/cmake/switch-devkitA64.cmake",
    "-DREXSDK_DIR=$Workspace/reference-nfsmw-nx/sdk", "-DREXGLUE_HOST_REXGLUE_TOOL=$HostTool", "-DREXGLUE_SWITCH_NVK_SDK=$MesaSdk", "-DNFSCARBON_ICON=$Icon")
Invoke-Logged 'switch-build' $CMake @('--build', "$Workspace/build/switch", '-j', "$Jobs")
& python "$PSScriptRoot/validate_build.py" "$Workspace/build/switch/nfscarbon.nro" "$Workspace/build/switch/nfscarbon"
if ($LASTEXITCODE) { throw 'NRO/ELF validation failed.' }
Copy-Item -LiteralPath "$Workspace/build/switch/nfscarbon.nro" -Destination "$Workspace/artifacts/nfscarbon.nro"
Copy-Item -LiteralPath "$Workspace/config/nfscarbon.toml" -Destination "$Workspace/artifacts/nfscarbon.toml"
