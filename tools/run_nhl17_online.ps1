# Starts NHL 17 connected to a CrossCrease server. This is what the CrossCrease launcher's Play button runs.
# The online connection files ship in the runtime package's online\ folder. Mirrors run_nhl17_server.ps1
# (crosscrease mode) for installed packages: the player's config is changed only for this session and
# restored when the game closes. With -BaseOnly (the game update turned off) the base game (1.00) starts
# online too; the server trust patch covers both versions.
param(
    [Parameter(Mandatory = $true)][string]$Game,
    [Parameter(Mandatory = $true)][string]$DataDirectory,
    [string]$ConnectionDirectory = '',
    [string]$ServerAddress = '',
    [ValidatePattern('^[a-zA-Z0-9_-]{3,16}$')][string]$OnlineId = 'NHL17Player',
    # The account password on the CrossCrease server. The launcher passes its Password setting in the
    # CROSSCREASE_SETTING_PASSWORD environment variable instead, so it never appears on a command line.
    # Without either, the shared developer password is sent, which only private test servers accept.
    [string]$Password = '',
    [string]$RuntimeDirectory = '',
    [switch]$BaseOnly,
    # Developer testing: a second player on the same PC (needs its own runtime folder).
    [switch]$AllowMultipleInstances
)
$ErrorActionPreference = 'Stop'
if (-not $Password -and $env:CROSSCREASE_SETTING_PASSWORD) { $Password = $env:CROSSCREASE_SETTING_PASSWORD }
Remove-Item Env:\CROSSCREASE_SETTING_PASSWORD -ErrorAction SilentlyContinue
# Windows PowerShell 5.1 can leave $PSScriptRoot empty while parameter defaults are evaluated.
$scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $ConnectionDirectory) { $ConnectionDirectory = Join-Path $scriptDirectory '..\online' }
if (-not $RuntimeDirectory) { $RuntimeDirectory = Join-Path $scriptDirectory '..\runtime' }
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
    # Sign-in is encrypted when the connection files name a TLS port and the CA certificate.
    $caCertificate = ''
    $identityPort = $connection.identity_tcp_port
    if ($connection.identity_tls_port) {
        $identityPort = $connection.identity_tls_port
        $caCertificate = (Resolve-Path -LiteralPath (Join-Path $connectionPath ([string]$connection.identity_ca_certificate_file))).Path
    }
    $config.Network.shadnet_server = "${ServerAddress}:$identityPort"
    $config.Network | Add-Member -NotePropertyName shadnet_ca_certificate -NotePropertyValue $caCertificate -Force
    # The WebAPI (which carries the player's token) uses HTTPS against the same CA when offered.
    if ($caCertificate -and $connection.psn_webapi_tls_port) {
        $config.Network.shadnet_webapi_server = "https://${ServerAddress}:$($connection.psn_webapi_tls_port)"
    } else {
        $config.Network.shadnet_webapi_server = "http://${ServerAddress}:$($connection.psn_webapi_http_port)"
    }
    $config | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $configPath
    $users = Get-Content -LiteralPath $usersPath -Raw | ConvertFrom-Json
    $user = $users.Users.user | Where-Object { $_.player_index -eq 1 } | Select-Object -First 1
    if (-not $user) { throw 'No primary emulator user exists.' }
    $user.shadnet_npid = $OnlineId
    # The emulator takes the password from SHADPS4_SHADNET_PASSWORD, so it is never written to users.json.
    $user.shadnet_password = ''
    $user.shadnet_token = ''
    $user.shadnet_email = ''
    $user.shadnet_enabled = $true
    $users | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $usersPath
    $env:SHADPS4_SHADNET_PASSWORD = $(if ($Password) { $Password } else { 'local-only' })
    $process = & (Join-Path $scriptDirectory 'run_nhl17.ps1') -Game $Game -RuntimeDirectory $runtimePath -DataDirectory $dataPath `
        -Label 'online' -NetworkHostOverridesJson $hostOverrides -PatchFile $patchPath -BaseOnly:$BaseOnly `
        -AllowMultipleInstances:$AllowMultipleInstances
    Remove-Item Env:\SHADPS4_SHADNET_PASSWORD -ErrorAction SilentlyContinue
    $Password = ''
    Wait-Process -Id $process.ProcessId
} finally {
    Remove-Item Env:\SHADPS4_SHADNET_PASSWORD -ErrorAction SilentlyContinue
    Copy-Item -LiteralPath $configBackup -Destination $configPath -Force
    Copy-Item -LiteralPath $usersBackup -Destination $usersPath -Force
}
