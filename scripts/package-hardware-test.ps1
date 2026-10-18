. "$PSScriptRoot/environment.ps1"
& "$PSScriptRoot/validate-build.ps1"
Invoke-Logged 'hardware-package-game-input' (Get-Command python).Source @("$PSScriptRoot/check_game.py", "$Workspace/game-data")
$hash = (Get-FileHash "$Workspace/artifacts/nfscarbon.nro" -Algorithm SHA256).Hash
$package = "$Workspace/artifacts/hardware-$($hash.Substring(0,12).ToLowerInvariant())"
$title = "$package/switch/nfscarbon"
New-Item -ItemType Directory -Force "$title/game-data/NFS", "$title/game-data/Movies", "$title/user/cache" | Out-Null
$sources = @(
    @("$Workspace/artifacts/nfscarbon.nro", "$title/nfscarbon.nro"),
    @("$Workspace/artifacts/nfscarbon.toml", "$title/nfscarbon.toml"),
    @("$Workspace/game-data/default.xex", "$title/game-data/default.xex"),
    @("$Workspace/docs/HARDWARE_TEST.md", "$package/HARDWARE_TEST.md"),
    @("$Workspace/dependencies.json", "$package/dependencies.json"))
foreach ($directory in @('NFS', 'Movies')) {
    $source = Get-Item -LiteralPath "$Workspace/game-data/$directory"
    if ($source.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Refusing linked game-data directories.' }
    # shortcut: this verified edition is flat; add guarded traversal if a verified edition needs subdirectories.
    foreach ($file in Get-ChildItem -LiteralPath $source.FullName -Force) {
        if ($file.PSIsContainer -or ($file.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw "Unexpected linked/nested game data; preserving originals: $($file.FullName)"
        }
        $sources += ,@($file.FullName, "$title/game-data/$directory/$($file.Name)")
    }
}
foreach ($entry in $sources) {
    $expected = (Get-FileHash -LiteralPath $entry[0] -Algorithm SHA256).Hash
    if (-not (Test-Path -LiteralPath $entry[1])) { Copy-Item -LiteralPath $entry[0] -Destination $entry[1] }
    Assert-FileHash $entry[1] SHA256 $expected
    Write-Output "Verified $(Split-Path $entry[1] -Leaf)."
}
Invoke-Logged 'hardware-package-game-check' (Get-Command python).Source @("$PSScriptRoot/check_game.py", "$title/game-data")
$revisions = [ordered]@{}
foreach ($repo in @('carbon-source', 'reference-nfsmw-nx', 'toolchains/mesa-switch')) {
    $path = "$Workspace/$repo".Replace('\','/')
    $revision = & git -c "safe.directory=$path" -C $path rev-parse HEAD
    if ($LASTEXITCODE) { throw "Cannot record source revision: $repo" }
    $revisions[$repo] = $revision
}
$nro = Get-Item -LiteralPath "$title/nfscarbon.nro"
$receipt = [ordered]@{
    nro_sha256 = $hash
    nro_size_bytes = $nro.Length
    build_utc = $nro.LastWriteTimeUtc.ToString('o')
    hardware_tested = $false
    source_revisions = $revisions
    game_input = (Get-Content "$LogDirectory/hardware-package-game-check.log" -Raw | ConvertFrom-Json)
}
$json = $receipt | ConvertTo-Json -Depth 4
$record = "$package/build-receipt.json"
if (Test-Path -LiteralPath $record) {
    if ((Get-Content $record -Raw).Trim() -ne $json.Trim()) { throw 'Preserving a different existing build receipt.' }
} else {
    [IO.File]::WriteAllText($record, $json, (New-Object Text.UTF8Encoding $false))
}
Write-Output "PRIVATE hardware test package: $package"
Write-Output "NRO SHA256: $hash"
Write-Output 'Contains local game assets: never commit, upload or redistribute this directory. Hardware testing is not performed.'
