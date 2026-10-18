$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
foreach ($file in Get-ChildItem "$root/scripts", "$root/tests" -Filter '*.ps1') {
    $tokens = $null
    $errors = $null
    [void][Management.Automation.Language.Parser]::ParseFile($file.FullName, [ref]$tokens, [ref]$errors)
    if ($errors.Count) { throw "$($file.Name): $errors" }
}
. "$root/scripts/environment.ps1"
Invoke-Logged 'test-bash-line-endings' (Get-Command git).Source @('-c', "safe.directory=$($root.Replace('\','/'))",
    '-c', 'core.autocrlf=true', 'cat-file', '--filters', 'HEAD:scripts/devkita64-config.sh')
if ([IO.File]::ReadAllBytes("$LogDirectory/test-bash-line-endings.log") -contains 13) {
    throw 'Git checkout filtering introduced CRLF into a bash script.'
}
$fixture = "$LogDirectory/test-process-success.log"
New-Item -ItemType Directory -Force "$LogDirectory" | Out-Null
if (-not (Test-Path $fixture)) { New-Item -ItemType File $fixture | Out-Null }
Assert-FileHash $fixture 'SHA256' (Get-FileHash $fixture -Algorithm SHA256).Hash
$rejected = $false
try { Assert-FileHash $fixture 'SHA256' ('0' * 64) }
catch {
    if ($_.Exception.Message -notmatch 'Source checksum mismatch') { throw }
    $rejected = $true
}
if (-not $rejected) { throw 'Corrupt source archive was incorrectly accepted.' }
Invoke-Logged 'test-process-success' (Get-Command python).Source @('-c', 'import sys; sys.exit(0)')
$failed = $false
try { Invoke-Logged 'test-process-failure' (Get-Command python).Source @('-c', 'import sys; sys.exit(17)') }
catch {
    if ($_.Exception.Message -notmatch 'failed \(17\)') { throw }
    $failed = $true
}
if (-not $failed) { throw 'Build command failures were incorrectly treated as success.' }
$icon = & "$root/scripts/make-nro-icon.ps1" "$root/build/missing-game-data" "$root/build/test-nro-icon"
if ($icon) { throw 'Missing game data must fall back to the default NRO icon.' }
Write-Output 'PASS: PowerShell syntax, exact subprocess failure propagation and icon fallback.'
