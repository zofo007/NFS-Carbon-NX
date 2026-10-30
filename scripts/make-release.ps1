param(
    [ValidateSet('standard', 'collectors')][string]$Edition = 'standard',
    [string]$Version = (Select-String -Path "$PSScriptRoot/../CHANGELOG.md" -Pattern $(if ($Edition -eq 'collectors') { '^## (\d\S*-collectors\S*)' } else { '^## (\d\S*)' }) | Select-Object -First 1).Matches[0].Groups[1].Value,
    [string]$Nro = '',
    [string]$Toml = '',
    [switch]$SourceOnly   # skip the NRO zip: only build the source zip (used by make-public-branch.ps1)
)
# Builds release/v<Version>/ from tracked files and a verified NRO. Nothing is uploaded or published; other release folders are never touched.
#   release/v<Version>/nfscarbon-<version>.zip                     Standard: the SD card content (switch/nfscarbon/...)
#   release/v<Version>/nfscarbon-collectors-<version>.zip          Collector's: the SD card content (switch/nfscarbon-collectors/...)
#   release/v<Version>/nfscarbon-<version>-src.zip                 Standard only: source tree for the repository (whitelist below)
#   release/v<Version>/SHA256SUMS.txt
# Standard NRO default: artifacts/nfscarbon.nro. Collector's: artifacts/collectors-rl/nfscarbon-collectors-rl.nro (installed as nfscarbon-collectors.nro).
. "$PSScriptRoot/environment.ps1"
$ce = $Edition -eq 'collectors'
if ($ce -and $SourceOnly) { throw 'The source zip belongs to the standard edition release.' }
$folder = if ($ce) { 'nfscarbon-collectors' } else { 'nfscarbon' }
$nroName = if ($ce) { 'nfscarbon-collectors.nro' } else { 'nfscarbon.nro' }
$zipVersion = $Version -replace '-collectors', ''     # 1.1.0-collectors-beta.1 -> 1.1.0-beta.1
$zipName = if ($ce) { "nfscarbon-collectors-$zipVersion.zip" } else { "nfscarbon-$Version.zip" }
$releaseRoot = [IO.Path]::GetFullPath("$Workspace/release")
$out = [IO.Path]::GetFullPath("$releaseRoot/v$Version")
if (-not $out.StartsWith("$releaseRoot\v", [StringComparison]::OrdinalIgnoreCase)) { throw 'Unexpected release output path.' }
if (Test-Path -LiteralPath $out) {
    if ((Get-Item -LiteralPath $out).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Refusing linked release output.' }
    Remove-Item -LiteralPath $out -Recurse -Force
}
$stage = [IO.Path]::GetFullPath("$out/stage")
New-Item -ItemType Directory -Force "$stage/sd/switch/$folder", "$stage/src" | Out-Null

if (-not $SourceOnly) {
    if (-not $Nro) { $Nro = if ($ce) { "$Workspace/artifacts/collectors-rl/nfscarbon-collectors-rl.nro" } else { "$Workspace/artifacts/nfscarbon.nro" } }
    if (-not $Toml) { $Toml = if ($ce) { "$Workspace/artifacts/collectors-rl/nfscarbon.toml" } else { "$Workspace/artifacts/nfscarbon.toml" } }
    foreach ($f in @($Nro, $Toml)) { if (-not (Test-Path -LiteralPath $f)) { throw "Missing $f" } }
    Copy-Item -LiteralPath $Nro "$stage/sd/switch/$folder/$nroName"
    Copy-Item -LiteralPath $Toml "$stage/sd/switch/$folder/nfscarbon.toml"
    Copy-Item "$Workspace/config/$(if ($ce) { 'nfscarbon.collectors.report.toml' } else { 'nfscarbon.report.toml' })" "$stage/sd/switch/$folder/nfscarbon.report.toml"
    foreach ($f in @('README.md', 'LICENSE', 'THIRD_PARTY_NOTICES.md', 'CHANGELOG.md')) {
        Copy-Item "$Workspace/$f" "$stage/sd/switch/$folder/$f"
    }
    New-Item -ItemType Directory -Force "$stage/sd/switch/$folder/game-data", "$stage/sd/switch/$folder/dlc" | Out-Null
    $gameName = if ($ce) { "Need for Speed: Carbon - Collector's Edition (USA)" } else { 'Need for Speed: Carbon' }
    Set-Content "$stage/sd/switch/$folder/game-data/PUT_YOUR_GAME_FILES_HERE.txt" `
        "Copy default.xex, the NFS folder and the Movies folder of your own Xbox 360 copy of $gameName here." -Encoding ascii
    Set-Content "$stage/sd/switch/$folder/dlc/PUT_DLC_PACKAGES_HERE.txt" `
        'Optional. Copy your Xbox 360 DLC packages (the STFS files, from your own copy of the game) here, in any sub-folder. 30 packs at most: more than that crashed the profile load. They are installed at the next start; the log shows [dlc] N package(s) installed.' -Encoding ascii
}

if (-not $ce) {
# Source tree: only what is meant to be public.
$include = @('README.md', 'LICENSE', 'THIRD_PARTY_NOTICES.md', 'CHANGELOG.md', 'dependencies.json', '.gitignore', '.gitattributes', '.editorconfig', 'CONTRIBUTING.md', 'docs/README.md',
    'docs/CONFIG.md', 'docs/HOW_IT_WORKS.md', 'docs/PERFORMANCE.md', 'docs/banner.svg',
    'patches', 'scripts', 'tests', 'config', '.github')
# Public build and known-issues pages are already in the repository.
New-Item -ItemType Directory -Force "$stage/src/docs" | Out-Null
Copy-Item "$Workspace/docs/BUILD.md" "$stage/src/docs/BUILD.md"
Copy-Item "$Workspace/docs/KNOWN_ISSUES.md" "$stage/src/docs/KNOWN_ISSUES.md"
foreach ($item in $include) {
    $src = "$Workspace/$item"
    if (-not (Test-Path -LiteralPath $src)) { continue }
    $dst = "$stage/src/$item"
    New-Item -ItemType Directory -Force (Split-Path $dst) | Out-Null
    Copy-Item -Recurse -LiteralPath $src -Destination $dst
}
# Stamp the banner with this release's version (the dev copy keeps whatever it had).
$svg = "$stage/src/docs/banner.svg"
if (Test-Path $svg) { $t = (Get-Content $svg -Raw -Encoding utf8) -replace '(fill="#ff8a5c">)[^<]*<', "`${1}v$Version<" ; [IO.File]::WriteAllText($svg, $t, (New-Object Text.UTF8Encoding($false))) }
Remove-Item -Force -ErrorAction SilentlyContinue "$stage/src/scripts/setup-claude-lane.ps1", "$stage/src/scripts/allow-claude-push.ps1", "$stage/src/scripts/push-private.ps1", "$stage/src/scripts/make-public-branch.ps1"
# The committed .gitignore, without local additions.
git show HEAD:.gitignore | Set-Content "$stage/src/.gitignore" -Encoding ascii
Get-ChildItem -Recurse -Directory -Filter __pycache__ "$stage/src" | ForEach-Object {
    $cache = [IO.Path]::GetFullPath($_.FullName)
    if (-not $cache.StartsWith("$stage\src\", [StringComparison]::OrdinalIgnoreCase)) { throw 'Unexpected cache path.' }
    Remove-Item -LiteralPath $cache -Recurse -Force
}
}

# Refuse to continue if anything proprietary or private slipped in.
$forbidden = '\.(xex|xexp|iso|wmv|bin|dll|exe|elf|nacp|log)$|game-data|HW-Test|USERNOTES|hardware-[0-9a-f]{12}|AGENTS\.md|CLAUDE|continue-prompt'
$bad = Get-ChildItem -Recurse -File $stage | Where-Object { $_.FullName.Substring($stage.Length) -match $forbidden -and $_.Name -ne 'PUT_YOUR_GAME_FILES_HERE.txt' }
if ($bad) { throw "Forbidden files in the release tree:`n$($bad.FullName -join "`n")" }

if (-not $SourceOnly) { & "$env:SystemRoot/System32/tar.exe" -a -c -f "$out/$zipName" -C "$stage/sd" switch }  # bsdtar: forward slashes
if (-not $ce) { & "$env:SystemRoot/System32/tar.exe" -a -c -f "$out/nfscarbon-$Version-src.zip" -C "$stage/src" @(Get-ChildItem -Force "$stage/src" | ForEach-Object Name) }
Get-ChildItem "$out/*.zip" | ForEach-Object { "$((Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLower())  $($_.Name)" } |
    Set-Content "$out/SHA256SUMS.txt" -Encoding ascii
$stageFull = [IO.Path]::GetFullPath($stage)
if (-not $stageFull.StartsWith("$out\", [StringComparison]::OrdinalIgnoreCase)) { throw 'Unexpected stage path.' }
Remove-Item -LiteralPath $stageFull -Recurse -Force
Get-Content "$out/SHA256SUMS.txt"