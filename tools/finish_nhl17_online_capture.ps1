param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[a-zA-Z0-9_-]+$')][string]$Label,
    [switch]$CloseGame
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$session = Join-Path $root (Join-Path 'artifacts\online-captures' $Label)
$statePath = Join-Path $session 'session.json'
if (-not (Test-Path -LiteralPath $statePath)) { throw "Unknown capture label: $Label" }
$state = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
$closedByScript = $false
$runtimeExecutable = (Resolve-Path (Join-Path $root 'runtime\shadPS4.exe')).Path
$gameProcesses = @()
if ($state.ProcessId) {
    $gameProcesses += Get-Process -Id $state.ProcessId -ErrorAction SilentlyContinue
}
$gameProcesses += Get-Process shadPS4 -ErrorAction SilentlyContinue | Where-Object {
    try { $_.Path -eq $runtimeExecutable } catch { $false }
}
$gameProcesses = @($gameProcesses | Sort-Object Id -Unique)
if ($gameProcesses.Count) {
    if (-not $CloseGame) {
        $runningIds = ($gameProcesses.Id -join ', ')
        throw "shadPS4 process(es) $runningIds are still running. Close the game or use -CloseGame before finishing the capture."
    }
    foreach ($gameProcess in $gameProcesses) {
        Stop-Process -Id $gameProcess.Id -ErrorAction SilentlyContinue
    }
    foreach ($gameProcess in $gameProcesses) {
        Wait-Process -Id $gameProcess.Id -Timeout 15 -ErrorAction SilentlyContinue
    }
    $closedByScript = $true
}
$configPath = Join-Path $root 'runtime\user\config.json'
$config = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
$config.Network.connected_to_network = [bool]$state.PriorConnectedToNetwork
$config.Network.shad_net_enabled = [bool]$state.PriorShadNetEnabled
$config.Log.filter = [string]$state.PriorLogFilter
$config | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $configPath
$summary = Join-Path $session 'summary.json'
$manifest = Join-Path $state.CaptureDirectory 'events.tsv'
$analysisStatus = 'complete'
if (Test-Path -LiteralPath $manifest) {
    python (Join-Path $PSScriptRoot 'analyze_nhl17_capture.py') $state.CaptureDirectory --output $summary | Out-Null
} else {
    $analysisStatus = 'no-events-manifest'
    [ordered]@{
        capture = $state.CaptureDirectory
        events = [ordered]@{}
        resolved_hosts = @()
        connection_attempts = @()
        tls_streams = @()
        fire2_streams = @()
        analysis_status = $analysisStatus
        warning = 'The game process produced no raw network capture manifest.'
    } | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $summary
}
$state | Add-Member -NotePropertyName FinishedUtc -NotePropertyValue ([DateTime]::UtcNow.ToString('o')) -Force
$state | Add-Member -NotePropertyName Summary -NotePropertyValue $summary -Force
$state | Add-Member -NotePropertyName AnalysisStatus -NotePropertyValue $analysisStatus -Force
$state | ConvertTo-Json | Set-Content -LiteralPath $statePath
$stamp = Get-Date -Format 'HH:mm'
$closeNote = if ($closedByScript) { 'Closed every shadPS4 process from the captured workspace runtime before finalization.' } else { 'Confirmed the shadPS4 process was already closed before finalization.' }
[IO.File]::AppendAllText((Join-Path $root 'devlog.md'), "`r`n### $stamp CET - Live capture $Label finished`r`n`r`n- $closeNote`r`n- Restored the pre-capture network and log settings.`r`n- Wrote the redacted protocol summary at ``artifacts/online-captures/$Label/summary.json`` with analysis status ``$analysisStatus``; raw payloads remain local-only.`r`n")
Get-Content -LiteralPath $summary -Raw | ConvertFrom-Json
