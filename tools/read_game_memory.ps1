param([int]$GameProcessId, [long]$Address, [int]$Size, [string]$Output)
$ErrorActionPreference = 'Stop'
$game = Get-Process -Id $GameProcessId
if ($game.ProcessName -ne 'shadPS4') { throw 'Expected shadPS4 process.' }
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class NhlMemory {
    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern IntPtr OpenProcess(uint access, bool inherit, int pid);
    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern bool ReadProcessMemory(IntPtr process, IntPtr address, byte[] data, UIntPtr size, out UIntPtr read);
    [DllImport("kernel32.dll")]
    public static extern bool CloseHandle(IntPtr handle);
}
'@
$handle = [NhlMemory]::OpenProcess(0x410, $false, $GameProcessId)
if ($handle -eq [IntPtr]::Zero) { throw 'Cannot open emulator for read-only memory access.' }
try {
    $data = [byte[]]::new($Size)
    $read = [UIntPtr]::Zero
    if (-not [NhlMemory]::ReadProcessMemory($handle, [IntPtr]$Address, $data, [UIntPtr]$Size, [ref]$read)) {
        throw ('ReadProcessMemory failed: ' + [Runtime.InteropServices.Marshal]::GetLastWin32Error())
    }
    [IO.File]::WriteAllBytes($Output, $data)
    [pscustomobject]@{ Bytes = $read.ToUInt64(); Output = $Output }
} finally {
    [void][NhlMemory]::CloseHandle($handle)
}
