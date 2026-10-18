. "$PSScriptRoot/environment.ps1"
& python "$PSScriptRoot/validate_build.py" "$Workspace/artifacts/nfscarbon.nro" "$Workspace/build/switch/nfscarbon"
if ($LASTEXITCODE) { throw 'NRO/ELF validation failed.' }
