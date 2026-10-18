. "$PSScriptRoot/../scripts/environment.ps1"
$env:PATH = "$Workspace/toolchains/msys64/mingw64/bin;$env:PATH"
$testDir = "$Workspace/build/tests/native-leaf"
New-Item -ItemType Directory -Force $testDir | Out-Null
$gen = "$Workspace/carbon-source/generated/default"
# Private fixtures from the current generated code; never committed.
& python "$Workspace/scripts/localize_leaf.py" $gen "$testDir/native_leaf.h" @(Get-Content "$Workspace/scripts/native_leaf_list.txt")
if ($LASTEXITCODE) { throw 'localize_leaf.py failed.' }
$leaf = (Get-Content "$testDir/native_leaf.h" -Raw).Replace('#include "nfscarbon_pch.h"', '')
[IO.File]::WriteAllText("$testDir/native_leaf.h", $leaf, (New-Object Text.UTF8Encoding($false)))
$partition = Get-Content "$gen/codegen.partition.json" -Raw | ConvertFrom-Json
$reference = ''
foreach ($name in @('826D1678', '826CD8D0', '826CD528', '826B56A8')) {
    $bucket = $partition.assignments.$name
    $generated = Get-Content "$gen/nfscarbon_recomp.$bucket.cpp" -Raw
    $match = [regex]::Match($generated, "(?s)DEFINE_REX_FUNC\(sub_$name\) \{.*?(?=
?
DEFINE_REX_FUNC|\z)")
    if (-not $match.Success) { throw "$name reference not found." }
    $reference += $match.Value.Replace("DEFINE_REX_FUNC(sub_$name)", "void Reference$name(PPCContext& __restrict ctx, uint8_t* base)") + "`n"
}
[IO.File]::WriteAllText("$testDir/reference.inc", $reference, (New-Object Text.UTF8Encoding($false)))
$flags = @('-std=c++23', '-O3', '-msse4.1', '-mfma', '-ffp-contract=off', '-Wall', '-Wextra',
    '-DSPDLOG_COMPILED_LIB', '-DFMT_HEADER_ONLY', "-I$testDir", "-I$Workspace/carbon-source/src",
    "-I$Workspace/reference-nfsmw-nx/sdk/include", "-I$Workspace/reference-nfsmw-nx/sdk/thirdparty/simde",
    "-I$Workspace/reference-nfsmw-nx/sdk/thirdparty/fmt/include",
    "-I$Workspace/reference-nfsmw-nx/sdk/thirdparty/spdlog/include",
    "$Workspace/tests/native_leaf_test.cpp", '-o', "$testDir/test.exe")
foreach ($mode in @('target-like', 'strict-rounding')) {
    $extra = if ($mode -eq 'strict-rounding') { @('-frounding-math') } else { @() }
    Invoke-Logged "native-leaf-$mode-compile" "$Workspace/toolchains/msys64/mingw64/bin/g++.exe" ($flags + $extra)
    Invoke-Logged "native-leaf-$mode-test" "$testDir/test.exe" @()
    Get-Content "$LogDirectory/native-leaf-$mode-test.log"
}
