. "$PSScriptRoot/../scripts/environment.ps1"
$env:PATH = "$Workspace/toolchains/msys64/mingw64/bin;$env:PATH"
$compiler = "$Workspace/toolchains/msys64/mingw64/bin/g++.exe"
$testDir = "$Workspace/build/tests/audio-resampler-15d0"
New-Item -ItemType Directory -Force $testDir | Out-Null
$partition = Get-Content "$Workspace/carbon-source/generated/default/codegen.partition.json" -Raw | ConvertFrom-Json
$bucket = $partition.assignments.'826D15D0'
if ($null -eq $bucket) { throw 'Missing 826D15D0 partition.' }
$generated = Get-Content "$Workspace/carbon-source/generated/default/nfscarbon_recomp.$bucket.cpp" -Raw
$match = [regex]::Match($generated, '(?s)DEFINE_REX_FUNC\(sub_826D15D0\) \{.*?(?=\r?\nDEFINE_REX_FUNC|\z)')
if (-not $match.Success) { throw '15D0 resampler reference not found.' }
$reference = $match.Value.Replace('DEFINE_REX_FUNC(sub_826D15D0)', 'void Reference15D0(PPCContext& __restrict ctx, uint8_t* base)')
[IO.File]::WriteAllText("$testDir/reference.inc", $reference, (New-Object Text.UTF8Encoding($false)))
$flags = @('-std=c++23', '-O3', '-msse4.1', '-mfma', '-ffp-contract=off', '-Wall', '-Wextra',
    '-DSPDLOG_COMPILED_LIB', '-DFMT_HEADER_ONLY', "-I$testDir", "-I$Workspace/carbon-source/src",
    "-I$Workspace/reference-nfsmw-nx/sdk/include", "-I$Workspace/reference-nfsmw-nx/sdk/thirdparty/simde",
    "-I$Workspace/reference-nfsmw-nx/sdk/thirdparty/fmt/include",
    "-I$Workspace/reference-nfsmw-nx/sdk/thirdparty/spdlog/include",
    "$Workspace/tests/audio_resampler_15d0_test.cpp", '-o', "$testDir/test.exe")
foreach ($mode in @('target-like', 'strict-rounding')) {
    $extra = if ($mode -eq 'strict-rounding') { @('-frounding-math') } else { @() }
    Invoke-Logged "audio-15d0-$mode-compile" $compiler ($flags + $extra)
    Invoke-Logged "audio-15d0-$mode-test" "$testDir/test.exe" @()
    Get-Content "$LogDirectory/audio-15d0-$mode-test.log"
}
