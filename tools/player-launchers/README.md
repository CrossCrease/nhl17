# Local two-player launchers

Build the two clickable Windows launchers from the NHL 17 project root:

```powershell
.\tools\build_player_launchers.ps1
```

Start the Blaze stack first:

```powershell
cd ..\blaze\Blaze15.1.1.3.0-nhl.01\Games\NHL17\PS4\v1.08\Server\Host
.\start_stack.ps1
```

Then double-click `artifacts\player-launchers\Player1.exe` and
`artifacts\player-launchers\Player2.exe`. Player 1 uses `runtime`; Player 2
creates and uses `runtime-player2` and the local identity `NHL17Probe2`.

Both launchers use `tools\run_nhl17_server.ps1`, pass isolated runtime paths,
and allow the two shadPS4 processes to run at the same time. The server stack
must be stopped separately with `stop_stack.ps1` after both game instances
close.
