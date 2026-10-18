. "$PSScriptRoot/../scripts/environment.ps1"
$compiler = "$Workspace/toolchains/msys64/mingw64/bin/g++.exe"
$env:PATH = "$Workspace/toolchains/msys64/mingw64/bin;$env:PATH"
New-Item -ItemType Directory -Force "$Workspace/build/tests" | Out-Null
Invoke-Logged 'frame-pacing-compile' $compiler @('-std=c++23', '-O3', '-Wall', '-Wextra', '-Werror', '-DFMT_HEADER_ONLY',
    "-I$Workspace/carbon-source/renderer/src", "-I$Workspace/reference-nfsmw-nx/sdk/thirdparty/fmt/include",
    "$Workspace/tests/frame_pacing_test.cpp", '-o', "$Workspace/build/tests/frame-pacing-test.exe")
$synthetic = "$Workspace/build/tests/frame-pacing-synthetic.log"
Invoke-Logged 'frame-pacing-test' "$Workspace/build/tests/frame-pacing-test.exe" @($synthetic)
Get-Content "$LogDirectory/frame-pacing-test.log"
# The analyzer must accept the real formatter's output and rebuild the same windows.
Invoke-Logged 'frame-pacing-analyze' (Get-Command python).Source @("$Workspace/scripts/analyze_frame_pacing.py", $synthetic)
$report = Get-Content -Raw "$LogDirectory/frame-pacing-analyze.log"
foreach ($expected in 'Windows: 3; frames 500; stored samples 500', '16.7 ms multiples', '1: 300 (60.0%)',
    'within +/-1.5 ms: 100.0% of frames', '>=3 vblanks (') {
    if (-not $report.Contains($expected)) { throw "Frame pacing analyzer output lacks: $expected" }
}
$report
Write-Output 'PASS: frame pacing window/percentile/vblank/capacity/no-allocation cases and C++ formatter -> Python analyzer contract.'
