. "$PSScriptRoot/../scripts/environment.ps1"
$generated = "$Workspace/carbon-source/generated/default"
if (-not (Select-String -LiteralPath "$Workspace/carbon-source/nfscarbon_manifest.toml" `
    -Pattern '^0x822AF8A0\s*=\s*\{\}')) {
    throw 'Missing first-mission callback hint: 822AF8A0.'
}
if (-not (Select-String -LiteralPath "$generated/nfscarbon_init.cpp" `
    -SimpleMatch '{ 0x822AF8A0, sub_822AF8A0 }')) {
    throw 'First-mission callback is not registered; regenerate guest sources.'
}
$partition = Get-Content -LiteralPath "$generated/codegen.partition.json" -Raw | ConvertFrom-Json
$bucket = $partition.assignments.'822AF8A0'
if ($null -eq $bucket -or -not (Select-String -LiteralPath "$generated/nfscarbon_recomp.$bucket.cpp" `
    -SimpleMatch 'DEFINE_REX_FUNC(sub_822AF8A0) {')) {
    throw 'First-mission callback has no generated implementation.'
}
$nm = "$Workspace/toolchains/msys64/opt/devkitpro/devkitA64/bin/aarch64-none-elf-nm.exe"
$env:PATH = "$Workspace/toolchains/msys64/mingw64/bin;$env:PATH"
$symbols = @(& $nm --defined-only "$Workspace/build/switch/nfscarbon")
if ($LASTEXITCODE -or -not ($symbols -match ' [TW] __imp__sub_822AF8A0$') -or
    -not ($symbols -match ' [TW] sub_822AF8A0$')) {
    throw 'First-mission callback is not linked into the Switch executable; rebuild it.'
}
$registration = @(& $Ninja -C "$Workspace/build/switch" -t commands `
    'CMakeFiles/nfscarbon_recomp.dir/generated/default/nfscarbon_register.cpp.obj')
if ($LASTEXITCODE -or $registration[-1] -notmatch ' -O0 ' -or $registration[-1] -match ' -include ') {
    throw 'Startup registration is missing its cold-file optimization exception.'
}
$guest = @(& $Ninja -C "$Workspace/build/switch" -t commands `
    'CMakeFiles/nfscarbon_recomp.dir/generated/default/nfscarbon_recomp.162.cpp.obj')
if ($LASTEXITCODE -or $guest[-1] -notmatch ' -O3 ' -or $guest[-1] -match ' -O0 ') {
    throw 'The registration exception must not disable guest-function optimization.'
}
Write-Output 'PASS: first-mission callback 822AF8A0 is hinted, implemented, registered and linked.'
$ringHook = @($symbols | Select-String -Pattern '^([0-9a-fA-F]+) T sub_826E0250$')
$ringOriginal = @($symbols | Select-String -Pattern '^([0-9a-fA-F]+) T __imp__sub_826E0250$')
if ($ringHook.Count -ne 1 -or $ringOriginal.Count -ne 1 -or
    $ringHook[0].Matches[0].Groups[1].Value -eq $ringOriginal[0].Matches[0].Groups[1].Value) {
    throw 'Ring wait requires a strong hook distinct from the preserved original watchdog.'
}
Write-Output 'PASS: Carbon ring-wait hook is strong and the original watchdog remains linked separately.'
$audioHook = @($symbols | Select-String -Pattern '^([0-9a-fA-F]+) T sub_826CB1D8$')
$audioOriginal = @($symbols | Select-String -Pattern '^([0-9a-fA-F]+) T __imp__sub_826CB1D8$')
if ($audioHook.Count -ne 1 -or $audioOriginal.Count -ne 1 -or
    $audioHook[0].Matches[0].Groups[1].Value -eq $audioOriginal[0].Matches[0].Groups[1].Value) {
    throw 'Native resampler requires a strong hook distinct from the preserved original.'
}
Write-Output 'PASS: native Carbon resampler hook is strong; original remains linked for same-binary A/B.'
$hook15d0 = @($symbols | Select-String -Pattern '^([0-9a-fA-F]+) T sub_826D15D0$')
$original15d0 = @($symbols | Select-String -Pattern '^([0-9a-fA-F]+) T __imp__sub_826D15D0$')
if ($hook15d0.Count -ne 1 -or $original15d0.Count -ne 1 -or
    $hook15d0[0].Matches[0].Groups[1].Value -eq $original15d0[0].Matches[0].Groups[1].Value) {
    throw 'Native 826D15D0 requires a strong hook distinct from its preserved original.'
}
Write-Output 'PASS: native 826D15D0 hook is strong; original remains linked for same-binary A/B.'
$filterHook = @($symbols | Select-String -Pattern '^([0-9a-fA-F]+) T sub_826C8BB0$')
$filterOriginal = @($symbols | Select-String -Pattern '^([0-9a-fA-F]+) T __imp__sub_826C8BB0$')
if ($filterHook.Count -ne 1 -or $filterOriginal.Count -ne 1 -or
    $filterHook[0].Matches[0].Groups[1].Value -eq $filterOriginal[0].Matches[0].Groups[1].Value) {
    throw 'Native 826C8BB0 requires a strong hook distinct from its preserved original.'
}
Write-Output 'PASS: native 826C8BB0 hook is strong; original remains linked for same-binary A/B.'
$cca20Hook = @($symbols | Select-String -Pattern '^([0-9a-fA-F]+) T sub_826CCA20$')
$cca20Original = @($symbols | Select-String -Pattern '^([0-9a-fA-F]+) T __imp__sub_826CCA20$')
if ($cca20Hook.Count -ne 1 -or $cca20Original.Count -ne 1 -or
    $cca20Hook[0].Matches[0].Groups[1].Value -eq $cca20Original[0].Matches[0].Groups[1].Value) {
    throw 'Native 826CCA20 requires a strong hook distinct from its preserved original.'
}
Write-Output 'PASS: native 826CCA20 hook is strong; original remains linked for same-binary A/B.'
foreach ($name in 'sub_826D1678', 'sub_826CD8D0', 'sub_826CD528', 'sub_826B56A8') {
    $hook = @($symbols | Select-String -Pattern "^([0-9a-fA-F]+) T $name`$")
    $original = @($symbols | Select-String -Pattern "^([0-9a-fA-F]+) T __imp__$name`$")
    if ($hook.Count -ne 1 -or $original.Count -ne 1 -or
        $hook[0].Matches[0].Groups[1].Value -eq $original[0].Matches[0].Groups[1].Value) {
        throw "Register-local leaf $name requires a strong hook distinct from its original."
    }
}
Write-Output 'PASS: register-local leaf hooks (826D1678, 826CD8D0, 826CD528, 826B56A8) are strong; originals remain linked.'
foreach ($name in 'sub_824F0210', 'sub_8250B4D0', 'sub_824EED60', 'sub_824EF830', 'sub_824EFB70', 'sub_824EF1A8', 'sub_824EE110') {
    $hook = @($symbols | Select-String -Pattern "^([0-9a-fA-F]+) T $name`$")
    $original = @($symbols | Select-String -Pattern "^([0-9a-fA-F]+) T __imp__$name`$")
    if ($hook.Count -ne 1 -or $original.Count -ne 1 -or
        $hook[0].Matches[0].Groups[1].Value -eq $original[0].Matches[0].Groups[1].Value) {
        throw "Render pass profiling requires a strong hook for $name distinct from its original."
    }
}
Write-Output 'PASS: render pass profiling hooks are strong; all seven original pass functions remain linked.'
$hook = @($symbols | Select-String -Pattern '^([0-9a-fA-F]+) T sub_82943548$')
$original = @($symbols | Select-String -Pattern '^([0-9a-fA-F]+) T __imp__sub_82943548$')
if ($hook.Count -ne 1 -or $original.Count -ne 1 -or
    $hook[0].Matches[0].Groups[1].Value -eq $original[0].Matches[0].Groups[1].Value) {
    throw 'Frame handoff requires a strong hook for the game Sleep sub_82943548 distinct from its original.'
}
Write-Output 'PASS: frame handoff Sleep hook is strong; the original Sleep remains linked for every other call.'
foreach ($name in 'sub_826C6AB0', 'sub_82A4C630') {
    $hook = @($symbols | Select-String -Pattern "^([0-9a-fA-F]+) T $name`$")
    $original = @($symbols | Select-String -Pattern "^([0-9a-fA-F]+) T __imp__$name`$")
    if ($hook.Count -ne 1 -or $original.Count -ne 1 -or
        $hook[0].Matches[0].Groups[1].Value -eq $original[0].Matches[0].Groups[1].Value) {
        throw "Audio server wait requires a strong hook for $name distinct from its original."
    }
}
Write-Output 'PASS: audio server wait hooks (mixer loop, SubmitPacket) are strong; both originals remain linked.'
