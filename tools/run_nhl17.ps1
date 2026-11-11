param(
    [string]$Game = (Join-Path $PSScriptRoot '..\game\eboot.bin'),
    [ValidatePattern('^[a-zA-Z0-9_-]+$')][string]$Label = 'nhl17',
    [switch]$Validation,
    [switch]$FrameStats,
    [switch]$ProfileScopes,
    [switch]$DisableOverlays = $true,
    [ValidateSet(64, 128, 256, 512, 1024, 2048, 4096)][int]$BufferReadbackWindowKiB = 128,
    [string]$LogFilter = '',
    [string]$NetworkCaptureDirectory = '',
    [string]$NetworkHostOverridesJson = '',
    [string]$PatchFile = '',
    [string]$RuntimeDirectory = (Join-Path $PSScriptRoot '..\runtime'),
    # Per-player data folder (saves, settings, caches, logs) kept outside an installed runtime package.
    # shadPS4 uses <working directory>\user, so the game starts from this folder when it is set.
    [string]$DataDirectory = '',
    [switch]$AllowMultipleInstances,
    [switch]$BaseOnly
)
$ErrorActionPreference = 'Stop'
$runtimePath = (Resolve-Path -LiteralPath $RuntimeDirectory).Path
$gamePath = (Resolve-Path -LiteralPath $Game).Path
$exe = Join-Path $runtimePath 'shadPS4.exe'
if (-not (Test-Path -LiteralPath $exe)) { throw 'Build and package shadPS4.exe first.' }
if (Get-Process shadPS4 -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $exe }) { throw "This runtime already has a running game: $runtimePath" }
if (-not $AllowMultipleInstances -and (Get-Process shadPS4 -ErrorAction SilentlyContinue)) { throw 'A shadPS4 instance is already running.' }
if ($DataDirectory) {
    $workingPath = [IO.Path]::GetFullPath($DataDirectory)
    $userPath = Join-Path $workingPath 'user'
    New-Item -ItemType Directory -Path $userPath -Force | Out-Null
    $defaults = Join-Path $runtimePath 'defaults\user'
    foreach ($name in 'config.json', 'users.json') {
        $target = Join-Path $userPath $name
        if (-not (Test-Path -LiteralPath $target)) { Copy-Item -LiteralPath (Join-Path $defaults $name) -Destination $target }
    }
    # shadPS4 asks a "Save Migration" question whenever it creates a player's home folder. With the
    # hidden launch window nobody can answer it, so create the (empty) folders it would create.
    $seededUsers = Get-Content -LiteralPath (Join-Path $userPath 'users.json') -Raw | ConvertFrom-Json
    foreach ($player in $seededUsers.Users.user) {
        $playerHome = Join-Path $userPath ('home\' + [string]$player.user_id)
        if (-not (Test-Path -LiteralPath $playerHome)) {
            foreach ($folder in 'savedata', 'trophy', 'inputs') { New-Item -ItemType Directory -Path (Join-Path $playerHome $folder) -Force | Out-Null }
        }
    }
    $artifacts = Join-Path $workingPath 'logs'
} else {
    $workingPath = $runtimePath
    $userPath = Join-Path $runtimePath 'user'
    $artifacts = Join-Path $PSScriptRoot '..\artifacts'
}
New-Item -ItemType Directory -Path $artifacts -Force | Out-Null
$configPath = Join-Path $userPath 'config.json'
$config = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
$config.GPU.readback_linear_images_enabled = $true
$config.Vulkan.vkvalidation_enabled = [bool]$Validation
$config.Vulkan.vkvalidation_sync_enabled = [bool]$Validation
$config.Log.filter = if ($LogFilter) { $LogFilter } elseif ($ProfileScopes) { '*:warning Lib.VideoOut:info Render:info Render.Vulkan:info' } elseif ($FrameStats) { '*:warning Lib.VideoOut:info' } else { '*:warning' }
$config | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $configPath
$arguments = '--show-fps -g "' + $gamePath + '"'
if ($BaseOnly) { $arguments += ' --ignore-game-patch' }
$resolvedPatchFile = ''
if ($PatchFile) {
    $resolvedPatchFile = (Resolve-Path -LiteralPath $PatchFile).Path
    $arguments += ' -p "' + $resolvedPatchFile + '"'
}
$values = @{
    SDL_WINDOW_ACTIVATE_WHEN_SHOWN = '0'
    SDL_WINDOW_ACTIVATE_WHEN_RAISED = '0'
    SHADPS4_ENABLE_IPC = 'false'
    SHADPS4_BUFFER_READBACK_KIB = [string]$BufferReadbackWindowKiB
    SHADPS4_LOG_FRAME_STATS = $(if ($FrameStats -or $ProfileScopes) { '1' } else { $null })
    SHADPS4_PROFILE_SCOPES = $(if ($ProfileScopes) { '1' } else { $null })
    SHADPS4_NET_CAPTURE_DIR = $(if ($NetworkCaptureDirectory) {
        [IO.Path]::GetFullPath($NetworkCaptureDirectory)
    } else { $null })
    SHADPS4_NET_HOST_OVERRIDES_JSON = $(if ($NetworkHostOverridesJson) {
        (Resolve-Path -LiteralPath $NetworkHostOverridesJson).Path
    } else { $null })
}
if ($Validation) {
    $values.VK_LAYER_PATH = (Resolve-Path (Join-Path $PSScriptRoot '..\artifacts\vulkan-validation')).Path
}
if ($DisableOverlays) {
    $values.VK_LOADER_LAYERS_DISABLE = '~implicit~'
}
$original = @{}
try {
    foreach ($name in $values.Keys) {
        $original[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
        [Environment]::SetEnvironmentVariable($name, $values[$name], 'Process')
    }
    $process = Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory $workingPath `
        -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $artifacts "$Label.stdout.log") `
        -RedirectStandardError (Join-Path $artifacts "$Label.stderr.log")
    $process.Id | Set-Content -LiteralPath (Join-Path $artifacts "$Label.pid")
    [ordered]@{
        ProcessId = $process.Id
        StartedUtc = $process.StartTime.ToUniversalTime().ToString('o')
        Executable = $exe
        ExecutableSha256 = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
        Game = $gamePath
        Arguments = $arguments
        Validation = [bool]$Validation
        FrameStats = [bool]($FrameStats -or $ProfileScopes)
        ProfileScopes = [bool]$ProfileScopes
        ImplicitOverlaysDisabled = [bool]$DisableOverlays
        BufferReadbackWindowKiB = $BufferReadbackWindowKiB
        GpuReadbacksMode = $config.GPU.readbacks_mode
        LinearImageReadbacks = $config.GPU.readback_linear_images_enabled
        FastShaderMath = $config.GPU.fast_shader_math
        VblankFrequency = $config.GPU.vblank_frequency
        InternalWidth = $config.GPU.internal_screen_width
        InternalHeight = $config.GPU.internal_screen_height
        PresentMode = $config.GPU.present_mode
        NetworkCaptureDirectory = $values.SHADPS4_NET_CAPTURE_DIR
        NetworkHostOverridesJson = $values.SHADPS4_NET_HOST_OVERRIDES_JSON
        PatchFile = $resolvedPatchFile
    } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $artifacts "$Label.run.json")
    [pscustomobject]@{ ProcessId = $process.Id; Label = $Label; Validation = [bool]$Validation }
} finally {
    foreach ($name in $original.Keys) {
        [Environment]::SetEnvironmentVariable($name, $original[$name], 'Process')
    }
}
