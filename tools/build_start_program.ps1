# Builds tools\start\CrossCreaseNhl17.cs into crosscrease-nhl17.exe, the program the CrossCrease launcher
# runs for Play and for renaming an online ID. It needs only the .NET Framework 4 compiler that ships
# with Windows, and runs under Wine/Proton (wine-mono) as well as on Windows.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\build_start_program.ps1 [-Out <file>]
param(
    [string]$Out = (Join-Path $PSScriptRoot '..\artifacts\start\crosscrease-nhl17.exe')
)
$ErrorActionPreference = 'Stop'
$compiler = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
if (-not (Test-Path -LiteralPath $compiler)) { throw "csc.exe not found at $compiler" }
$source = Join-Path $PSScriptRoot 'start\CrossCreaseNhl17.cs'
$output = [IO.Path]::GetFullPath($Out)
New-Item -ItemType Directory -Path (Split-Path -Parent $output) -Force | Out-Null
& $compiler /nologo /target:winexe /optimize+ "/out:$output" `
    /r:System.dll /r:System.Core.dll /r:System.Xml.dll /r:System.Web.Extensions.dll /r:System.Windows.Forms.dll `
    $source
if ($LASTEXITCODE -ne 0) { throw "csc failed ($LASTEXITCODE)" }
Get-Item -LiteralPath $output | Select-Object FullName, Length, LastWriteTime
