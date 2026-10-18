param([Parameter(Mandatory=$true)][int]$GameProcessId)
. "$PSScriptRoot/environment.ps1"
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class CarbonWindowCapture {
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr window, IntPtr dc, uint flags);
}
'@
$game = Get-Process -Id $GameProcessId
if ($game.ProcessName -ne 'nfscarbon' -or $game.MainWindowHandle -eq [IntPtr]::Zero) { throw 'Carbon has no capturable window.' }
$rect = New-Object CarbonWindowCapture+Rect
if (-not [CarbonWindowCapture]::GetWindowRect($game.MainWindowHandle, [ref]$rect)) { throw 'Cannot read Carbon window bounds.' }
$bitmap = New-Object Drawing.Bitmap ($rect.Right - $rect.Left), ($rect.Bottom - $rect.Top)
$graphics = [Drawing.Graphics]::FromImage($bitmap)
$dc = $graphics.GetHdc()
try {
    if (-not [CarbonWindowCapture]::PrintWindow($game.MainWindowHandle, $dc, 2)) { throw 'Window capture failed.' }
} finally { $graphics.ReleaseHdc($dc); $graphics.Dispose() }
try { $bitmap.Save("$Workspace/logs/host-window.png", [Drawing.Imaging.ImageFormat]::Png) }
finally { $bitmap.Dispose() }
Write-Output 'Captured only the Carbon window to logs/host-window.png.'
