# NHL 17 server connection toggle

Edit the `mode` in `SERVER-CONNECTION.json`, then run the command below. The file holds your own address and online ID, so it is not in Git. It is created from `SERVER-CONNECTION.example.json` the first time you run the launcher script or a player launcher.

```powershell
.\tools\run_nhl17_server.ps1
```

Available modes:

- `offline` — launch with emulated network connectivity disabled.
- `crosscrease` — use the packaged CrossCrease endpoint values. The default is
  the local server at `127.0.0.1`.
- `default` — preserve the existing shadPS4 network, identity, and DNS settings.
  This is the escape hatch for another already-configured service.
- `custom` — use the address and ports under the `custom` object.

The captured NHL 17 redirector port is TCP `42230`. The game embeds this port;
the visible `redirector_tcp_port` field documents the required server listener
and makes custom deployment configuration unambiguous.

Set `online_id` to a unique 3-16 character ID for CrossCrease or custom local
identity services. Do not put passwords, bearer tokens, authorization codes, or
private keys in this file.

The launcher makes private ignored backups, applies the chosen mode, starts the
game without automating its controls, waits for it to close, and restores the
original emulator configuration and profile. CrossCrease/custom mode also loads
the visible `trust_patch_file` as a transient shadPS4 external patch. It replaces
only the server host's public CA modulus in memory; the owned game files and
shadPS4 source are never modified. Default/offline mode does not load the patch.
The emulator change underneath the routing toggle is generic hostname override
support, not a title-specific network hack.

Project operating rule: shadPS4 lifecycle actions use command tools only. No UI
automation is used for the game window, and all in-game controls remain with
the player.
