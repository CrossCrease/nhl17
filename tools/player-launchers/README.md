# Local two-player launchers

Build the two clickable Windows launchers from the NHL 17 project root:

```powershell
.\tools\build_player_launchers.ps1
```

Start the NHL 17 server first (sibling `crosscrease-server17` project):

```powershell
..\crosscrease-server17\nhl17-server.cmd start
```

Then double-click `artifacts\player-launchers\Player1.exe` and
`artifacts\player-launchers\Player2.exe`. Player 1 uses `runtime`; Player 2
creates and uses `runtime-player2` and the local identity `NHL17Probe2`.

Both launchers use `tools\run_nhl17_server.ps1`, pass isolated runtime paths,
and allow the two shadPS4 processes to run at the same time. Stop the server
separately with `..\crosscrease-server17\nhl17-server.cmd stop` after both game
instances close.
