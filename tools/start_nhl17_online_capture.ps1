param(
    [ValidatePattern('^[a-zA-Z0-9_-]+$')][string]$Label = ('online-' + (Get-Date -Format 'yyyyMMdd-HHmmss')),
    [switch]$UseShadNet,
    [string]$NetworkHostOverridesJson = '',
    [string]$PatchFile = ''
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$session = Join-Path $root (Join-Path 'artifacts\online-captures' $Label)
if (Test-Path -LiteralPath $session) { throw "Capture label already exists: $Label" }
New-Item -ItemType Directory -Path $session -Force | Out-Null
$raw = Join-Path $session 'raw'
$configPath = Join-Path $root 'runtime\user\config.json'
$config = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
$state = [ordered]@{
    Label = $Label
    StartedUtc = [DateTime]::UtcNow.ToString('o')
    ProcessId = $null
    CaptureDirectory = $raw
    PriorConnectedToNetwork = [bool]$config.Network.connected_to_network
    PriorShadNetEnabled = [bool]$config.Network.shad_net_enabled
    PriorLogFilter = [string]$config.Log.filter
    PatchFile = $PatchFile
}
$state | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $session 'session.json')
try {
    $config.Network.connected_to_network = $true
    $config.Network.shad_net_enabled = [bool]$UseShadNet
    $config | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $configPath
    python (Join-Path $PSScriptRoot 'inspect_nhl17_online.py') `
        --output (Join-Path $session 'static-client-evidence.json') | Out-Null
    $logFilter = '*:warning Loader:info Lib.Net:trace Lib.Http:trace Lib.Ssl:trace Lib.NetCtl:debug Lib.Np:debug ShadNet:debug'
    $launch = @{
        Label = $Label
        NetworkCaptureDirectory = $raw
        LogFilter = $logFilter
    }
    if ($NetworkHostOverridesJson) {
        $launch.NetworkHostOverridesJson = $NetworkHostOverridesJson
    }
    if ($PatchFile) {
        $launch.PatchFile = $PatchFile
    }
    $process = & (Join-Path $PSScriptRoot 'run_nhl17.ps1') @launch
    $state.ProcessId = $process.ProcessId
    $state | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $session 'session.json')
    $stamp = Get-Date -Format 'HH:mm'
    $patchNote = if ($PatchFile) { "- Loaded the session's version-locked public redirector CA patch through shadPS4's external patch option.`r`n" } else { '' }
    [IO.File]::AppendAllText((Join-Path $root 'devlog.md'), "`r`n### $stamp CET - Live capture $Label started`r`n`r`n- Enabled the emulator's network-connected state for this session; ShadNet enabled: $([bool]$UseShadNet).`r`n- Started shadPS4 PID $($process.ProcessId) with opt-in raw socket capture at ``artifacts/online-captures/$Label/raw``.`r`n$patchNote- Raw payloads remain local and may contain account or authorization data; they must not be published.`r`n")
    [pscustomobject]@{ Label = $Label; ProcessId = $process.ProcessId; CaptureDirectory = $raw }
} catch {
    $config = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
    $config.Network.connected_to_network = $state.PriorConnectedToNetwork
    $config.Network.shad_net_enabled = $state.PriorShadNetEnabled
    $config.Log.filter = $state.PriorLogFilter
    $config | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $configPath
    throw
}
