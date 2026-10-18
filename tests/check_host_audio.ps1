. "$PSScriptRoot/../scripts/environment.ps1"
if (-not (Test-Path -LiteralPath "$Workspace/build/host-sdk/build.ninja")) {
    throw 'Configure the existing Windows SDK baseline first.'
}
Initialize-HostCompiler
Invoke-Logged 'host-audio-check' $CMake @('--build', "$Workspace/build/host-sdk",
    '--target', 'rexaudio', '-j', '2')
Write-Output 'PASS: actual Windows audio/XMA library target compiles; no game launched or guest code regenerated.'
