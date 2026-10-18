$ErrorActionPreference = 'Stop'
$Workspace = Split-Path -Parent $PSScriptRoot
$LogDirectory = if ($env:NFS_BUILD_LOG_DIR) { [IO.Path]::GetFullPath($env:NFS_BUILD_LOG_DIR) } else { Join-Path $Workspace 'logs' }
$CMake = Join-Path $Workspace 'toolchains/python/cmake/data/bin/cmake.exe'
$Ninja = Join-Path $Workspace 'toolchains/python/bin/ninja.exe'
$HostTool = Join-Path $Workspace 'reference-nfsmw-nx/sdk/out/win-amd64/rexglue.exe'
$env:PATH = "$(Split-Path $CMake);$(Split-Path $Ninja);$env:PATH"

function Assert-FileHash([string]$Path, [string]$Algorithm, [string]$Expected) {
    if ((Get-FileHash -LiteralPath $Path -Algorithm $Algorithm).Hash -ne $Expected) {
        throw "Source checksum mismatch: $Path"
    }
}

function Invoke-Logged([string]$Name, [string]$Program, [string[]]$Arguments) {
    $logDir = $LogDirectory
    New-Item -ItemType Directory -Force $logDir | Out-Null
    $quoted = $Arguments | ForEach-Object { '"' + $_ + '"' }
    $info = New-Object System.Diagnostics.ProcessStartInfo
    $info.FileName = $Program
    $info.Arguments = $quoted -join ' '
    $info.WorkingDirectory = $Workspace
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $info
    $stdout = [IO.File]::Open("$logDir/$Name.log", [IO.FileMode]::Create, [IO.FileAccess]::Write, [IO.FileShare]::Read)
    $stderr = [IO.File]::Open("$logDir/$Name.err", [IO.FileMode]::Create, [IO.FileAccess]::Write, [IO.FileShare]::Read)
    try {
        if (-not $process.Start()) { throw "Could not start $Name" }
        $outCopy = $process.StandardOutput.BaseStream.CopyToAsync($stdout)
        $errCopy = $process.StandardError.BaseStream.CopyToAsync($stderr)
        $process.WaitForExit()
        [void]$outCopy.GetAwaiter().GetResult()
        [void]$errCopy.GetAwaiter().GetResult()
        $exitCode = $process.ExitCode
    } finally {
        $stdout.Dispose()
        $stderr.Dispose()
        $process.Dispose()
    }
    if ($exitCode -ne 0) {
        Get-Content "$logDir/$Name.log" -Tail 40
        Get-Content "$logDir/$Name.err" -Tail 40
        throw "$Name failed ($exitCode); see $logDir/$Name.log and .err"
    }
    Write-Output "$Name passed; $logDir/$Name.log"
}

function Initialize-HostCompiler {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Run scripts/setup-environment.ps1 first (C++ Build Tools missing).' }
    $installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $installation) { throw 'C++ Build Tools installation is incomplete or missing.' }
    & "$installation/Common7/Tools/Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
    $llvm = Join-Path $installation 'VC/Tools/Llvm/x64/bin'
    if (-not (Test-Path "$llvm/clang++.exe")) { throw 'C++ Build Tools is missing the LLVM Clang component.' }
    $env:PATH = "$llvm;$env:PATH"
}
