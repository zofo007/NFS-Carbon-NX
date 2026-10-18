. "$PSScriptRoot/../scripts/environment.ps1"
$tools = "$Workspace/toolchains/msys64/opt/devkitpro/devkitA64/bin/aarch64-none-elf"
$env:PATH = "$Workspace/toolchains/msys64/mingw64/bin;$env:PATH"
$build = "$Workspace/build/binutils-smoke"
New-Item -ItemType Directory -Force $build | Out-Null
$object = "$build/binary-smoke.o"
$archive = "$build/binary-smoke.a"
Invoke-Logged 'binutils-smoke-assemble' "$tools-as.exe" @("$Workspace/tests/fixtures/binutils-binary-smoke.s", '-o', $object)
Invoke-Logged 'binutils-smoke-readelf' "$tools-readelf.exe" @('-h', '-S', $object)
if (Select-String -Path "$LogDirectory/binutils-smoke-readelf.err" -Pattern 'Error:','Warning:') {
    throw 'Assembler output has malformed ELF sections.'
}
if (-not (Select-String -Path "$LogDirectory/binutils-smoke-readelf.log" -Pattern 'Machine:.*AArch64')) {
    throw 'Assembler output is not AArch64 ELF.'
}
Invoke-Logged 'binutils-smoke-data' "$tools-objcopy.exe" @('-O', 'binary', '-j', '.rodata', $object, "$build/sentinel.bin")
if (([IO.File]::ReadAllBytes("$build/sentinel.bin") -join ',') -ne '0,10,13,26,255,13,10') {
    throw 'Binary I/O altered CR/LF or EOF sentinel bytes.'
}
Invoke-Logged 'binutils-smoke-archive' "$tools-ar.exe" @('rcs', $archive, $object)
$header = [IO.File]::ReadAllBytes($archive)
if (($header[0..7] -join ',') -ne '33,60,97,114,99,104,62,10') { throw 'Invalid binary archive header.' }
$extract = "$build/extracted"
New-Item -ItemType Directory -Force $extract | Out-Null
# ar p writes text-mode stdout on Windows; x uses the supported binary-file path.
Invoke-Logged 'binutils-smoke-extract' "$tools-ar.exe" @('x', "--output=$extract", $archive, 'binary-smoke.o')
Assert-FileHash "$extract/binary-smoke.o" SHA256 (Get-FileHash -LiteralPath $object -Algorithm SHA256).Hash
Write-Output 'PASS: ARM64 assembly/ELF sections, binary sentinels and archive extraction preserve exact bytes.'
