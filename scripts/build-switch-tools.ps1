param([ValidateRange(1,8)][int]$Jobs = 2)
. "$PSScriptRoot/environment.ps1"
$bash = "$Workspace/toolchains/msys64/usr/bin/bash.exe"
if (-not (Test-Path -LiteralPath $bash)) { throw 'Workspace-local MSYS2 is required.' }
$pins = Get-Content "$Workspace/dependencies.json" -Raw | ConvertFrom-Json
foreach ($item in @(@('devkita64_rules', 'devkita64-rules'), @('general_tools', 'general-tools'), @('switch_tools', 'switch-tools'))) {
    $pin = $pins.($item[0])
    $source = "$Workspace/toolchains/$($item[1])"
    if (-not (Test-Path "$source/.git")) {
        & git clone --no-checkout $pin.url $source
        if ($LASTEXITCODE) { throw "Cannot acquire $($item[1])." }
        & git -c "safe.directory=$($source.Replace('\','/'))" -C $source checkout $pin.revision
        if ($LASTEXITCODE) { throw "Cannot select pinned $($item[1])." }
    }
    $revision = & git -c "safe.directory=$($source.Replace('\','/'))" -C $source rev-parse HEAD
    if ($LASTEXITCODE -or $revision -ne $pin.revision) { throw "Unexpected $($item[1]) revision." }
}
$env:MSYSTEM = 'MINGW64'
$env:CARBON_WORKSPACE = $Workspace.Replace('\', '/')
$env:CARBON_BUILD_JOBS = "$Jobs"
Invoke-Logged 'switch-tools-build' $bash @('--login', "$($PSScriptRoot.Replace('\','/'))/build-switch-tools.sh")
