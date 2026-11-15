# Builds the downloadable CrossCrease NHL 17 packages and the feed the CrossCrease launcher reads.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\package_release.ps1
#
# Output (artifacts\release\<version>\), uploaded unchanged as the assets of GitHub release v<version>:
#   crosscrease-nhl17-runtime-<version>-windows-x64.zip  emulator runtime, default config, launch scripts and,
#                                                        in online\, the public files for connecting to a CrossCrease server
#   crosscrease-nhl17-feed.json                          package list with versions, sizes and SHA-256
#   SHA256SUMS.txt
#
# Packages are built from an allowlist. Game files, firmware, keys, saves, caches and the developer's
# runtime\user folder are never included.
param(
    [string]$RuntimeDirectory = (Join-Path $PSScriptRoot '..\runtime'),
    [string]$OnlineDirectory = (Join-Path $PSScriptRoot '..\..\blaze\Blaze15.1.1.3.0-nhl.01\Games\NHL17\PS4\v1.08\Player'),
    [string]$OutputDirectory = '',
    [string]$ServerAddress = '',
    [string]$Repository = 'CrossCrease/nhl17',
    [string]$MinimumLauncher = '0.7.0'
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem

$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$version = (Get-Content -LiteralPath (Join-Path $PSScriptRoot 'release\VERSION') -Raw).Trim()
if ($version -notmatch '^\d+\.\d+\.\d+(-[0-9A-Za-z.-]+)?$') { throw "VERSION is not a semantic version: $version" }
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $root "artifacts\release\$version" }
$out = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $out) { Remove-Item -LiteralPath $out -Recurse -Force }
$staging = Join-Path $out '.staging'
New-Item -ItemType Directory -Path $staging -Force | Out-Null

function Copy-Listed([string]$From, [string[]]$Files, [string]$To) {
    foreach ($file in $Files) {
        $source = Join-Path $From $file
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Missing release file: $source" }
        $target = Join-Path $To $file
        New-Item -ItemType Directory -Path (Split-Path $target) -Force | Out-Null
        Copy-Item -LiteralPath $source -Destination $target
    }
}

function New-Package([string]$Folder, [string]$FileName) {
    $zip = Join-Path $out $FileName
    $base = (Resolve-Path -LiteralPath $Folder).Path.TrimEnd('\') + '\'
    $archive = [IO.Compression.ZipFile]::Open($zip, [IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach ($file in Get-ChildItem -LiteralPath $Folder -Recurse -File | Sort-Object FullName) {
            $name = $file.FullName.Substring($base.Length).Replace('\', '/')
            [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $file.FullName, $name, [IO.Compression.CompressionLevel]::Optimal)
        }
    } finally { $archive.Dispose() }
    $item = Get-Item -LiteralPath $zip
    [pscustomobject]@{ File = $FileName; Size = $item.Length; Sha256 = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant() }
}

$sourceUrl = "https://github.com/$Repository/tree/v$version"
$notice = @"
CrossCrease NHL 17 runtime $version

This package is part of CrossCrease and is installed by the CrossCrease launcher.
It contains a build of shadPS4 (https://github.com/shadps4-emu/shadPS4) with CrossCrease
changes for NHL 17. It is not an official shadPS4 release.

shadPS4 is licensed under the GNU General Public License v2.0 or later; see LICENSE and LICENSES.
The complete corresponding source code for this build is available at:
$sourceUrl

The online\ folder holds the public client files for connecting to a CrossCrease server (server
address, the server's public CA certificate and the in-memory trust patch). They come from the
CrossCrease Blaze server project and contain no private keys.

The patches\ folder holds the CrossCrease compatibility patch for NHL 17 update 1.08, applied in
memory when the game starts. It changes a few bytes of game code; the game files are not modified.

No game files, console firmware, keys or saves are included. You need your own copy of NHL 17.
"@

# Runtime package
$runtimeStage = Join-Path $staging 'runtime'
$runtimeFiles = 'shadPS4.exe', 'msvcp140.dll', 'msvcp140_atomic_wait.dll', 'vcruntime140.dll', 'vcruntime140_1.dll'
Copy-Listed (Resolve-Path -LiteralPath $RuntimeDirectory).Path $runtimeFiles (Join-Path $runtimeStage 'runtime')
Copy-Listed (Join-Path $PSScriptRoot 'release\defaults\user') 'config.json', 'users.json' (Join-Path $runtimeStage 'runtime\defaults\user')
Copy-Listed $PSScriptRoot 'run_nhl17.ps1', 'run_nhl17_online.ps1', 'rename_online_id.ps1' (Join-Path $runtimeStage 'tools')
Copy-Listed (Join-Path $root 'patches') 'nhl17-compat.xml' (Join-Path $runtimeStage 'patches')
Copy-Listed $root 'LICENSE' $runtimeStage
Copy-Item -LiteralPath (Join-Path $root 'LICENSES') -Destination (Join-Path $runtimeStage 'LICENSES') -Recurse
Set-Content -LiteralPath (Join-Path $runtimeStage 'README.txt') -Value $notice -Encoding ASCII

# Online connection files (public files only; requirements.json lists what must never ship)
$onlineStage = Join-Path $runtimeStage 'online'
$onlinePath = (Resolve-Path -LiteralPath $OnlineDirectory).Path
Copy-Listed $onlinePath 'server_connection.json', 'requirements.json', 'README.md', 'generated\nhl17-crosscrease-ca.xml', 'generated\crosscrease-ca-cert.pem', 'generated\trust-manifest.json' $onlineStage
$manifest = Get-Content -LiteralPath (Join-Path $onlineStage 'generated\trust-manifest.json') -Raw | ConvertFrom-Json
if ($manifest.contains_private_key) { throw 'The trust manifest says it contains a private key; refusing to package it.' }
if (@(Get-ChildItem -LiteralPath $onlineStage -Recurse -File | Select-String -Pattern '-----BEGIN ([A-Z]+ )?PRIVATE KEY-----' -CaseSensitive).Count -gt 0) { throw 'A private key was found in the online connection files.' }
$connectionFile = Join-Path $onlineStage 'server_connection.json'
if ($ServerAddress) {
    if ($ServerAddress -notmatch '^[a-zA-Z0-9.:-]+$') { throw "Invalid server address: $ServerAddress" }
    $connection = Get-Content -LiteralPath $connectionFile -Raw | ConvertFrom-Json
    $connection.server_address = $ServerAddress
    $connection | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $connectionFile -Encoding ASCII
}
$defaultServer = [string](Get-Content -LiteralPath $connectionFile -Raw | ConvertFrom-Json).server_address
if ($defaultServer -match '^(localhost|127\.|10\.|192\.168\.|172\.(1[6-9]|2[0-9]|3[01])\.)') {
    Write-Warning "The default server is $defaultServer, which only works on the packaging machine or its LAN. Pass -ServerAddress <public host name> for a public release."
}
try { [void][Net.Dns]::GetHostAddresses(($defaultServer -split ':')[0]) }
catch { Write-Warning "The default server $defaultServer does not resolve. Players could not connect; add its DNS record before publishing this package." }
$runtimePackage = New-Package $runtimeStage "crosscrease-nhl17-runtime-$version-windows-x64.zip"

Remove-Item -LiteralPath $staging -Recurse -Force

$feed = [ordered]@{
    schema_version = 1
    game = 'nhl17'
    release = $version
    prerelease = $version.Contains('-')
    published = (Get-Date).ToUniversalTime().ToString('yyyy-MM-dd')
    notes_url = "https://github.com/$Repository/releases/tag/v$version"
    source_url = $sourceUrl
    minimum_launcher = $MinimumLauncher
    packages = @(
        [ordered]@{
            id = 'runtime'; name = 'NHL 17 runtime'; version = $version
            description = 'shadPS4-based emulator runtime with CrossCrease NHL 17 compatibility work and the online connection files.'
            url = $runtimePackage.File; size = $runtimePackage.Size; sha256 = $runtimePackage.Sha256
        }
    )
}
$feedFile = Join-Path $out 'crosscrease-nhl17-feed.json'
$feed | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $feedFile -Encoding ASCII
@($runtimePackage) | ForEach-Object { "$($_.Sha256)  $($_.File)" } |
    Set-Content -LiteralPath (Join-Path $out 'SHA256SUMS.txt') -Encoding ASCII
Get-ChildItem -LiteralPath $out | Select-Object Name, Length
