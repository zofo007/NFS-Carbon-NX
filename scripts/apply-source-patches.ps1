. "$PSScriptRoot/environment.ps1"
$ErrorActionPreference = 'Continue' # A failed reverse check is expected for a fresh checkout.
foreach ($entry in @(@('carbon-source', 'patches/carbon-switch.patch'), @('reference-nfsmw-nx', 'patches/horizon-sdk.patch'),
    @('toolchains/mesa-switch', 'reference-nfsmw-nx/mesa/mesa-switch-nfsmw.patch'), @('toolchains/mesa-switch', 'patches/mesa-local-paths.patch'))) {
    $repo = Join-Path $Workspace $entry[0]
    $patch = Join-Path $Workspace $entry[1]
    if (-not (Test-Path $patch)) { throw "Source patch missing: $patch" }
    $safe = "safe.directory=$($repo.Replace('\', '/'))"
    & git -c $safe -C $repo apply --reverse --check $patch 2>$null
    if ($LASTEXITCODE -eq 0) { continue }
    & git -c $safe -C $repo apply --check $patch
    if ($LASTEXITCODE) { throw "Patch conflicts with local changes in $($entry[0]); nothing overwritten." }
    & git -c $safe -C $repo apply $patch
    if ($LASTEXITCODE) { throw "Could not apply $($entry[1])" }
}
