param([string]$DevkitPro = $env:DEVKITPRO, [string]$MesaSdk = $env:REXGLUE_SWITCH_NVK_SDK)
. "$PSScriptRoot/environment.ps1"
if (-not $DevkitPro) { $DevkitPro = "$Workspace/toolchains/msys64/opt/devkitpro" }
if (-not $MesaSdk) { $MesaSdk = "$Workspace/toolchains/mesa-switch/mesa-unified-install/opt/devkitpro/portlibs/switch" }
$required = @($CMake, $Ninja, $HostTool, "$DevkitPro/devkitA64/bin/aarch64-none-elf-g++.exe",
    "$DevkitPro/devkitA64/aarch64-none-elf/lib/libc.a", "$DevkitPro/devkitA64/aarch64-none-elf/lib/libstdc++.a",
    "$DevkitPro/libnx/switch.specs", "$DevkitPro/libnx/lib/libnx.a", "$DevkitPro/tools/bin/elf2nro.exe",
    "$DevkitPro/tools/bin/nacptool.exe", "$MesaSdk/lib/libvulkan.a",
    "$DevkitPro/portlibs/switch/lib/libexpat.a", "$DevkitPro/portlibs/switch/lib/libzstd.a", "$DevkitPro/portlibs/switch/lib/libz.a")
$missing = @($required | Where-Object { -not (Test-Path -LiteralPath $_) })
foreach ($file in $required) { Write-Output "$(if (Test-Path -LiteralPath $file) { 'OK' } else { 'MISSING' }) $file" }
if ($missing.Count) { throw "$($missing.Count) build dependencies unavailable." }
