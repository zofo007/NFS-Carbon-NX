. "$PSScriptRoot/../scripts/environment.ps1"
$env:PATH = "$Workspace/toolchains/msys64/mingw64/bin;$env:PATH"
$testDir = "$Workspace/build/tests/audio-filter-cca20"
New-Item -ItemType Directory -Force $testDir | Out-Null
$partition = Get-Content "$Workspace/carbon-source/generated/default/codegen.partition.json" -Raw | ConvertFrom-Json
$bucket = $partition.assignments.'826CCA20'
if ($null -eq $bucket) { throw 'Missing 826CCA20 partition.' }
$generated = Get-Content "$Workspace/carbon-source/generated/default/nfscarbon_recomp.$bucket.cpp" -Raw
$match = [regex]::Match($generated, '(?s)DEFINE_REX_FUNC\(sub_826CCA20\) \{.*?(?=\r?\nDEFINE_REX_FUNC|\z)')
if (-not $match.Success) { throw 'CCA20 reference not found.' }
$reference = $match.Value.Replace('DEFINE_REX_FUNC(sub_826CCA20)', 'void ReferenceCCA20(PPCContext& __restrict ctx, uint8_t* base)')
[IO.File]::WriteAllText("$testDir/reference.inc",$reference,(New-Object Text.UTF8Encoding($false)))
$flags = @('-std=c++23', '-O3', '-msse4.1', '-mfma', '-ffp-contract=off', '-Wall', '-Wextra',
    '-DSPDLOG_COMPILED_LIB', '-DFMT_HEADER_ONLY', "-I$testDir", "-I$Workspace/carbon-source/src",
    "-I$Workspace/reference-nfsmw-nx/sdk/include", "-I$Workspace/reference-nfsmw-nx/sdk/thirdparty/simde",
    "-I$Workspace/reference-nfsmw-nx/sdk/thirdparty/fmt/include",
    "-I$Workspace/reference-nfsmw-nx/sdk/thirdparty/spdlog/include",
    "$Workspace/tests/audio_filter_cca20_test.cpp", '-o', "$testDir/test.exe")
foreach ($mode in @('target-like','strict-rounding')) {
    $extra = if ($mode -eq 'strict-rounding') { @('-frounding-math') } else { @() }
    Invoke-Logged "audio-cca20-$mode-compile" "$Workspace/toolchains/msys64/mingw64/bin/g++.exe" ($flags + $extra)
    Invoke-Logged "audio-cca20-$mode-test" "$testDir/test.exe" @()
    Get-Content "$LogDirectory/audio-cca20-$mode-test.log"
}
