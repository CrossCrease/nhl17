param(
    [int]$GameProcessId = 0,
    [string[]]$Keys = @(),
    [switch]$Screenshot,
    [switch]$ScreenshotWithOverlays,
    [switch]$Close,
    [switch]$Focus,
    [switch]$NativeInput,
    [ValidateRange(30, 2000)][int]$HoldMs = 150
)
$ErrorActionPreference = 'Stop'
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class Nhl17Window {
    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool PostMessage(IntPtr hwnd, uint msg, UIntPtr key, IntPtr data);
    [DllImport("user32.dll")]
    public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr hwnd);
    [DllImport("user32.dll")]
    public static extern bool ShowWindow(IntPtr hwnd, int command);
    [DllImport("user32.dll")]
    public static extern uint MapVirtualKey(uint code, uint mapType);
    [DllImport("user32.dll")]
    public static extern void keybd_event(byte key, byte scan, uint flags, UIntPtr extra);
}
'@
$games = if ($GameProcessId) { @(Get-Process -Id $GameProcessId) } else { @(Get-Process -Name shadPS4) }
if ($games.Count -ne 1) { throw 'Specify the game process ID when multiple instances are running.' }
$game = $games[0]
if ($game.ProcessName -notin @('nhl17', 'nhl24', 'shadPS4')) { throw 'The target is not an NHL emulator process.' }
$window = $game.MainWindowHandle
if ($window -eq [IntPtr]::Zero) { throw 'The game has no window yet.' }
$foreground = [Nhl17Window]::GetForegroundWindow()
if ($Focus) {
    [void][Nhl17Window]::ShowWindow($window, 9)
    $activation = New-Object -ComObject WScript.Shell
    [void]$activation.AppActivate($game.Id)
    if ([Nhl17Window]::GetForegroundWindow() -ne $window) {
        [Nhl17Window]::keybd_event(0x12, 0x38, 0, [UIntPtr]::Zero)
        [Nhl17Window]::keybd_event(0x12, 0x38, 2, [UIntPtr]::Zero)
    }
    [void][Nhl17Window]::SetForegroundWindow($window)
    Start-Sleep -Milliseconds 100
}
$namedKeys = @{ Cross = 0x4e; Circle = 0x42; Square = 0x56; Triangle = 0x43;
    Options = 0x0d; Up = 0x26; Down = 0x28; Left = 0x25; Right = 0x27;
    Escape = 0x1b; F10 = 0x79; F12 = 0x7b }
$arrowScans = @{ Up = 0x48; Down = 0x50; Left = 0x4b; Right = 0x4d }
if ($Screenshot -or $ScreenshotWithOverlays) { $Keys += 'F12' }
foreach ($key in $Keys) {
    $vk = if ($namedKeys.ContainsKey($key)) { $namedKeys[$key] }
          elseif ($key.Length -eq 1) { [int][char]$key.ToUpperInvariant() }
          else { throw "Unknown key: $key" }
    if ($NativeInput) {
        if ([Nhl17Window]::GetForegroundWindow() -ne $window) {
            throw 'Game is not focused; no native input sent.'
        }
        $scan = [byte][Nhl17Window]::MapVirtualKey($vk, 0)
        $flags = if ($arrowScans.ContainsKey($key)) { 1 } else { 0 }
        try {
            [Nhl17Window]::keybd_event([byte]$vk, $scan, $flags, [UIntPtr]::Zero)
            Start-Sleep -Milliseconds $HoldMs
        } finally {
            [Nhl17Window]::keybd_event([byte]$vk, $scan, ($flags -bor 2), [UIntPtr]::Zero)
        }
        Start-Sleep -Milliseconds 200
        continue
    }
    $keyData = if ($arrowScans.ContainsKey($key)) { 0x01000001 -bor ($arrowScans[$key] -shl 16) }
               else { 1 -bor ([Nhl17Window]::MapVirtualKey($vk, 0) -shl 16) }
    if ($ScreenshotWithOverlays -and $key -eq 'F12') {
        [void][Nhl17Window]::PostMessage($window, 0x100, [UIntPtr]0x12, [IntPtr]0x00380001)
        Start-Sleep -Milliseconds 100
    }
    if (-not [Nhl17Window]::PostMessage($window, 0x100, [UIntPtr]$vk, [IntPtr]$keyData)) {
        throw "Could not send $key to the game."
    }
    Start-Sleep -Milliseconds $HoldMs
    if (-not [Nhl17Window]::PostMessage($window, 0x101, [UIntPtr]$vk, [IntPtr]($keyData -bor [long]0xc0000000))) {
        throw "Could not release $key in the game."
    }
    if ($ScreenshotWithOverlays -and $key -eq 'F12') {
        [void][Nhl17Window]::PostMessage($window, 0x101, [UIntPtr]0x12, [IntPtr]([long]0xc0380001))
    }
    Start-Sleep -Milliseconds 200
}
if ($Close) {
    [void][Nhl17Window]::PostMessage($window, 0x10, [UIntPtr]::Zero, [IntPtr]::Zero)
}
[pscustomobject]@{ ProcessId = $game.Id; Keys = $Keys -join ', ';
    TargetFocused = $window -eq [Nhl17Window]::GetForegroundWindow();
    ForegroundUnchanged = $foreground -eq [Nhl17Window]::GetForegroundWindow() }
