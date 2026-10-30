param([string]$DevkitPro = $env:DEVKITPRO, [string]$MesaSdk = $env:REXGLUE_SWITCH_NVK_SDK, [int]$Jobs = 2,
      [ValidateSet('standard', 'collectors', 'collectors-rl')][string]$Edition = 'standard', [string]$HotOrderFile = '', [switch]$FastLoads, [switch]$NoFastLoads)
. "$PSScriptRoot/environment.ps1"
if (-not $DevkitPro) { $DevkitPro = "$Workspace/toolchains/msys64/opt/devkitpro" }
if (-not $MesaSdk) { $MesaSdk = "$Workspace/toolchains/mesa-switch/mesa-unified-install/opt/devkitpro/portlibs/switch" }
& "$PSScriptRoot/check-dependencies.ps1" -DevkitPro $DevkitPro -MesaSdk $MesaSdk
$ce = $Edition -ne 'standard'
# Plain guest loads by default (Standard paired hardware test: 13-17 pct less render-thread CPU, no crash). The Collector's editions
# get the same transform; the scan keeps volatile loads in their own wait-like functions. -NoFastLoads builds the old way.
$useFastLoads = (-not $NoFastLoads)
$suffix = if ($ce) { "-$Edition" } else { '' }
$gameRoot = if ($ce) { "$Workspace/game-data-collectors" } else { "$Workspace/game-data" }
$generated = if ($ce) { "$Workspace/carbon-source/generated/$Edition" } else { "$Workspace/carbon-source/generated/default" }
$buildDirectory = "$Workspace/build/switch$suffix"
$artifactDirectory = if ($ce) { "$Workspace/artifacts/$Edition" } else { "$Workspace/artifacts" }
$sdkOutputDirectory = if ($ce) { "$buildDirectory/sdk-out" } else { "$Workspace/reference-nfsmw-nx/sdk/out/switch-arm64" }
& python "$PSScriptRoot/check_game.py" $gameRoot $(if ($ce) { 'collectors' } else { 'standard' })
if ($LASTEXITCODE) { throw 'Game-data validation failed.' }
Invoke-Logged "switch$suffix-codegen" (Get-Command python).Source @('-m', 'scripts.codegen', '--edition', $Edition)
# FAST_LOADS leaves its changes in the generated files; undo them first so the leaf step and a normal build see the original code.
& python "$PSScriptRoot/fast_loads_apply.py" revert $generated
# Register-local copies of the listed leaf functions, from the generated code (never committed).
$leafList = switch ($Edition) { 'collectors' { 'native_leaf_list_collectors.txt' } 'collectors-rl' { 'native_leaf_list_collectors_rl.txt' } default { 'native_leaf_list.txt' } }
# Collector's keeps non-volatile registers in the context, so its leaf copies localize those as well.
[string[]]$leafFlags = if ($Edition -eq 'collectors') { @('--nonvolatile') } else { @() }  # collectors-rl is register-local like standard
& python "$PSScriptRoot/localize_leaf.py" @leafFlags $generated `
    "$generated/native_leaf.h" @(Get-Content "$PSScriptRoot/$leafList")
if ($LASTEXITCODE) { throw 'Native leaf generation failed.' }
if ($useFastLoads) { & python "$PSScriptRoot/fast_loads_apply.py" apply $generated; if ($LASTEXITCODE) { throw 'FAST_LOADS transform failed.' } }
$Icon = & "$PSScriptRoot/make-nro-icon.ps1" $gameRoot "$Workspace/build/nro-icon$suffix"
$env:DEVKITPRO = $DevkitPro.Replace('\', '/')
if ($env:DEVKITPRO -eq "$($Workspace.Replace('\','/'))/toolchains/msys64/opt/devkitpro") {
    $env:PATH = "$Workspace/toolchains/msys64/mingw64/bin;$env:PATH"
}
Invoke-Logged "switch$suffix-configure" $CMake @('-S', "$Workspace/carbon-source", '-B', $buildDirectory, '-G', 'Ninja',
    '-DCMAKE_BUILD_TYPE=Release', "-DCMAKE_MAKE_PROGRAM=$Ninja", "-DDEVKITPRO=$env:DEVKITPRO",
    "-DCMAKE_TOOLCHAIN_FILE=$Workspace/reference-nfsmw-nx/tools/switch/cmake/switch-devkitA64.cmake",
    "-DREXSDK_DIR=$Workspace/reference-nfsmw-nx/sdk", "-DREXGLUE_HOST_REXGLUE_TOOL=$HostTool", "-DREXGLUE_SWITCH_NVK_SDK=$MesaSdk", "-DNFSCARBON_ICON=$Icon", "-DNFSCARBON_EDITION=$Edition", "-DREXGLUE_OUTPUT_DIR=$sdkOutputDirectory", "-DNFSCARBON_HOT_ORDER=$(if ($HotOrderFile) { 'ON' } else { 'OFF' })", "-DNFSCARBON_HOT_ORDER_FILE=$HotOrderFile", "-DNFSCARBON_FAST_LOADS=$(if ($useFastLoads) { 'ON' } else { 'OFF' })")
Invoke-Logged "switch$suffix-build" $CMake @('--build', $buildDirectory, '-j', "$Jobs")
& python "$PSScriptRoot/validate_build.py" "$buildDirectory/nfscarbon.nro" "$buildDirectory/nfscarbon"
if ($LASTEXITCODE) { throw 'NRO/ELF validation failed.' }
New-Item -ItemType Directory -Force $artifactDirectory | Out-Null
Copy-Item -LiteralPath "$buildDirectory/nfscarbon.nro" -Destination "$artifactDirectory/nfscarbon$suffix.nro"
$config = if ($ce) { 'nfscarbon.collectors.toml' } else { 'nfscarbon.toml' }
Copy-Item -LiteralPath "$Workspace/config/$config" -Destination "$artifactDirectory/nfscarbon.toml"
