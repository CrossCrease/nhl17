# Starts NHL 17 connected to a CrossCrease server using an installed online connection package.
# Used by the CrossCrease launcher; mirrors run_nhl17_server.ps1 (crosscrease mode) for installed packages:
# the player's config is changed only for this session and restored when the game closes.
param(
    [Parameter(Mandatory = $true)][string]$Game,
    [Parameter(Mandatory = $true)][string]$ConnectionDirectory,
    [Parameter(Mandatory = $true)][string]$DataDirectory,
    [string]$ServerAddress = '',
    [ValidatePattern('^[a-zA-Z0-9_-]{3,16}$')][string]$OnlineId = 'NHL17Player',
    [string]$RuntimeDirectory = (Join-Path $PSScriptRoot '..\runtime')
)
$ErrorActionPreference = 'Stop'
$connectionPath = (Resolve-Path -LiteralPath $ConnectionDirectory).Path
$connection = Get-Content -LiteralPath (Join-Path $connectionPath 'server_connection.json') -Raw | ConvertFrom-Json
if (-not $ServerAddress -or $ServerAddress -eq 'default') { $ServerAddress = [string]$connection.server_address }
if ($ServerAddress -notmatch '^[a-zA-Z0-9.:-]+$') { throw "Invalid server address: $ServerAddress" }
$patchPath = Join-Path $connectionPath ([string]$connection.trust_patch_file)
if (-not (Test-Path -LiteralPath $patchPath)) { throw "The server trust patch is missing: $patchPath" }

$dataPath = [IO.Path]::GetFullPath($DataDirectory)
$userPath = Join-Path $dataPath 'user'
$session = Join-Path $dataPath ('logs\online-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $session -Force | Out-Null
$hostOverrides = Join-Path $session 'host_overrides.json'
[ordered]@{
    _pack = [string]$connection.pack
    _server = $ServerAddress
    $connection.redirector_hostname = $ServerAddress
} | ConvertTo-Json | Set-Content -LiteralPath $hostOverrides

# run_nhl17.ps1 seeds missing defaults; do it first so there is a profile to back up.
$runtimePath = (Resolve-Path -LiteralPath $RuntimeDirectory).Path
New-Item -ItemType Directory -Path $userPath -Force | Out-Null
foreach ($name in 'config.json', 'users.json') {
    $target = Join-Path $userPath $name
    if (-not (Test-Path -LiteralPath $target)) { Copy-Item -LiteralPath (Join-Path $runtimePath "defaults\user\$name") -Destination $target }
}
$configPath = Join-Path $userPath 'config.json'
$usersPath = Join-Path $userPath 'users.json'
$configBackup = Join-Path $session 'config.before.json'
$usersBackup = Join-Path $session 'users.before.json'
Copy-Item -LiteralPath $configPath -Destination $configBackup
Copy-Item -LiteralPath $usersPath -Destination $usersBackup
try {
    $config = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
    $config.Network.connected_to_network = $true
    $config.Network.shad_net_enabled = $true
    $config.Network.shadnet_server = "${ServerAddress}:$($connection.identity_tcp_port)"
    $config.Network.shadnet_webapi_server = "http://${ServerAddress}:$($connection.psn_webapi_http_port)"
    $config | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $configPath
    $users = Get-Content -LiteralPath $usersPath -Raw | ConvertFrom-Json
    $user = $users.Users.user | Where-Object { $_.player_index -eq 1 } | Select-Object -First 1
    if (-not $user) { throw 'No primary emulator user exists.' }
    $user.shadnet_npid = $OnlineId
    $user.shadnet_password = 'local-only'
    $user.shadnet_token = ''
    $user.shadnet_email = ''
    $user.shadnet_enabled = $true
    $users | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $usersPath
    $process = & (Join-Path $PSScriptRoot 'run_nhl17.ps1') -Game $Game -RuntimeDirectory $runtimePath -DataDirectory $dataPath `
        -Label 'online' -NetworkHostOverridesJson $hostOverrides -PatchFile $patchPath
    Wait-Process -Id $process.ProcessId
} finally {
    Copy-Item -LiteralPath $configBackup -Destination $configPath -Force
    Copy-Item -LiteralPath $usersBackup -Destination $usersPath -Force
}
