. "$PSScriptRoot/../scripts/environment.ps1"
if (Get-Process -Name nfscarbon -ErrorAction SilentlyContinue) {
    throw 'Refusing to rebuild the host renderer while the game may be using its DLL.'
}
if (-not (Test-Path -LiteralPath "$Workspace/build/carbon-host/build.ninja")) {
    throw 'Configure the existing Windows host baseline first.'
}
Initialize-HostCompiler
Invoke-Logged 'carbon-host-renderer-check' $CMake @('--build', "$Workspace/build/carbon-host",
    '--target', 'rexgpu-carbon', '-j', '2')
Write-Output 'PASS: existing Windows native renderer target compiles/links; no game launched or guest code regenerated.'
