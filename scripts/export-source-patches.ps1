. "$PSScriptRoot/environment.ps1"
$pins = Get-Content "$Workspace/dependencies.json" -Raw | ConvertFrom-Json
New-Item -ItemType Directory -Force "$Workspace/patches" | Out-Null
foreach ($entry in @(@('carbon', 'carbon-source', 'carbon-switch.patch'), @('switch_reference', 'reference-nfsmw-nx', 'horizon-sdk.patch'))) {
    $repo = Join-Path $Workspace $entry[1]
    # New source files must be known to git (intent to add) or the diff leaves them out; generated code is never added.
    $new = @(& git -c "safe.directory=$($repo.Replace('\', '/'))" -C $repo ls-files --others --exclude-standard -- '*.c' '*.cpp' '*.h' '*.inc' '*.cmake' | Where-Object { $_ -notmatch '^generated/' })
    if ($new) { & git -c "safe.directory=$($repo.Replace('\', '/'))" -C $repo add -N -- $new; Write-Host "added to the diff: $($new -join ', ')" }
    & git -c "safe.directory=$($repo.Replace('\', '/'))" -C $repo diff $pins.($entry[0]).revision `
        "--output=$Workspace/patches/$($entry[2])" -- '*.c' '*.cpp' '*.h' '*.inc' '*.cmake' ':(glob)**/CMakeLists.txt' CMakeLists.txt nfscarbon_manifest.toml tools/direct_calls.py
    if ($LASTEXITCODE) { throw "Could not export $($entry[2])" }
}
$mesa = "$Workspace/toolchains/mesa-switch"
& git -c "safe.directory=$($mesa.Replace('\','/'))" -C $mesa diff $pins.mesa.revision `
    "--output=$Workspace/patches/mesa-local-paths.patch" -- switch-pkg-config-wrapper.sh src/nouveau/headers/util.py
if ($LASTEXITCODE) { throw 'Could not export the portable Mesa pkg-config patch.' }
