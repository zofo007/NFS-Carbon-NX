param([ValidateSet('standard', 'collectors', 'collectors-rl')][string]$Edition = 'standard')
. "$PSScriptRoot/../scripts/environment.ps1"
$env:PATH = "$Workspace/toolchains/msys64/mingw64/bin;$env:PATH"
$ce = $Edition -ne 'standard'
$testDir = "$Workspace/build/tests/native-leaf" + $(if ($ce) { "-$Edition" } else { '' })
New-Item -ItemType Directory -Force $testDir | Out-Null
$gen = "$Workspace/carbon-source/generated/" + $(if ($ce) { $Edition } else { 'default' })
# The test refers to the standard names; Collector's maps them to its own addresses.
$address = @{ '826D1678' = $(if ($ce) { '826D14B0' } else { '826D1678' }); '826CD8D0' = $(if ($ce) { '826CD708' } else { '826CD8D0' })
              '826CD528' = $(if ($ce) { '826CD360' } else { '826CD528' }); '826B56A8' = $(if ($ce) { '826B54E0' } else { '826B56A8' }) }
$leafList = switch ($Edition) { 'collectors' { 'native_leaf_list_collectors.txt' } 'collectors-rl' { 'native_leaf_list_collectors_rl.txt' } default { 'native_leaf_list.txt' } }
[string[]]$leafFlags = if ($Edition -eq 'collectors') { @('--nonvolatile') } else { @() }
# Private fixtures from the current generated code; never committed.
& python "$Workspace/scripts/localize_leaf.py" @leafFlags $gen "$testDir/native_leaf.h" @(Get-Content "$Workspace/scripts/$leafList")
if ($LASTEXITCODE) { throw 'localize_leaf.py failed.' }
$leaf = (Get-Content "$testDir/native_leaf.h" -Raw).Replace('#include "nfscarbon_pch.h"', '')
[IO.File]::WriteAllText("$testDir/native_leaf.h", $leaf, (New-Object Text.UTF8Encoding($false)))
$partition = Get-Content "$gen/codegen.partition.json" -Raw | ConvertFrom-Json
$reference = ''
foreach ($name in @('826D1678', '826CD8D0', '826CD528', '826B56A8')) {
    $bucket = $partition.assignments.($address[$name])
    $generated = Get-Content "$gen/nfscarbon_recomp.$bucket.cpp" -Raw
    $match = [regex]::Match($generated, "(?s)DEFINE_REX_FUNC\(sub_$($address[$name])\) \{.*?(?=\r?\nDEFINE_REX_FUNC|\z)")
    if (-not $match.Success) { throw "$name reference not found." }
    $reference += $match.Value.Replace("DEFINE_REX_FUNC(sub_$($address[$name]))", "void Reference$name(PPCContext& __restrict ctx, uint8_t* base)") + "`n"
}
# Collector's originals call the generated register save/restore millicode; give the test the real bodies.
$helpers = ''
foreach ($helper in ([regex]::Matches($reference, '__(?:save|rest)(?:gpr|fpr|vmx)\w*(?=\(ctx)') | ForEach-Object Value | Sort-Object -Unique)) {
    $file = Select-String -Path "$gen/nfscarbon_recomp.*.cpp" -Pattern "^DEFINE_REX_FUNC\($helper\)" | Select-Object -First 1
    $body = [regex]::Match((Get-Content $file.Path -Raw), "(?s)DEFINE_REX_FUNC\($helper\) \{.*?(?=\r?\nDEFINE_REX_FUNC|\z)").Value
    $helpers += $body.Replace("DEFINE_REX_FUNC($helper)", "extern `"C`" void $helper(PPCContext& __restrict ctx, uint8_t* base)") + "`n"
}
$reference = $helpers + $reference
[IO.File]::WriteAllText("$testDir/reference.inc", $reference, (New-Object Text.UTF8Encoding($false)))
$flags = @('-std=c++23', '-O3', '-msse4.1', '-mfma', '-ffp-contract=off', '-Wall', '-Wextra',
    '-DSPDLOG_COMPILED_LIB', '-DFMT_HEADER_ONLY', "-I$testDir", "-I$Workspace/carbon-source/src",
    "-I$Workspace/reference-nfsmw-nx/sdk/include", "-I$Workspace/reference-nfsmw-nx/sdk/thirdparty/simde",
    "-I$Workspace/reference-nfsmw-nx/sdk/thirdparty/fmt/include",
    "-I$Workspace/reference-nfsmw-nx/sdk/thirdparty/spdlog/include",
    "$Workspace/tests/native_leaf_test.cpp", '-o', "$testDir/test.exe")
foreach ($mode in @('target-like', 'strict-rounding')) {
    [string[]]$extra = if ($mode -eq 'strict-rounding') { @('-frounding-math') } else { @() }
    if ($ce) { $extra += '-DNFSCARBON_COLLECTORS=1'; if ($Edition -eq 'collectors-rl') { $extra += '-DNFSCARBON_COLLECTORS_RL=1' } }
    Invoke-Logged "native-leaf-$mode-compile" "$Workspace/toolchains/msys64/mingw64/bin/g++.exe" ($flags + $extra)
    Invoke-Logged "native-leaf-$mode-test" "$testDir/test.exe" @()
    Get-Content "$LogDirectory/native-leaf-$mode-test.log"
}
