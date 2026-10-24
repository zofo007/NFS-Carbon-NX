# Differential check of the shared native audio hooks against Collector's generated (context-preserving) code.
# The standard checks (check_audio_*.ps1) compare against standard codegen, where cr/xer/r31 are locals; here the
# original keeps them in the context, so every context byte, guest write and host FP state must still match.
param([ValidateSet('collectors', 'collectors-rl')][string]$Edition = 'collectors')
. "$PSScriptRoot/../scripts/environment.ps1"
$env:PATH = "$Workspace/toolchains/msys64/mingw64/bin;$env:PATH"
$compiler = "$Workspace/toolchains/msys64/mingw64/bin/g++.exe"
$gen = "$Workspace/carbon-source/generated/$Edition"
$partition = Get-Content "$gen/codegen.partition.json" -Raw | ConvertFrom-Json
$cases = @(
    @{ Name = 'resampler'; Address = '826CB010'; Test = 'audio_resampler_test.cpp'; Reference = 'ReferenceResample' },
    @{ Name = 'resampler-15d0'; Address = '826D1408'; Test = 'audio_resampler_15d0_test.cpp'; Reference = 'Reference15D0' },
    @{ Name = 'filter-8bb0'; Address = '826C89E8'; Test = 'audio_filter_8bb0_test.cpp'; Reference = 'Reference8BB0' },
    @{ Name = 'filter-cca20'; Address = '826CC858'; Test = 'audio_filter_cca20_test.cpp'; Reference = 'ReferenceCCA20' })
$failed = @()
foreach ($case in $cases) {
    $testDir = "$Workspace/build/tests/audio-$($case.Name)-$Edition"
    New-Item -ItemType Directory -Force $testDir | Out-Null
    $bucket = $partition.assignments.($case.Address)
    if ($null -eq $bucket) { throw "Missing $($case.Address) partition." }
    $source = Get-Content "$gen/nfscarbon_recomp.$bucket.cpp" -Raw
    $match = [regex]::Match($source, "(?s)DEFINE_REX_FUNC\(sub_$($case.Address)\) \{.*?(?=\r?\nDEFINE_REX_FUNC|\z)")
    if (-not $match.Success) { throw "$($case.Name) reference not found." }
    $reference = $match.Value.Replace("DEFINE_REX_FUNC(sub_$($case.Address))", "void $($case.Reference)(PPCContext& __restrict ctx, uint8_t* base)")
    # The originals call the generated register save/restore helpers; give the test the real bodies.
    $helpers = ''
    foreach ($helper in '__savegprlr_28', '__restgprlr_28') {
        if ($match.Value -notmatch [regex]::Escape($helper)) { continue }
        $file = Select-String -Path "$gen/nfscarbon_recomp.*.cpp" -Pattern "^DEFINE_REX_FUNC\($helper\)" | Select-Object -First 1
        $body = [regex]::Match((Get-Content $file.Path -Raw), "(?s)DEFINE_REX_FUNC\($helper\) \{.*?(?=\r?\nDEFINE_REX_FUNC|\z)").Value
        $helpers += $body.Replace("DEFINE_REX_FUNC($helper)", "extern `"C`" void $helper(PPCContext& __restrict ctx, uint8_t* base)") + "`n"
    }
    $reference = $helpers + $reference
    [IO.File]::WriteAllText("$testDir/reference.inc", $reference, (New-Object Text.UTF8Encoding($false)))
    $flags = @('-std=c++23', '-O3', '-msse4.1', '-mfma', '-ffp-contract=off', '-Wall', '-Wextra', '-DNFSCARBON_COLLECTORS=1', $(if ($Edition -eq 'collectors-rl') { '-DNFSCARBON_COLLECTORS_RL=1' } else { '-DNFSCARBON_COLLECTORS=1' }),
        '-DSPDLOG_COMPILED_LIB', '-DFMT_HEADER_ONLY', "-I$testDir", "-I$Workspace/carbon-source/src",
        "-I$Workspace/reference-nfsmw-nx/sdk/include", "-I$Workspace/reference-nfsmw-nx/sdk/thirdparty/simde",
        "-I$Workspace/reference-nfsmw-nx/sdk/thirdparty/fmt/include",
        "-I$Workspace/reference-nfsmw-nx/sdk/thirdparty/spdlog/include",
        "$Workspace/tests/$($case.Test)", '-o', "$testDir/test.exe")
    try {
        Invoke-Logged "audio-$($case.Name)-$Edition-compile" $compiler $flags
        Invoke-Logged "audio-$($case.Name)-$Edition-test" "$testDir/test.exe" @()
        Get-Content "$LogDirectory/audio-$($case.Name)-$Edition-test.log"
    } catch {
        $failed += $case.Name
        Get-Content "$LogDirectory/audio-$($case.Name)-$Edition-test.err" -ErrorAction SilentlyContinue
    }
}
if ($failed) { throw "Differential check failed: $($failed -join ', ')" }
