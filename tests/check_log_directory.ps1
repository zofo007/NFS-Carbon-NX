$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$savedLogDir = $env:NFS_BUILD_LOG_DIR
try {
    $env:NFS_BUILD_LOG_DIR = "$root/build/tests/redirected-logs"
    . "$root/scripts/environment.ps1"
    if ($LogDirectory -ne [IO.Path]::GetFullPath($env:NFS_BUILD_LOG_DIR)) {
        throw 'Build logging must honor NFS_BUILD_LOG_DIR before starting any subprocess.'
    }
    Invoke-Logged 'redirect-success' (Get-Command python).Source @('-c', 'print(42)')
    if ((Get-Content "$LogDirectory/redirect-success.log" -Raw).Trim() -ne '42') {
        throw 'Redirected stdout was not captured.'
    }
    $failed = $false
    try { Invoke-Logged 'redirect-failure' (Get-Command python).Source @('-c', 'import sys; sys.exit(17)') }
    catch {
        if ($_.Exception.Message -notmatch 'failed \(17\)' -or
            -not $_.Exception.Message.Contains($LogDirectory)) { throw }
        $failed = $true
    }
    if (-not $failed) { throw 'Redirected logging swallowed a subprocess failure.' }
    Write-Output 'PASS: redirected build logs, stdout and exact failure propagation.'
} finally { $env:NFS_BUILD_LOG_DIR = $savedLogDir }
