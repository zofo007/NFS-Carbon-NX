param([switch]$SourcesOnly, [ValidateRange(1,8)][int]$Jobs = 2)
. "$PSScriptRoot/environment.ps1"
$env:PATH = "$Workspace/toolchains/msys64/usr/bin;$env:PATH"
$pins = Get-Content "$Workspace/dependencies.json" -Raw | ConvertFrom-Json
$devkit = "$Workspace/toolchains/msys64/opt/devkitpro".Replace('\','/')
$sourceDir = "$Workspace/build/portlib-sources"
New-Item -ItemType Directory -Force $sourceDir | Out-Null
foreach ($library in $pins.switch_portlibs) {
    $file = "$Workspace/toolchains/downloads/$(([Uri]$library.url).Segments[-1])"
    if (-not (Test-Path -LiteralPath $file)) {
        Invoke-Logged "download-$($library.name)" (Get-Command curl.exe).Source @('--fail', '--location', '--retry', '2',
            '--max-time', '600', $library.url, '--output', "$file.part")
        Assert-FileHash "$file.part" 'SHA256' $library.sha256
        Move-Item -LiteralPath "$file.part" -Destination $file
    }
    Assert-FileHash $file 'SHA256' $library.sha256
    $source = "$sourceDir/$($library.name)-$($library.version)"
    if (-not (Test-Path "$source/.carbon-extracted")) {
        $extract = @('--force-local', '--skip-old-files', '-xf', $file, '-C', $sourceDir)
        if ($library.name -eq 'zstd') {
            # CLI-only test symlinks target an unbuilt program and cannot be emulated by MSYS2.
            $extract += "--exclude=zstd-$($library.version)/tests/cli-tests/bin"
        }
        Invoke-Logged "extract-$($library.name)" "$Workspace/toolchains/msys64/usr/bin/tar.exe" $extract
        New-Item -ItemType File "$source/.carbon-extracted" | Out-Null
    }
    if (-not (Test-Path "$source/CMakeLists.txt") -and -not (Test-Path "$source/build/cmake/CMakeLists.txt")) {
        throw "Incomplete $($library.name) source extraction; preserve/inspect $source before retrying."
    }
}
if ($SourcesOnly) { return }
if (-not (Test-Path "$devkit/libnx/lib/libnx.a")) { throw 'Complete the compiler and build-libnx.ps1 first.' }
$env:DEVKITPRO = $devkit
$env:PATH = "$Workspace/toolchains/msys64/mingw64/bin;$env:PATH"
$options = @{
    expat = @('-DEXPAT_SHARED_LIBS=OFF', '-DEXPAT_BUILD_DOCS=OFF', '-DEXPAT_BUILD_EXAMPLES=OFF', '-DEXPAT_BUILD_TESTS=OFF', '-DEXPAT_BUILD_TOOLS=OFF',
        '-DEXPAT_WITH_ARC4RANDOM_BUF=ON')
    zlib = @('-DZLIB_BUILD_SHARED=OFF', '-DZLIB_BUILD_STATIC=ON', '-DZLIB_BUILD_TESTING=OFF')
    zstd = @('-DZSTD_BUILD_PROGRAMS=OFF', '-DZSTD_BUILD_STATIC=ON', '-DZSTD_BUILD_SHARED=OFF')
}
foreach ($library in $pins.switch_portlibs) {
    $source = "$sourceDir/$($library.name)-$($library.version)"
    $build = "$Workspace/build/portlibs/$($library.name)"
    if ($library.name -eq 'zstd') { $source += '/build/cmake' }
    Invoke-Logged "portlib-$($library.name)-configure" $CMake (@('-S', $source, '-B', $build, '-G', 'Ninja',
        '-DCMAKE_BUILD_TYPE=Release', "-DCMAKE_MAKE_PROGRAM=$Ninja", "-DCMAKE_INSTALL_PREFIX=$devkit/portlibs/switch",
        "-DDEVKITPRO=$devkit", "-DCMAKE_TOOLCHAIN_FILE=$Workspace/reference-nfsmw-nx/tools/switch/cmake/switch-devkitA64.cmake") + $options.($library.name))
    Invoke-Logged "portlib-$($library.name)-build" $CMake @('--build', $build, '-j', "$Jobs")
    Invoke-Logged "portlib-$($library.name)-install" $CMake @('--install', $build)
}
$archives = @("$devkit/portlibs/switch/lib/libexpat.a", "$devkit/portlibs/switch/lib/libz.a", "$devkit/portlibs/switch/lib/libzstd.a")
foreach ($file in $archives) { if (-not (Test-Path -LiteralPath $file)) { throw "Missing static portlib: $file" } }
Invoke-Logged 'portlibs-architecture' "$devkit/devkitA64/bin/aarch64-none-elf-objdump.exe" (@('-f') + $archives)
$formats = @(Get-Content "$LogDirectory/portlibs-architecture.log" | Select-String 'file format ')
if (-not $formats.Count -or @($formats | Where-Object { $_ -notmatch 'file format elf64-littleaarch64$' }).Count) {
    throw 'Portlib archive members are not all AArch64 ELF.'
}
Invoke-Logged 'portlibs-smoke-configure' $CMake @('-S', "$Workspace/tests/portlib-smoke", '-B', "$Workspace/build/portlib-smoke",
    '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', "-DCMAKE_MAKE_PROGRAM=$Ninja", "-DDEVKITPRO=$devkit",
    "-DCMAKE_TOOLCHAIN_FILE=$Workspace/reference-nfsmw-nx/tools/switch/cmake/switch-devkitA64.cmake")
Invoke-Logged 'portlibs-smoke-build' $CMake @('--build', "$Workspace/build/portlib-smoke", '-j', "$Jobs")
Write-Output 'PASS: target archives and parser/compression/entropy link test; hardware execution not performed.'
