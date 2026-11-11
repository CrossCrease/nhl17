param(
    [string]$Out = (Join-Path $PSScriptRoot '..\artifacts\player-launchers')
)

$ErrorActionPreference = 'Stop'
$compiler = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
if (-not (Test-Path -LiteralPath $compiler)) { throw "csc.exe not found at $compiler" }

$source = Join-Path $PSScriptRoot 'player-launchers\PlayerLauncher.cs'
if (-not (Test-Path -LiteralPath $source)) { throw "Launcher source not found: $source" }

New-Item -ItemType Directory -Force -Path $Out | Out-Null
foreach ($name in @('Player1', 'Player2')) {
    $output = Join-Path $Out ($name + '.exe')
    & $compiler /nologo /target:winexe /optimize+ "/out:$output" `
        /r:System.dll /r:System.Web.Extensions.dll /r:System.Windows.Forms.dll `
        $source
    if ($LASTEXITCODE -ne 0) { throw "csc failed for $name ($LASTEXITCODE)" }
}

Get-ChildItem -LiteralPath $Out -Filter 'Player?.exe' |
    Select-Object FullName, Length, LastWriteTime
