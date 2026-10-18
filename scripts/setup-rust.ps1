. "$PSScriptRoot/environment.ps1"
$pins = Get-Content "$Workspace/dependencies.json" -Raw | ConvertFrom-Json
$env:CARGO_HOME = "$Workspace/toolchains/rust/cargo"
$env:RUSTUP_HOME = "$Workspace/toolchains/rust/rustup"
$env:RUSTUP_TOOLCHAIN = $pins.rust_toolchain
$env:PATH = "$env:CARGO_HOME/bin;$Workspace/toolchains/msys64/mingw64/bin;$env:PATH"
$rustup = "$env:CARGO_HOME/bin/rustup.exe"
if (-not (Test-Path -LiteralPath $rustup)) {
    $installer = "$Workspace/toolchains/downloads/rustup-init-$($pins.rustup_installer.version).exe"
    if (-not (Test-Path -LiteralPath $installer)) {
        Invoke-Logged 'download-rustup' (Get-Command curl.exe).Source @('--fail', '--location', '--retry', '2',
            '--max-time', '600', $pins.rustup_installer.url, '--output', $installer)
    }
    Assert-FileHash $installer 'SHA256' $pins.rustup_installer.sha256
    Invoke-Logged 'rustup-install' $installer @('-y', '--no-modify-path', '--profile', 'minimal',
        '--default-host', 'x86_64-pc-windows-gnu', '--default-toolchain', $pins.rust_toolchain,
        '--target', 'aarch64-unknown-linux-gnu')
}
Invoke-Logged 'rust-toolchain' $rustup @('toolchain', 'install', $pins.rust_toolchain, '--profile', 'minimal',
    '--target', 'aarch64-unknown-linux-gnu', '--component', 'rustfmt')
Invoke-Logged 'rust-version' "$env:CARGO_HOME/bin/rustc.exe" @('--version', '--verbose')
Invoke-Logged 'rust-arm64-smoke' "$env:CARGO_HOME/bin/rustc.exe" @('--target', 'aarch64-unknown-linux-gnu',
    '--crate-type', 'lib', '--emit=obj', "$Workspace/tests/rust-arm64-smoke.rs", '-o', "$Workspace/build/rust-arm64-smoke.o")
$header = [IO.File]::ReadAllBytes("$Workspace/build/rust-arm64-smoke.o")
if ($header.Length -lt 64 -or $header[0] -ne 127 -or $header[1] -ne 69 -or $header[2] -ne 76 -or
    $header[3] -ne 70 -or $header[4] -ne 2 -or $header[5] -ne 1 -or $header[16] -ne 1 -or
    $header[17] -ne 0 -or $header[18] -ne 183 -or $header[19] -ne 0) {
    throw 'Rust target smoke test did not produce an ELF64 little-endian AArch64 object.'
}
Write-Output 'PASS: pinned GNU Rust compiler emitted an AArch64 ELF object (not Windows COFF).'
