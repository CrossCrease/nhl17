param(
    [ValidatePattern('^[a-zA-Z0-9_-]+$')][string]$Label = ('online-local-relay-' + (Get-Date -Format 'yyyyMMdd-HHmmss')),
    [ValidatePattern('^[a-zA-Z0-9_-]{3,16}$')][string]$OnlineId = 'NHL17Probe',
    [string]$Distro = 'docker-desktop'
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$blazeRoot = (Resolve-Path (Join-Path $root '..\blaze')).Path
$relayRoot = Join-Path $blazeRoot 'Blaze15.1.1.3.0-nhl.01\Games\NHL17\PS4\v1.08\Server\TransportRelay'
$relay = $null
try {
    $relay = & (Join-Path $relayRoot 'start_wsl_relay.ps1') -Label $Label -Distro $Distro
    $capture = & (Join-Path $PSScriptRoot 'start_nhl17_local_identity_capture.ps1') `
        -Label $Label -OnlineId $OnlineId -UseLocalRedirector -UseProvisionedRedirectorTrust `
        -RedirectorAddress $relay.RelayAddress -RedirectorBindAddress $relay.WindowsAddress `
        -RedirectorListenPort $relay.BackendRedirectorPort `
        -BlazeAddress $relay.RelayAddress -BlazePort $relay.PublicBlazePort `
        -BlazeListenPort $relay.BackendBlazePort
    $statePath = Join-Path $root (Join-Path 'artifacts\local-identity-sessions' (Join-Path $Label 'state.json'))
    $state = Get-Content -Raw -LiteralPath $statePath | ConvertFrom-Json
    $state | Add-Member -NotePropertyName RelayStatePath -NotePropertyValue $relay.StatePath -Force
    $state | Add-Member -NotePropertyName TransportMode -NotePropertyValue 'docker-wsl-tcp-relay' -Force
    $state | ConvertTo-Json | Set-Content -LiteralPath $statePath
    $capture | Add-Member -NotePropertyName RelayAddress -NotePropertyValue $relay.RelayAddress -Force
    $capture | Add-Member -NotePropertyName RelayProcessIds -NotePropertyValue $relay.ProcessIds -Force
    $capture
} catch {
    if ($relay -and $relay.StatePath) {
        & (Join-Path $relayRoot 'stop_wsl_relay.ps1') -StatePath $relay.StatePath -ErrorAction SilentlyContinue
    }
    throw
}
