. "$PSScriptRoot/../scripts/environment.ps1"
$build = "$Workspace/build/switch"
if (-not (Test-Path "$build/build.ninja")) { throw 'Configure the Carbon Switch build first.' }
$env:PATH = "$Workspace/toolchains/msys64/mingw64/bin;$env:PATH"
$targets = @(
    'renderer/CMakeFiles/rexgpu-carbon.dir/src/shader_translator.cpp.obj',
    'renderer/CMakeFiles/rexgpu-carbon.dir/src/texture_decode.cpp.obj',
    'renderer/CMakeFiles/rexgpu-carbon.dir/src/renderer.cpp.obj',
    'renderer/CMakeFiles/rexgpu-carbon.dir/src/command_processor.cpp.obj',
    'CMakeFiles/nfscarbon.dir/src/ring_wait_hook.cpp.obj',
    'CMakeFiles/nfscarbon.dir/src/audio_resampler_hook.cpp.obj',
    'rexglue-sdk/src/core/CMakeFiles/rexcore.dir/clock_posix.cpp.obj',
    'rexglue-sdk/src/core/CMakeFiles/rexcore.dir/threading_switch.cpp.obj',
    'rexglue-sdk/src/core/CMakeFiles/rexcore.dir/fiber_switch.cpp.obj',
    'rexglue-sdk/src/core/CMakeFiles/rexcore.dir/guest_memory_switch.cpp.obj',
    'rexglue-sdk/src/core/CMakeFiles/rexcore.dir/memory_switch.cpp.obj',
    'rexglue-sdk/src/core/CMakeFiles/rexcore.dir/filesystem_posix.cpp.obj',
    'rexglue-sdk/src/ui/CMakeFiles/rexui.dir/window_switch.cpp.obj',
    'rexglue-sdk/src/input/CMakeFiles/rexinput.dir/switch/switch_input_driver.cpp.obj',
    'rexglue-sdk/src/audio/CMakeFiles/rexaudio.dir/audio_system.cpp.obj',
    'rexglue-sdk/src/audio/CMakeFiles/rexaudio.dir/xma_context.cpp.obj',
    'rexglue-sdk/src/audio/CMakeFiles/rexaudio.dir/xma_decoder.cpp.obj',
    'rexglue-sdk/CMakeFiles/rexglue_switch_startup.dir/src/ui/switch_perf.cpp.obj',
    'rexglue-sdk/src/audio/CMakeFiles/rexaudio.dir/switch/switch_audio_system.cpp.obj')
foreach ($target in $targets) {
    $file = [IO.Path]::GetFileNameWithoutExtension([IO.Path]::GetFileNameWithoutExtension($target))
    $commands = @(& $Ninja -C $build -t commands $target)
    if ($LASTEXITCODE -or -not $commands.Count) { throw "Cannot inspect the $file compile recipe." }
    $recipe = [regex]::Match($commands[-1], '^(?<program>"[^"]+"|\S+)\s+(?<arguments>.*)$')
    $program = $recipe.Groups['program'].Value.Trim('"')
    if (-not $recipe.Success -or (Split-Path $program -Leaf) -ne 'aarch64-none-elf-g++.exe') {
        throw 'Expected a devkitA64 C++ compile recipe, not a shell command.'
    }
    $arguments = $recipe.Groups['arguments'].Value
    $arguments = $arguments -replace ' -MD -MT ("[^"]+"|\S+) -MF ("[^"]+"|\S+)', ''
    $arguments = $arguments -replace ' -o ("[^"]+"|\S+)', ''
    if ($arguments -match '(^| )-(o|MF|MD|MT)( |$)' -or $arguments -notmatch ' -c ') {
        throw 'Refusing a syntax check that could overwrite build outputs.'
    }
    # Keep Ninja's actual target flags; syntax-only produces no object/dependency file.
    $log = "$LogDirectory/switch-syntax-$file"
    $process = Start-Process -FilePath $program -ArgumentList "$arguments -fsyntax-only" -WorkingDirectory $build `
        -WindowStyle Hidden -Wait -PassThru -RedirectStandardOutput "$log.log" -RedirectStandardError "$log.err"
    if ($null -eq $process.ExitCode) { throw "Cannot verify compiler exit status: $file" }
    if ($process.ExitCode) {
        Get-Content "$log.err" -Tail 30
        throw "Switch syntax check failed ($($process.ExitCode)): $file"
    }
    Write-Output "PASS: ARM64 syntax check $file (no build output modified)."
}
