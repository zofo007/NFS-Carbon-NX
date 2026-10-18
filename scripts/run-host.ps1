param([ValidateSet('carbon', 'xenos')][string]$Renderer = 'xenos')
. "$PSScriptRoot/environment.ps1"
$exe = "$Workspace/build/carbon-host/nfscarbon.exe"
if (-not (Test-Path -LiteralPath $exe)) { throw 'Run scripts/build-host.ps1 first.' }
$arguments = @("--game_data_root=$Workspace/game-data", "--user_data_root=$Workspace/build/host-user",
    "--cache_root=$Workspace/build/host-cache", "--gpu_plugin=$Renderer", '--mnk_mode=true',
    '--mnk_mouse=false', '--keybind_right_trigger=W,Control', '--keybind_left_trigger=S,Shift')
$quoted = $arguments | ForEach-Object { '"' + $_ + '"' }
# This is an interactive test the user asked to play, so keep its window visible.
Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe) -WindowStyle Normal -PassThru `
    -ArgumentList $quoted | Select-Object Id,ProcessName
