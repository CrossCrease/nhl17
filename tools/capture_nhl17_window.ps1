param(
    [int]$GameProcessId = 0,
    [string]$Output = (Join-Path $PSScriptRoot '..\artifacts\nhl17-window.png')
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class Nhl17Capture {
    [StructLayout(LayoutKind.Sequential)]
    public struct Rect { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll")]
    public static extern bool PrintWindow(IntPtr window, IntPtr dc, uint flags);
    [DllImport("user32.dll")]
    public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
}
'@
$game = if ($GameProcessId) { Get-Process -Id $GameProcessId } else { Get-Process -Name shadPS4 }
if (@($game).Count -ne 1 -or $game.ProcessName -ne 'shadPS4') { throw 'Specify one shadPS4 process.' }
$window = $game.MainWindowHandle
if ($window -eq [IntPtr]::Zero) { throw 'The game has no window.' }
[void][Nhl17Capture]::SetThreadDpiAwarenessContext([IntPtr](-4))
$rect = [Nhl17Capture+Rect]::new()
if (-not [Nhl17Capture]::GetWindowRect($window, [ref]$rect)) { throw 'Cannot read game window bounds.' }
$bitmap = [Drawing.Bitmap]::new($rect.Right - $rect.Left, $rect.Bottom - $rect.Top)
$graphics = [Drawing.Graphics]::FromImage($bitmap)
$dc = $graphics.GetHdc()
try {
    if (-not [Nhl17Capture]::PrintWindow($window, $dc, 2)) { throw 'Cannot capture game window.' }
} finally {
    $graphics.ReleaseHdc($dc)
    $graphics.Dispose()
}
try {
    $bitmap.Save([IO.Path]::GetFullPath($Output), [Drawing.Imaging.ImageFormat]::Png)
} finally {
    $bitmap.Dispose()
}
[IO.Path]::GetFullPath($Output)
