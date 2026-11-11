param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[a-zA-Z0-9_-]+$')][string]$Label
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$blazeRoot = (Resolve-Path (Join-Path $root '..\blaze')).Path
$relayRoot = Join-Path $blazeRoot 'Blaze15.1.1.3.0-nhl.01\Games\NHL17\PS4\v1.08\Server\TransportRelay'
$stateRoot = Join-Path $root (Join-Path 'artifacts\local-identity-sessions' $Label)
$statePath = Join-Path $stateRoot 'state.json'
if (-not (Test-Path -LiteralPath $statePath)) { throw "Unknown local identity label: $Label" }
$state = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
$summary = $null
$captureError = $null
try {
    try {
        $summary = & (Join-Path $PSScriptRoot 'finish_nhl17_online_capture.ps1') -Label $Label -CloseGame
    } catch {
        $captureError = $_
    }
    if ($state.ServerProcessId) {
        Stop-Process -Id $state.ServerProcessId -ErrorAction SilentlyContinue
    }
    if ($state.WebApiProcessId) {
        Stop-Process -Id $state.WebApiProcessId -ErrorAction SilentlyContinue
    }
    if ($state.RedirectorProcessId) {
        Stop-Process -Id $state.RedirectorProcessId -ErrorAction SilentlyContinue
    }
    if ($state.BlazeProcessId) {
        Stop-Process -Id $state.BlazeProcessId -ErrorAction SilentlyContinue
    }
    if ($state.CTSProcessId) {
        Stop-Process -Id $state.CTSProcessId -ErrorAction SilentlyContinue
    }
    if ($state.RelayStatePath) {
        & (Join-Path $relayRoot 'stop_wsl_relay.ps1') -StatePath $state.RelayStatePath
    }
} finally {
    Copy-Item -LiteralPath $state.ConfigBackup -Destination (Join-Path $root 'runtime\user\config.json') -Force
    Copy-Item -LiteralPath $state.UsersBackup -Destination (Join-Path $root 'runtime\user\users.json') -Force
}
$state | Add-Member -NotePropertyName FinishedUtc -NotePropertyValue ([DateTime]::UtcNow.ToString('o')) -Force
$state | Add-Member -NotePropertyName RestoredOriginalProfile -NotePropertyValue $true -Force
$state | Add-Member -NotePropertyName CaptureFinalizationError -NotePropertyValue $(if ($captureError) { $captureError.Exception.Message } else { $null }) -Force
$state | ConvertTo-Json | Set-Content -LiteralPath $statePath
$stamp = Get-Date -Format 'HH:mm'
$services = if ($state.RedirectorProcessId) { 'ShadNet identity, PSN WebAPI, TLS redirector probe, FIRE2 discovery probe, and CTS HTTP probe' } else { 'ShadNet identity and PSN WebAPI' }
[IO.File]::AppendAllText((Join-Path $root 'devlog.md'), "`r`n### $stamp CET - Local identity session $Label restored`r`n`r`n- Stopped the localhost-only $services.`r`n- Restored the complete pre-capture emulator network configuration and player profile from private ignored backups.`r`n")
if ($captureError) {
    throw $captureError
}
$summary
