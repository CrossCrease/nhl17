param(
    [ValidatePattern('^[a-zA-Z0-9_-]+$')][string]$Label = ('online-local-' + (Get-Date -Format 'yyyyMMdd-HHmmss')),
    [ValidatePattern('^[a-zA-Z0-9_-]{3,16}$')][string]$OnlineId = 'NHL17Probe',
    [switch]$UseLocalRedirector,
    [switch]$UseProvisionedRedirectorTrust,
    [string]$RedirectorAddress = '127.0.0.1',
    [string]$RedirectorBindAddress = '',
    [ValidateRange(1, 65535)][int]$RedirectorListenPort = 42230,
    [string]$BlazeAddress = '',
    [ValidateRange(1, 65535)][int]$BlazePort = 10041,
    [int]$BlazeListenPort = 0
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$blazeRoot = (Resolve-Path (Join-Path $root '..\blaze')).Path
$serverRoot = Join-Path $blazeRoot 'Blaze15.1.1.3.0-nhl.01\Games\NHL17\PS4\v1.08\Server'
$identityRoot = Join-Path $serverRoot 'Identity'
$psnWebApiRoot = Join-Path $serverRoot 'PSNWebAPI'
$redirectorRoot = Join-Path $serverRoot 'Redirector'
$blazeComponentRoot = Join-Path $serverRoot 'Blaze'
$ctsRoot = Join-Path $serverRoot 'CTS'
$stateRoot = Join-Path $root (Join-Path 'artifacts\local-identity-sessions' $Label)
if (Test-Path -LiteralPath $stateRoot) { throw "Local identity label already exists: $Label" }
if (Get-Process shadPS4 -ErrorAction SilentlyContinue) { throw 'A shadPS4 instance is already running.' }
New-Item -ItemType Directory -Path $stateRoot -Force | Out-Null
$configPath = Join-Path $root 'runtime\user\config.json'
$usersPath = Join-Path $root 'runtime\user\users.json'
$configBackup = Join-Path $stateRoot 'config.before.json'
$usersBackup = Join-Path $stateRoot 'users.before.json'
Copy-Item -LiteralPath $configPath -Destination $configBackup
Copy-Item -LiteralPath $usersPath -Destination $usersBackup
$statePath = Join-Path $stateRoot 'state.json'
$state = [ordered]@{
    Label = $Label
    OnlineId = $OnlineId
    StartedUtc = [DateTime]::UtcNow.ToString('o')
    ServerProcessId = $null
    WebApiProcessId = $null
    RedirectorProcessId = $null
    BlazeProcessId = $null
    CTSProcessId = $null
    HostOverridesPath = $null
    TrustPatchPath = $null
    ConfigBackup = $configBackup
    UsersBackup = $usersBackup
}
$state | ConvertTo-Json | Set-Content -LiteralPath $statePath
try {
    if ($UseProvisionedRedirectorTrust -and -not $UseLocalRedirector) {
        throw 'UseProvisionedRedirectorTrust requires UseLocalRedirector.'
    }
    if ($BlazeListenPort -lt 0 -or $BlazeListenPort -gt 65535) {
        throw 'BlazeListenPort must be 0 or a valid TCP port.'
    }
    $config = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
    $config.Network.shadnet_server = '127.0.0.1:31313'
    $config.Network.shadnet_webapi_server = 'http://127.0.0.1:31315'
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
    $python = (Get-Command python).Source
    $server = Start-Process -FilePath $python -ArgumentList '-u','local_shadnet.py','--port','31313','--listen-delay','10' `
        -WorkingDirectory $identityRoot -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $stateRoot 'local-shadnet.stdout.log') `
        -RedirectStandardError (Join-Path $stateRoot 'local-shadnet.stderr.log')
    $state.ServerProcessId = $server.Id
    $state | ConvertTo-Json | Set-Content -LiteralPath $statePath
    $webApi = Start-Process -FilePath $python -ArgumentList '-u','local_psn_webapi.py','--port','31315' `
        -WorkingDirectory $psnWebApiRoot -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $stateRoot 'local-psn-webapi.stdout.log') `
        -RedirectStandardError (Join-Path $stateRoot 'local-psn-webapi.stderr.log')
    $state.WebApiProcessId = $webApi.Id
    $state | ConvertTo-Json | Set-Content -LiteralPath $statePath
    Start-Sleep -Milliseconds 500
    if ($server.HasExited) { throw 'The local ShadNet identity server exited during startup.' }
    if ($webApi.HasExited) { throw 'The local PSN WebAPI server exited during startup.' }
    $hostOverridesPath = ''
    if ($UseLocalRedirector) {
        $bindAddress = if ($RedirectorBindAddress) { $RedirectorBindAddress } else { $RedirectorAddress }
        $advertisedBlazeAddress = if ($BlazeAddress) { $BlazeAddress } else { $RedirectorAddress }
        $boundBlazePort = if ($BlazeListenPort) { $BlazeListenPort } else { $BlazePort }
        if ($UseProvisionedRedirectorTrust) {
            $trust = & (Join-Path $redirectorRoot 'provision_server_trust.ps1')
            $certificate = [pscustomobject]@{
                CertificatePath = $trust.ServerCertificatePath
                PrivateKeyPath = $trust.ServerPrivateKeyPath
            }
            $state.TrustPatchPath = $trust.PlayerPatchPath
        } else {
            $certificate = & (Join-Path $redirectorRoot 'create_dev_certificate.ps1')
        }
        $blaze = Start-Process -FilePath $python `
            -ArgumentList '-u','fire2_probe.py','--bind',$bindAddress,'--port',$boundBlazePort,'--cts-port','31314' `
            -WorkingDirectory $blazeComponentRoot -WindowStyle Hidden -PassThru `
            -RedirectStandardOutput (Join-Path $stateRoot 'local-blaze.stdout.log') `
            -RedirectStandardError (Join-Path $stateRoot 'local-blaze.stderr.log')
        $state.BlazeProcessId = $blaze.Id
        $cts = Start-Process -FilePath $python `
            -ArgumentList '-u','local_cts_probe.py','--bind',$bindAddress,'--port','31314' `
            -WorkingDirectory $ctsRoot -WindowStyle Hidden -PassThru `
            -RedirectStandardOutput (Join-Path $stateRoot 'local-cts.stdout.log') `
            -RedirectStandardError (Join-Path $stateRoot 'local-cts.stderr.log')
        $state.CTSProcessId = $cts.Id
        $redirector = Start-Process -FilePath $python `
            -ArgumentList '-u','local_tls_probe.py','--bind',$bindAddress,'--port',$RedirectorListenPort,'--certificate',$certificate.CertificatePath,'--private-key',$certificate.PrivateKeyPath,'--respond','--blaze-host',$advertisedBlazeAddress,'--blaze-port',$BlazePort `
            -WorkingDirectory $redirectorRoot -WindowStyle Hidden -PassThru `
            -RedirectStandardOutput (Join-Path $stateRoot 'local-redirector.stdout.log') `
            -RedirectStandardError (Join-Path $stateRoot 'local-redirector.stderr.log')
        $state.RedirectorProcessId = $redirector.Id
        $hostOverridesPath = Join-Path $stateRoot 'host_overrides.json'
        [ordered]@{
            'winter15.gosredirector.ea.com' = $RedirectorAddress
        } | ConvertTo-Json | Set-Content -LiteralPath $hostOverridesPath
        $state.HostOverridesPath = $hostOverridesPath
        $state | ConvertTo-Json | Set-Content -LiteralPath $statePath
        Start-Sleep -Milliseconds 500
        if ($blaze.HasExited) { throw 'The local FIRE2 discovery probe exited during startup.' }
        if ($cts.HasExited) { throw 'The local CTS HTTP probe exited during startup.' }
        if ($redirector.HasExited) { throw 'The local TLS redirector probe exited during startup.' }
    }
    $captureArgs = @{ Label = $Label; UseShadNet = $true }
    if ($hostOverridesPath) { $captureArgs.NetworkHostOverridesJson = $hostOverridesPath }
    if ($state.TrustPatchPath) { $captureArgs.PatchFile = $state.TrustPatchPath }
    $capture = & (Join-Path $PSScriptRoot 'start_nhl17_online_capture.ps1') @captureArgs
    [pscustomobject]@{
        Label = $Label
        ProcessId = $capture.ProcessId
        LocalIdentity = $OnlineId
        IdentityServerProcessId = $server.Id
        RedirectorProbeProcessId = $state.RedirectorProcessId
        BlazeProbeProcessId = $state.BlazeProcessId
        CTSProbeProcessId = $state.CTSProcessId
    }
} catch {
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
    Copy-Item -LiteralPath $configBackup -Destination $configPath -Force
    Copy-Item -LiteralPath $usersBackup -Destination $usersPath -Force
    throw
}
