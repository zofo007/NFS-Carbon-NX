param([Parameter(Mandatory)][string]$GameData, [Parameter(Mandatory)][string]$OutDir)
# Prints the path of the 256x256 JPEG elf2nro packs as the hbmenu icon, or warns and prints
# nothing so the NRO keeps the devkitPro default. Sharpest source first: the Carbon logo in the
# 1280x720 attract movie, then the XEX's XDBF title image (only 64x64, soft when scaled up).
. "$PSScriptRoot/environment.ps1"
Add-Type -AssemblyName System.Drawing, System.Runtime.WindowsRuntime
$jpg = Join-Path $OutDir 'icon.jpg'

function Save-Icon([Drawing.Image]$Source, [Drawing.Rectangle]$From, [Drawing.Rectangle]$To) {
    $icon = New-Object Drawing.Bitmap 256, 256
    $graphics = [Drawing.Graphics]::FromImage($icon)
    try {
        $graphics.Clear([Drawing.Color]::Black)
        $graphics.InterpolationMode = 'HighQualityBicubic'
        $graphics.PixelOffsetMode = 'HighQuality'
        $graphics.DrawImage($Source, $To, $From, [Drawing.GraphicsUnit]::Pixel)
        $icon.Save($jpg, [Drawing.Imaging.ImageFormat]::Jpeg)
    } finally { $graphics.Dispose(); $icon.Dispose(); $Source.Dispose() }
    $jpg.Replace('\', '/')
}

# One video frame through Windows Media Foundation (WinRT; Windows PowerShell 5.1 only).
function Get-MovieFrame([string]$Movie, [TimeSpan]$At) {
    $asTask = [WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {
        $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and
        $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1' } | Select-Object -First 1
    function Await($operation, [Type]$type) {
        $task = $asTask.MakeGenericMethod($type).Invoke($null, @($operation))
        [void]$task.Wait(-1)
        $task.Result
    }
    [void][Windows.Storage.StorageFile, Windows.Storage, ContentType = WindowsRuntime]
    [void][Windows.Media.Editing.MediaComposition, Windows.Media.Editing, ContentType = WindowsRuntime]
    [void][Windows.Graphics.Imaging.ImageStream, Windows.Graphics.Imaging, ContentType = WindowsRuntime]
    $file = Await ([Windows.Storage.StorageFile]::GetFileFromPathAsync((Resolve-Path -LiteralPath $Movie).Path)) ([Windows.Storage.StorageFile])
    $clip = Await ([Windows.Media.Editing.MediaClip]::CreateFromFileAsync($file)) ([Windows.Media.Editing.MediaClip])
    $composition = New-Object Windows.Media.Editing.MediaComposition
    # PowerShell 5.1 does not expose the projected IList.Add on WinRT collections.
    [void][Collections.Generic.ICollection[Windows.Media.Editing.MediaClip]].GetMethod('Add').Invoke($composition.Clips, @($clip))
    $frame = Await ($composition.GetThumbnailAsync($At, 0, 0, [Windows.Media.Editing.VideoFramePrecision]::NearestFrame)) ([Windows.Graphics.Imaging.ImageStream])
    $bytes = New-Object IO.MemoryStream
    [IO.WindowsRuntimeStreamExtensions]::AsStreamForRead($frame).CopyTo($bytes)
    [Drawing.Image]::FromStream($bytes)
}

New-Item -ItemType Directory -Force $OutDir | Out-Null
Remove-Item -LiteralPath $jpg -ErrorAction SilentlyContinue
try {
    # 72 s: the logo is fully lit and has no lens flare (70 s has one).
    $frame = Get-MovieFrame (Join-Path $GameData 'Movies/attract_english_ntsc.wmv') ([TimeSpan]::FromSeconds(72))
    if ($frame.Width -ne 1280 -or $frame.Height -ne 720) { $frame.Dispose(); throw 'unexpected movie size.' }
    # The whole logo spans x 100-1180; letterbox that 1080x720 band on black.
    return Save-Icon $frame (New-Object Drawing.Rectangle 100, 0, 1080, 720) (New-Object Drawing.Rectangle 0, 42, 256, 171)
} catch { Write-Warning "Attract-movie logo unavailable: $_ Trying the XEX title image." }
$xex = Join-Path $GameData 'default.xex'
$png = Join-Path $OutDir 'icons/title.png'
try {
    Remove-Item -LiteralPath $png -ErrorAction SilentlyContinue
    # The host tool decrypts/decompresses the XEX and logs to stderr; the dummy init options
    # satisfy its CLI parser. Its stdout must not leak into this script's printed path.
    & $HostTool init --project-name nfscarbon --xex-path $xex achievements $xex $OutDir | Out-Null
    if ($LASTEXITCODE -or -not (Test-Path -LiteralPath $png)) { throw 'no title image extracted (see the host tool output above).' }
    $title = [Drawing.Image]::FromFile($png)
    Save-Icon $title (New-Object Drawing.Rectangle 0, 0, $title.Width, $title.Height) (New-Object Drawing.Rectangle 0, 0, 256, 256)
} catch {
    Write-Warning "XEX title icon unavailable ($xex): $_ Using the default NRO icon."
}
