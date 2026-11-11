param([ValidateSet(1,2)][int]$Player)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root
$label = 'multiplayer-player{0}-{1}-{2}' -f $Player,(Get-Date -Format 'yyyyMMdd-HHmmss'),([guid]::NewGuid().ToString('N').Substring(0,8))
$config = if ($Player -eq 1) { 'artifacts/multiplayer-lan-server.json' } else { 'artifacts/multiplayer-player2-server.json' }
$runtime = if ($Player -eq 1) { 'runtime' } else { 'artifacts/player2-runtime' }
& (Join-Path $PSScriptRoot 'run_nhl17_server.ps1') -ConfigPath $config -RuntimeDirectory $runtime -Label $label -AllowMultipleInstances -LogFilter '*:warning Core.Linker:info Lib.Net:info'
