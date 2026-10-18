. "$PSScriptRoot/environment.ps1"
$devkit = "$Workspace/toolchains/msys64/opt/devkitpro/devkitA64"
$compiler = "$devkit/bin/aarch64-none-elf-gcc.exe"
$env:PATH = "$Workspace/toolchains/msys64/mingw64/bin;$Workspace/toolchains/msys64/usr/bin;$env:PATH"
$version = & $compiler -dumpversion
if ($LASTEXITCODE) { throw 'Cannot inspect the installed compiler.' }
$header = "$devkit/lib/gcc/aarch64-none-elf/$version/include/limits.h"
$pins = Get-Content "$Workspace/dependencies.json" -Raw | ConvertFrom-Json
$gcc = $pins.devkita64_sources | Where-Object { $_.url -match '/gcc-' }
$archive = "$Workspace/toolchains/downloads/$(([Uri]$gcc.url).Segments[-1])"
Assert-FileHash $archive $gcc.algorithm $gcc.hash
$source = "$Workspace/build/gcc-header-sources"
New-Item -ItemType Directory -Force $source | Out-Null
Invoke-Logged 'gcc-limits-extract' "$Workspace/toolchains/msys64/usr/bin/tar.exe" @('--force-local', '-xf', $archive,
    '-C', $source, "gcc-$version/gcc/limitx.h", "gcc-$version/gcc/glimits.h", "gcc-$version/gcc/limity.h")
$prefix = [IO.File]::ReadAllBytes("$source/gcc-$version/gcc/limitx.h")
$integerLimits = [IO.File]::ReadAllBytes("$source/gcc-$version/gcc/glimits.h")
$suffix = [IO.File]::ReadAllBytes("$source/gcc-$version/gcc/limity.h")
$full = [byte[]]($prefix + $integerLimits + $suffix)
$installed = [Convert]::ToBase64String([IO.File]::ReadAllBytes($header))
if ($installed -eq [Convert]::ToBase64String($integerLimits)) {
    # GCC's first pass predates newlib; restore its standard system-header bridge.
    if (-not (Test-Path "$header.before-newlib")) { Copy-Item -LiteralPath $header -Destination "$header.before-newlib" }
    [IO.File]::WriteAllBytes($header, $full)
} elseif ($installed -ne [Convert]::ToBase64String($full)) {
    throw 'Unrecognized compiler limits.h; preserving local changes.'
}
Invoke-Logged 'gcc-newlib-limits' $compiler @('-D__SWITCH__', '-std=c11', '-Werror', '-c',
    "$Workspace/tests/newlib-limits.c", '-o', "$Workspace/build/newlib-limits.o")
Write-Output 'PASS: GCC limits.h bridges the installed newlib path/name limits.'
