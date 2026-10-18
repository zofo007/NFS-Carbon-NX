. "$PSScriptRoot/../scripts/environment.ps1"
Initialize-HostCompiler
$testDir = "$Workspace/build/tests/shader-predicates"
New-Item -ItemType Directory -Force "$testDir/baseline" | Out-Null
& git -c "safe.directory=$($Workspace.Replace('\','/'))/carbon-source" -C "$Workspace/carbon-source" archive `
    "--output=$testDir/baseline.tar" 2664ecb -- renderer/src/shader_translator.cpp
if ($LASTEXITCODE) { throw 'Cannot preserve the pre-change shader translator.' }
& "$env:SystemRoot/System32/tar.exe" -xf "$testDir/baseline.tar" -C "$testDir/baseline" --strip-components 2
if ($LASTEXITCODE) { throw 'Cannot extract the shader baseline.' }
$source = "$Workspace/carbon-source/renderer/src"
$sdk = "$Workspace/toolchains/rexglue-built"
$compiler = (Get-Command clang++.exe).Source
$flags = @('-std=c++23', '-O2', '-D_DLL', '-D_MT', '-Xclang', '--dependent-lib=msvcrt',
    '-nostartfiles', '-nostdlib', '-fuse-ld=lld-link', '-lkernel32', '-loldnames',
    '-DNOMINMAX', '-DWIN32_LEAN_AND_MEAN', '-DSPDLOG_FMT_EXTERNAL', '-DSPDLOG_COMPILED_LIB',
    "-I$source", "-I$Workspace/carbon-source/renderer/third_party",
    '-isystem', "$sdk/include", "-I$Workspace/reference-nfsmw-nx/sdk/thirdparty/glslang",
    "$Workspace/tests/shader_predicates_test.cpp", "$source/shader_compiler.cpp")
$libs = @('fmt', 'SPIRV', 'glslang', 'MachineIndependent', 'GenericCodeGen', 'OSDependent', 'OGLCompiler') |
    ForEach-Object { "$sdk/lib/$_.lib" }
foreach ($mode in @('baseline', 'candidate')) {
    $translator = if ($mode -eq 'baseline') { "$testDir/baseline/shader_translator.cpp" } else { "$source/shader_translator.cpp" }
    Invoke-Logged "shader-$mode-compile" $compiler ($flags + @($translator) + $libs + @('-o', "$testDir/$mode.exe"))
    Invoke-Logged "shader-$mode-test" "$testDir/$mode.exe" @()
}
& python "$Workspace/scripts/compare_shader_predicates.py" "$LogDirectory/shader-baseline-test.log" "$LogDirectory/shader-candidate-test.log"
if ($LASTEXITCODE) { throw 'Shader predicate differential check failed.' }
