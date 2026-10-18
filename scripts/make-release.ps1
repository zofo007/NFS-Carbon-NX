param(
    [string]$Version = (Select-String -Path "$PSScriptRoot/../CHANGELOG.md" -Pattern '^## (\d\S*)' | Select-Object -First 1).Matches[0].Groups[1].Value,
    [string]$Nro = '',
    [string]$Toml = '',
    [switch]$SourceOnly   # skip the NRO zip: only build release/src (used by make-public-branch.ps1)
)
# Builds release/ from tracked files and a verified NRO. Nothing is uploaded or published.
#   release/nfscarbon-<version>.zip         the SD card content: nfscarbon.nro, nfscarbon.toml, notices
#   release/nfscarbon-<version>-src.zip     source tree for the repository (whitelist below)
#   release/SHA256SUMS.txt
. "$PSScriptRoot/environment.ps1"
$out = "$Workspace/release"
if (Test-Path $out) { Remove-Item -Recurse -Force $out }
New-Item -ItemType Directory -Force "$out/sd/switch/nfscarbon", "$out/src" | Out-Null

if (-not $SourceOnly) {
    if (-not $Nro) { $Nro = "$Workspace/artifacts/nfscarbon.nro" }
    if (-not $Toml) { $Toml = "$Workspace/artifacts/nfscarbon.toml" }
    foreach ($f in @($Nro, $Toml)) { if (-not (Test-Path -LiteralPath $f)) { throw "Missing $f" } }
    Copy-Item -LiteralPath $Nro "$out/sd/switch/nfscarbon/nfscarbon.nro"
    Copy-Item -LiteralPath $Toml "$out/sd/switch/nfscarbon/nfscarbon.toml"
    foreach ($f in @('README.md', 'LICENSE', 'THIRD_PARTY_NOTICES.md', 'CHANGELOG.md')) {
        Copy-Item "$Workspace/$f" "$out/sd/switch/nfscarbon/$f"
    }
    New-Item -ItemType Directory -Force "$out/sd/switch/nfscarbon/game-data" | Out-Null
    Set-Content "$out/sd/switch/nfscarbon/game-data/PUT_YOUR_GAME_FILES_HERE.txt" `
        "Copy default.xex, the NFS folder and the Movies folder of your own Xbox 360 copy of Need for Speed: Carbon here." -Encoding ascii
}

# Source tree: only what is meant to be public.
$include = @('README.md', 'LICENSE', 'THIRD_PARTY_NOTICES.md', 'CHANGELOG.md', 'dependencies.json', '.gitignore', '.gitattributes', '.editorconfig', 'CONTRIBUTING.md', 'docs/README.md',
    'docs/CONFIG.md', 'docs/HOW_IT_WORKS.md', 'docs/PERFORMANCE.md', 'docs/banner.svg',
    'patches', 'scripts', 'tests', 'config', '.github')
# Public versions of two pages that are working notes in the development tree.
New-Item -ItemType Directory -Force "$out/src/docs" | Out-Null
Copy-Item "$Workspace/docs/public/BUILD.md" "$out/src/docs/BUILD.md"
Copy-Item "$Workspace/docs/public/KNOWN_ISSUES.md" "$out/src/docs/KNOWN_ISSUES.md"
foreach ($item in $include) {
    $src = "$Workspace/$item"
    if (-not (Test-Path -LiteralPath $src)) { continue }
    $dst = "$out/src/$item"
    New-Item -ItemType Directory -Force (Split-Path $dst) | Out-Null
    Copy-Item -Recurse -LiteralPath $src -Destination $dst
}
Remove-Item -Force -ErrorAction SilentlyContinue "$out/src/scripts/setup-claude-lane.ps1", "$out/src/scripts/allow-claude-push.ps1", "$out/src/scripts/push-private.ps1", "$out/src/scripts/make-public-branch.ps1"
# The committed .gitignore, without local additions.
git show HEAD:.gitignore | Set-Content "$out/src/.gitignore" -Encoding ascii
Get-ChildItem -Recurse -Directory -Filter __pycache__ "$out/src" | Remove-Item -Recurse -Force

# Refuse to continue if anything proprietary or private slipped in.
$forbidden = '\.(xex|xexp|iso|wmv|bin|dll|exe|elf|nacp|log)$|game-data|HW-Test|USERNOTES|hardware-[0-9a-f]{12}|AGENTS\.md|CLAUDE|continue-prompt'
$bad = Get-ChildItem -Recurse -File $out | Where-Object { $_.FullName.Substring($out.Length) -match $forbidden -and $_.Name -ne 'PUT_YOUR_GAME_FILES_HERE.txt' }
if ($bad) { throw "Forbidden files in the release tree:`n$($bad.FullName -join "`n")" }

if (-not $SourceOnly) { & "$env:SystemRoot/System32/tar.exe" -a -c -f "$out/nfscarbon-$Version.zip" -C "$out/sd" . }  # bsdtar: forward slashes
& "$env:SystemRoot/System32/tar.exe" -a -c -f "$out/nfscarbon-$Version-src.zip" -C "$out/src" .
Get-ChildItem "$out/*.zip" | ForEach-Object { "$((Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLower())  $($_.Name)" } |
    Set-Content "$out/SHA256SUMS.txt" -Encoding ascii
Get-Content "$out/SHA256SUMS.txt"
