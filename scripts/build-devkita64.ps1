param([switch]$SourcesOnly, [ValidateRange(1,8)][int]$Jobs = 2)
. "$PSScriptRoot/environment.ps1"
$bash = "$Workspace/toolchains/msys64/usr/bin/bash.exe"
if (-not (Test-Path -LiteralPath $bash)) { throw 'Workspace-local MSYS2 is required.' }
$pins = Get-Content "$Workspace/dependencies.json" -Raw | ConvertFrom-Json
$source = "$Workspace/toolchains/devkitA64-buildscripts"
if (-not (Test-Path "$source/.git")) {
    & git clone --config core.autocrlf=false --no-checkout $pins.devkita64_buildscripts.url $source
    if ($LASTEXITCODE) { throw 'Cannot acquire devkitA64 build scripts.' }
    & git -c "safe.directory=$($source.Replace('\','/'))" -C $source checkout $pins.devkita64_buildscripts.revision
    if ($LASTEXITCODE) { throw 'Cannot select pinned devkitA64 revision.' }
}
$revision = & git -c "safe.directory=$($source.Replace('\','/'))" -C $source rev-parse HEAD
if ($LASTEXITCODE -or $revision -notin @($pins.devkita64_buildscripts.revision, $pins.devkita64_buildscripts.local_revision)) {
    throw 'Unexpected devkitA64 build-script revision.'
}
$safe = "safe.directory=$($source.Replace('\','/'))"
$patch = "$Workspace/patches/devkita64-host.patch"
$ErrorActionPreference = 'Continue' # A failed reverse check is expected on a fresh checkout.
try { & git -c $safe -C $source apply --reverse --check $patch 2>$null }
finally { $ErrorActionPreference = 'Stop' }
if ($LASTEXITCODE) {
    & git -c $safe -C $source apply --check $patch
    if ($LASTEXITCODE) { throw 'Host-detection patch conflicts with local build-script changes.' }
    & git -c $safe -C $source apply $patch
    if ($LASTEXITCODE) { throw 'Cannot apply the Windows host-detection fix.' }
}
New-Item -ItemType Directory -Force "$Workspace/toolchains/downloads" | Out-Null
foreach ($archive in $pins.devkita64_sources) {
    $file = Join-Path "$Workspace/toolchains/downloads" ([Uri]$archive.url).Segments[-1]
    if (-not (Test-Path -LiteralPath $file)) {
        Invoke-Logged ('download-' + (Split-Path $file -Leaf)) (Get-Command curl.exe).Source @(
            '--fail', '--location', '--retry', '2', '--max-time', '900', $archive.url, '--output', "$file.part")
        Assert-FileHash "$file.part" $archive.algorithm $archive.hash
        Move-Item -LiteralPath "$file.part" -Destination $file
    }
    Assert-FileHash $file $archive.algorithm $archive.hash
    Write-Output "Verified $(Split-Path $file -Leaf)."
}
if ($SourcesOnly) { return }
$env:MSYSTEM = 'MINGW64'
$env:CARBON_WORKSPACE = $Workspace.Replace('\', '/')
$env:CARBON_BUILD_JOBS = "$Jobs"
Invoke-Logged 'devkita64-source-build' $bash @('--login', "$($PSScriptRoot.Replace('\','/'))/build-devkita64.sh")
& "$PSScriptRoot/fix-devkita64-limits.ps1"
& "$Workspace/tests/check_binutils.ps1"
