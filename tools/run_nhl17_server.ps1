param(
    [string]$ConfigPath = (Join-Path (Resolve-Path (Join-Path $PSScriptRoot '..')).Path 'SERVER-CONNECTION.json'),
    [ValidatePattern('^[a-zA-Z0-9_-]+$')][string]$Label = ('server-' + (Get-Date -Format 'yyyyMMdd-HHmmss')),
    [string]$NetworkCaptureDirectory = '',
    [string]$LogFilter = '',
    [switch]$FrameStats,
    [switch]$ProfileScopes,
    [string]$RuntimeDirectory = (Join-Path $PSScriptRoot '..\runtime'),
    [switch]$AllowMultipleInstances
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
function Add-SessionLog([string]$Text) {
    $mutex = [Threading.Mutex]::new($false, 'Local\CrossCreaseNhl17SessionLog')
    $acquired = $false
    try {
        $acquired = $mutex.WaitOne(5000)
        if (-not $acquired) { throw 'Timed out waiting for the session log.' }
        [IO.File]::AppendAllText((Join-Path $root 'devlog.md'), $Text)
    } finally {
        if ($acquired) { $mutex.ReleaseMutex() }
        $mutex.Dispose()
    }
}
if (-not (Test-Path -LiteralPath $ConfigPath)) {
    # SERVER-CONNECTION.json is personal and not in Git; start from the shared template.
    $template = Join-Path $root 'SERVER-CONNECTION.example.json'
    if (-not (Test-Path -LiteralPath $template)) { throw "Server connection settings not found: $ConfigPath" }
    Copy-Item -LiteralPath $template -Destination $ConfigPath
}
$selection = Get-Content -LiteralPath $ConfigPath -Raw | ConvertFrom-Json
$mode = [string]$selection.mode
if ($mode -notin 'offline','crosscrease','default','custom') {
    throw "Unsupported server mode: $mode"
}
$session = Join-Path $root (Join-Path 'artifacts\server-sessions' $Label)
if (Test-Path -LiteralPath $session) { throw "Server session label already exists: $Label" }
New-Item -ItemType Directory -Path $session -Force | Out-Null
$runtimePath = (Resolve-Path -LiteralPath $RuntimeDirectory).Path
$runningRuntime = Get-Process shadPS4 -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq (Join-Path $runtimePath 'shadPS4.exe') }
if ($runningRuntime) { throw "This runtime already has a running game: $runtimePath" }
$configPathRuntime = Join-Path $runtimePath 'user\config.json'
$usersPath = Join-Path $runtimePath 'user\users.json'
$configBackup = Join-Path $session 'config.before.json'
$usersBackup = Join-Path $session 'users.before.json'
Copy-Item -LiteralPath $configPathRuntime -Destination $configBackup
Copy-Item -LiteralPath $usersPath -Destination $usersBackup
$hostOverrides = $null
try {
    $config = Get-Content -LiteralPath $configPathRuntime -Raw | ConvertFrom-Json
    $users = Get-Content -LiteralPath $usersPath -Raw | ConvertFrom-Json
    if ($mode -eq 'offline') {
        $config.Network.connected_to_network = $false
        $config.Network.shad_net_enabled = $false
    } elseif ($mode -in 'crosscrease','custom') {
        $target = $selection.$mode
        $onlineId = [string]$selection.online_id
        if ($onlineId -notmatch '^[a-zA-Z0-9_-]{3,16}$') { throw "Invalid online_id: $onlineId" }
        $serverAddress = [string]$target.server_address
        if (-not $serverAddress) { throw "Mode $mode has no server_address." }
        $config.Network.connected_to_network = $true
        $config.Network.shad_net_enabled = $true
        $config.Network.shadnet_server = "${serverAddress}:$($target.identity_tcp_port)"
        $config.Network.shadnet_webapi_server = "http://${serverAddress}:$($target.psn_webapi_http_port)"
        $user = $users.Users.user | Where-Object { $_.player_index -eq 1 } | Select-Object -First 1
        if (-not $user) { throw 'No primary emulator user exists.' }
        $user.shadnet_npid = $onlineId
        $user.shadnet_password = 'local-only'
        $user.shadnet_token = ''
        $user.shadnet_email = ''
        $user.shadnet_enabled = $true
        $hostOverrides = Join-Path $session 'host_overrides.json'
        [ordered]@{
            _mode = $mode
            _server = $serverAddress
            $target.redirector_hostname = $serverAddress
        } | ConvertTo-Json | Set-Content -LiteralPath $hostOverrides
    }
    if ($mode -ne 'default') {
        $config | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $configPathRuntime
        $users | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $usersPath
    }
    $stamp = Get-Date -Format 'HH:mm'
    Add-SessionLog "`r`n### $stamp CET - Server toggle launch $Label`r`n`r`n- Selected mode: ``$mode``.`r`n- Applied temporary emulator routing/profile changes. CrossCrease/custom may also load the configured public CA patch in memory for this launch; the owned game image remains unchanged.`r`n"
    $launcherArgs = @{ Label = $Label; RuntimeDirectory = $runtimePath; AllowMultipleInstances = $AllowMultipleInstances }
    if ($NetworkCaptureDirectory) { $launcherArgs.NetworkCaptureDirectory = $NetworkCaptureDirectory }
    if ($LogFilter) { $launcherArgs.LogFilter = $LogFilter }
    if ($FrameStats) { $launcherArgs.FrameStats = $true }
    if ($ProfileScopes) { $launcherArgs.ProfileScopes = $true }
    if ($hostOverrides) { $launcherArgs.NetworkHostOverridesJson = $hostOverrides }
    if ($mode -in 'crosscrease','custom') {
        $configuredPatch = [string]$selection.$mode.trust_patch_file
        if ($configuredPatch) {
            $patchPath = if ([IO.Path]::IsPathRooted($configuredPatch)) {
                $configuredPatch
            } else {
                Join-Path $root $configuredPatch
            }
            if (-not (Test-Path -LiteralPath $patchPath)) {
                throw "Server trust patch not found: $patchPath. Run the Blaze Redirector provision_server_trust.ps1 first."
            }
            $launcherArgs.PatchFile = (Resolve-Path -LiteralPath $patchPath).Path
        }
    }
    $process = & (Join-Path $PSScriptRoot 'run_nhl17.ps1') @launcherArgs
    Wait-Process -Id $process.ProcessId
} finally {
    Copy-Item -LiteralPath $configBackup -Destination $configPathRuntime -Force
    Copy-Item -LiteralPath $usersBackup -Destination $usersPath -Force
    $stamp = Get-Date -Format 'HH:mm'
    Add-SessionLog "`r`n### $stamp CET - Server toggle session $Label restored`r`n`r`n- Restored the complete pre-launch emulator configuration and player profile.`r`n"
}
