param([Parameter(Mandatory)][string]$Logs, [string]$Out = "nfscarbon-report")
# Packs a copy of the logs folder from your SD card and the summary numbers into <Out>.zip for a bug report.
# usage: powershell -File scripts/pack-logs.ps1 -Logs E:\switch\nfscarbon\logs
$work = Join-Path $env:TEMP "nfscarbon-report"
if (Test-Path $work) { Remove-Item -Recurse -Force $work }
New-Item -ItemType Directory $work | Out-Null
Copy-Item -Recurse $Logs "$work/logs"
& python "$PSScriptRoot/summarize_run.py" $Logs | Set-Content "$work/summary.txt" -Encoding utf8
& "$env:SystemRoot/System32/tar.exe" -a -c -f "$Out.zip" -C $work .
Write-Output "Wrote $Out.zip. Paste summary.txt into the report and attach the zip."
Get-Content "$work/summary.txt"
