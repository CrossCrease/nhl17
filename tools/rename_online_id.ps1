# Renames a player's online ID on a CrossCrease server. The account stays the same: HUT, stats,
# friends and the password are kept. This is what the CrossCrease launcher runs when a player changes
# their Online ID and chooses to rename. The launcher passes the password in the
# CROSSCREASE_SETTING_PASSWORD environment variable; developers can use -Password.
#
# Prints one line for the player and exits with 0 when the server renamed the account.
param(
    [Parameter(Mandatory = $true)][ValidatePattern('^[a-zA-Z0-9_-]{3,16}$')][string]$OnlineId,
    [Parameter(Mandatory = $true)][string]$NewOnlineId,
    [string]$ServerAddress = '',
    [string]$Password = '',
    [string]$ConnectionDirectory = ''
)
$ErrorActionPreference = 'Stop'
if (-not $Password) { $Password = $env:CROSSCREASE_SETTING_PASSWORD }
Remove-Item Env:\CROSSCREASE_SETTING_PASSWORD -ErrorAction SilentlyContinue
$scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $ConnectionDirectory) { $ConnectionDirectory = Join-Path $scriptDirectory '..\online' }

function Fail([string]$message) {
    Write-Output $message
    exit 1
}

if (-not $Password) { Fail 'Enter your password in the launcher first.' }
try {
    $connection = Get-Content -LiteralPath (Join-Path $ConnectionDirectory 'server_connection.json') -Raw | ConvertFrom-Json
} catch {
    Fail "The runtime's online connection files are missing ($ConnectionDirectory)."
}
if (-not $ServerAddress -or $ServerAddress -eq 'default') { $ServerAddress = [string]$connection.server_address }
if ($ServerAddress -notmatch '^[a-zA-Z0-9.:-]+$') { Fail "Invalid server address: $ServerAddress" }
$port = [int]$connection.identity_tcp_port

function Field([int]$number, [string]$value) {
    $bytes = [Text.Encoding]::UTF8.GetBytes($value)
    if ($bytes.Length -gt 127) { Fail 'A value is too long.' }
    return [byte[]](@([byte](($number -shl 3) -bor 2), [byte]$bytes.Length) + $bytes)
}

function Read-Exact($stream, [int]$count) {
    $buffer = New-Object byte[] $count
    $offset = 0
    while ($offset -lt $count) {
        $read = $stream.Read($buffer, $offset, $count - $offset)
        if ($read -le 0) { throw 'The server closed the connection.' }
        $offset += $read
    }
    return $buffer
}

# ShadNet packet: u8 type, u16 command, u32 total size, u64 packet id, then the payload.
$changeOnlineId = 50
$message = [byte[]]((Field 1 $OnlineId) + (Field 2 $Password) + (Field 3 $NewOnlineId))
$payload = [BitConverter]::GetBytes([uint32]$message.Length) + $message
$packet = [byte[]](@([byte]0) + [BitConverter]::GetBytes([uint16]$changeOnlineId) +
    [BitConverter]::GetBytes([uint32](15 + $payload.Length)) + [BitConverter]::GetBytes([uint64]1) + $payload)
$Password = ''

$client = New-Object Net.Sockets.TcpClient
try {
    $connect = $client.BeginConnect($ServerAddress, $port, $null, $null)
    if (-not $connect.AsyncWaitHandle.WaitOne(10000) -or -not $client.Connected) { throw 'timeout' }
    $client.EndConnect($connect)
} catch {
    Fail "Could not reach the server at ${ServerAddress}:$port. Check the Server setting and your connection."
}
try {
    $stream = $client.GetStream()
    $stream.ReadTimeout = 15000
    $stream.Write($packet, 0, $packet.Length)
    while ($true) {
        $header = Read-Exact $stream 15
        $type = $header[0]
        $command = [BitConverter]::ToUInt16($header, 1)
        $size = [BitConverter]::ToUInt32($header, 3)
        $body = if ($size -gt 15) { Read-Exact $stream ($size - 15) } else { [byte[]]@() }
        if ($type -ne 1 -or $command -ne $changeOnlineId) { continue }
        $code = if ($body.Length -gt 0) { $body[0] } else { 33 }
        # Reply: error byte, u32 length, protobuf with the player message as string 1.
        $text = ''
        if ($body.Length -gt 6 -and $body[5] -eq 0x0A) {
            $length = 0; $shift = 0; $offset = 6
            do { $byte = $body[$offset]; $length = $length -bor (($byte -band 0x7F) -shl $shift); $shift += 7; $offset++ } while ($byte -ge 0x80 -and $offset -lt $body.Length)
            $text = [Text.Encoding]::UTF8.GetString($body, $offset, [Math]::Min($length, $body.Length - $offset))
        }
        if ($code -eq 0) {
            Write-Output $(if ($text) { $text } else { "Renamed to $NewOnlineId." })
            exit 0
        }
        if ($code -eq 33 -and -not $text) { Fail 'This server cannot rename accounts yet. Ask the server operator.' }
        Fail $(if ($text) { "The server did not rename the account: $text." } else { "The server did not rename the account (error $code)." })
    }
} catch {
    Fail "The server did not answer the rename request: $($_.Exception.Message)"
} finally {
    $client.Close()
}
