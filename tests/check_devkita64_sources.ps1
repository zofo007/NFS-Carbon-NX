. "$PSScriptRoot/../scripts/environment.ps1"
$pins = Get-Content "$Workspace/dependencies.json" -Raw | ConvertFrom-Json
$original = "$Workspace/toolchains/devkitA64-buildscripts"
$source = "$Workspace/build/devkita64-patch-test-$([Guid]::NewGuid().ToString('N'))"
Invoke-Logged 'test-devkita64-clone' (Get-Command git).Source @('clone', '--config', 'core.autocrlf=false',
    '--no-checkout', $original, $source)
$safe = "safe.directory=$($source.Replace('\','/'))"
Invoke-Logged 'test-devkita64-checkout' (Get-Command git).Source @('-c', $safe, '-C', $source,
    'checkout', $pins.devkita64_buildscripts.revision)
# Exercise the real revision/patch block against a disposable checkout, not a copy of its logic.
$wrapper = Get-Content "$Workspace/scripts/build-devkita64.ps1" -Raw
$start = $wrapper.IndexOf('$revision = & git')
$end = $wrapper.IndexOf('New-Item -ItemType Directory')
if ($start -lt 0 -or $end -le $start) { throw 'Cannot locate the compiler source-preparation block.' }
$prepare = [scriptblock]::Create($wrapper.Substring($start, $end - $start))
. $prepare
. $prepare
if ($ErrorActionPreference -ne 'Stop') { throw 'Patch probing did not restore strict error handling.' }
& git -c $safe -C $source apply --reverse --check "$Workspace/patches/devkita64-host.patch"
if ($LASTEXITCODE) { throw 'Fresh/idempotent patch application failed.' }
if ([IO.File]::ReadAllBytes("$source/config.guess") -contains 13) { throw 'Host detector has CRLF line endings.' }
$pins.devkita64_buildscripts.revision = '0' * 40
$pins.devkita64_buildscripts.local_revision = '0' * 40
$rejected = $false
try { . $prepare }
catch {
    if ($_.Exception.Message -notmatch 'Unexpected devkitA64 build-script revision') { throw }
    $rejected = $true
}
if (-not $rejected) { throw 'Unpinned compiler source revision was accepted.' }
Write-Output 'PASS: fresh and repeated patch application, LF checkout and unpinned revision rejection.'
