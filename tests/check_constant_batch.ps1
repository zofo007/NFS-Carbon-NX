. "$PSScriptRoot/../scripts/environment.ps1"
$compiler = "$Workspace/toolchains/msys64/mingw64/bin/g++.exe"
$env:PATH = "$Workspace/toolchains/msys64/mingw64/bin;$env:PATH"
$testDir = "$Workspace/build/tests/constant-batch"
New-Item -ItemType Directory -Force $testDir | Out-Null
# Exercise the actual reader/routes/notifications without linking a GPU or host thread.
$cp = Get-Content "$Workspace/carbon-source/renderer/src/command_processor.cpp" -Raw
$header = Get-Content "$Workspace/carbon-source/renderer/src/command_processor.h" -Raw
$renderer = Get-Content "$Workspace/carbon-source/renderer/src/renderer.h" -Raw
$parts = @(
    @('reader', $header, '(?ms)^  struct Reader \{.*?^  \};'),
    @('notify', $renderer, '(?ms)^  void OnFloatConstantWritten\(.*?^  \}'),
    @('notify-batch', $renderer, '(?ms)^  void OnFloatConstantsWritten\(.*?^  \}'),
    @('reader-route', $cp, '(?ms)^void CommandProcessor::WriteRegistersFromReader\(.*?^\}'),
    @('guest-route', $cp, '(?ms)^void CommandProcessor::WriteRegistersFromGuest\(.*?^\}')
)
foreach ($part in $parts) {
    $match = [regex]::Match($part[1], $part[2])
    if (-not $match.Success) { throw "Constant packet fixture source missing: $($part[0])" }
    [IO.File]::WriteAllText("$testDir/$($part[0]).inc", $match.Value, (New-Object Text.UTF8Encoding($false)))
}
Invoke-Logged 'constant-batch-compile' $compiler @('-std=c++23', '-O3', '-msse4.1', '-Wall', '-Wextra', '-Werror',
    "-I$testDir", "-I$Workspace/carbon-source/renderer/src", '-isystem', "$Workspace/reference-nfsmw-nx/sdk/include",
    '-isystem', "$Workspace/reference-nfsmw-nx/sdk/thirdparty/simde",
    '-isystem', "$Workspace/reference-nfsmw-nx/sdk/thirdparty/fmt/include",
    '-isystem', "$Workspace/reference-nfsmw-nx/sdk/thirdparty/spdlog/include",
    "$Workspace/tests/constant_batch_test.cpp", "$Workspace/tests/constant_packet_test.cpp",
    '-o', "$Workspace/build/tests/constant-batch-test.exe")
Invoke-Logged 'constant-batch-test' "$Workspace/build/tests/constant-batch-test.exe" @()
Get-Content "$LogDirectory/constant-batch-test.log"
Invoke-Logged 'constant-blocks-compile' $compiler @('-std=c++23', '-O2', '-Wall', '-Wextra', '-Werror',
    "-I$Workspace/carbon-source/renderer/src", "$Workspace/tests/constant_blocks_test.cpp",
    '-o', "$Workspace/build/tests/constant-blocks-test.exe")
Invoke-Logged 'constant-blocks-test' "$Workspace/build/tests/constant-blocks-test.exe" @()
Get-Content "$LogDirectory/constant-blocks-test.log"
