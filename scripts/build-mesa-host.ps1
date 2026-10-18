param([ValidateRange(1,8)][int]$Jobs = 2)
. "$PSScriptRoot/environment.ps1"
$mesa = "$Workspace/toolchains/mesa-switch".Replace('\','/')
$msys = "$Workspace/toolchains/msys64".Replace('\','/')
$meson = "$msys/mingw64/bin/meson.exe"
if (-not (Test-Path -LiteralPath $meson)) { throw 'Run setup-graphics-tools.ps1 first.' }
$configDir = "$Workspace/build/mesa-config"
New-Item -ItemType Directory -Force $configDir | Out-Null
foreach ($name in @('switch_cross_file_msys2.txt', 'switch_native_tools_msys2.txt', 'windows_x64_host_cross_file_msys2.txt')) {
    $content = (Get-Content "$mesa/$name" -Raw).Replace('C:/msys64', $msys).Replace('@DIRNAME@', $mesa)
    $content = $content.Replace('/clangarm64/bin/cbindgen.exe', '/clang64/bin/cbindgen.exe')
    if ($name -eq 'switch_native_tools_msys2.txt') {
        $content = $content.Replace('[binaries]', "[binaries]`nc = '$msys/mingw64/bin/gcc.exe'`ncpp = '$msys/mingw64/bin/g++.exe'")
    }
    [IO.File]::WriteAllText("$configDir/$name", $content, (New-Object Text.UTF8Encoding $false))
}
$env:PATH = "$msys/clang64/bin;$msys/mingw64/bin;$env:PATH"
$env:NINJA = $Ninja
$build = "$mesa/builddir-native-x64-host"
$arguments = @('setup', $build, $mesa, '--cross-file', "$configDir/windows_x64_host_cross_file_msys2.txt",
    '--buildtype=release', '-Dvulkan-drivers=', '-Dgallium-drivers=', '-Dshader-cache=disabled',
    '-Dplatforms=', '-Dglx=disabled', '-Degl=disabled', '-Dopengl=false', '-Dgles1=disabled',
    '-Dgles2=disabled', '-Dtools=[]', '-Dllvm=enabled', '-Dshared-llvm=enabled',
    '-Dmesa-clc=enabled', '-Dprecomp-compiler=enabled', '-Dinstall-mesa-clc=true')
if (Test-Path "$build/meson-private/coredata.dat") { $arguments += '--reconfigure' }
Invoke-Logged 'mesa-host-configure' $meson $arguments
Invoke-Logged 'mesa-host-build' $Ninja @('-C', $build, '-j', "$Jobs", 'src/compiler/clc/mesa_clc.exe',
    'src/compiler/spirv/vtn_bindgen2.exe')
Invoke-Logged 'mesa-clc-smoke' "$build/src/compiler/clc/mesa_clc.exe" @('-o', "$Workspace/build/mesa-smoke.spv",
    "$Workspace/tests/mesa-smoke.cl", '--', '-cl-std=CL2.0')
Invoke-Logged 'mesa-spirv-smoke' "$msys/clang64/bin/spirv-val.exe" @("$Workspace/build/mesa-smoke.spv")
