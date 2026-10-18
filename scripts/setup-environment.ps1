param([switch]$InstallHostCompiler, [switch]$FetchThirdParty)
. "$PSScriptRoot/environment.ps1"
$ProgressPreference = 'SilentlyContinue'
New-Item -ItemType Directory -Force "$Workspace/toolchains/downloads", "$Workspace/build", "$Workspace/artifacts", "$Workspace/logs" | Out-Null
$pins = Get-Content "$Workspace/dependencies.json" -Raw | ConvertFrom-Json
foreach ($item in @(@('carbon', 'carbon-source'), @('switch_reference', 'reference-nfsmw-nx'), @('mesa', 'toolchains/mesa-switch'))) {
    $path = Join-Path $Workspace $item[1]
    $pin = $pins.($item[0])
    if (-not (Test-Path "$path/.git")) {
        & git clone --no-checkout $pin.url $path
        if ($LASTEXITCODE) { throw "Failed to clone $($pin.url)" }
        & git -c "safe.directory=$($path.Replace('\','/'))" -C $path checkout -b carbon-nx $pin.revision
        if ($LASTEXITCODE) { throw "Failed to select pinned revision $($pin.revision)" }
    }
}
if (-not (Test-Path $CMake) -or -not (Test-Path $Ninja)) {
    & python -m pip install --target "$Workspace/toolchains/python" "cmake==$($pins.cmake)" "ninja==$($pins.ninja)"
    if ($LASTEXITCODE) { throw 'CMake/Ninja provisioning failed.' }
}
if ($InstallHostCompiler) {
    & winget install --exact --id Microsoft.VisualStudio.2022.BuildTools --source winget `
        --accept-package-agreements --accept-source-agreements --disable-interactivity `
        --override '--quiet --wait --norestart --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 --add Microsoft.VisualStudio.Component.VC.Llvm.Clang --add Microsoft.VisualStudio.Component.Windows11SDK.26100'
    if ($LASTEXITCODE) { throw "Build Tools setup failed ($LASTEXITCODE)." }
}
if ($FetchThirdParty -and -not (Test-Path "$Workspace/reference-nfsmw-nx/sdk/thirdparty/fmt/include/fmt/format.h")) {
    Invoke-Logged 'fetch-thirdparty' (Get-Command python).Source @("$Workspace/reference-nfsmw-nx/tools/fetch_thirdparty.py")
}
& "$PSScriptRoot/apply-source-patches.ps1"
Write-Output 'Local tools ready. Switch SDK availability is checked by scripts/check-dependencies.ps1.'
