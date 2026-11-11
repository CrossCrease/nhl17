# Releasing CrossCrease NHL 17

CrossCrease players never build this project. The CrossCrease launcher downloads the NHL 17 runtime and the online connection files from this repository's GitHub releases, verifies them, and installs them per user. This document describes how those releases are produced.

## What gets published

`tools\package_release.ps1` produces `artifacts\release\<version>\`:

| File | Contents |
| --- | --- |
| `crosscrease-nhl17-runtime-<version>-windows-x64.zip` | `runtime\shadPS4.exe` and its Visual C++ runtime DLLs, `runtime\defaults\user\` (default `config.json` and `users.json`), `tools\run_nhl17.ps1`, `tools\run_nhl17_online.ps1`, `LICENSE`, `LICENSES\`, and a `README.txt` that names the exact source tag. |
| `crosscrease-nhl17-online-<version>.zip` | Public client files from the Blaze project's NHL 17 player pack: `server_connection.json`, `requirements.json`, `README.md`, and `generated\` (the server's public CA certificate, trust patch, and trust manifest). |
| `crosscrease-nhl17-feed.json` | The list the launcher reads: release version, minimum launcher version, and each package's version, size and SHA-256. |
| `SHA256SUMS.txt` | Checksums of both ZIP files. |

Packages are built from an allowlist. The script never includes game files, `game-UPDATE`, console firmware, `keys.json`, saves, caches, logs, or the developer's `runtime\user` folder. It refuses to package the online files if any of them contains a PEM private key or if the trust manifest reports one.

## Steps

1. Build the runtime: `powershell -NoProfile -ExecutionPolicy Bypass -File tools\build_windows.ps1`, then copy the resulting `shadPS4.exe` into `runtime\` as usual.
2. Set the version in `tools\release\VERSION` (semantic versioning; use `-rc.1` etc. for prereleases).
3. If the default player configuration changed, update `tools\release\defaults\user\config.json` / `users.json`. They must not contain personal addresses, online IDs or credentials.
4. Package:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File tools\package_release.ps1
   ```

   Options: `-ServerAddress <host>` sets the default server in the online package, `-MinimumLauncher <version>` sets the oldest launcher that may install this release, and `-OnlineDirectory` / `-RuntimeDirectory` select other inputs.
5. Test it with the launcher before publishing: put the feed into `%LOCALAPPDATA%\CrossCrease\feeds.json` (see the launcher README), then download, play and update through the launcher.
6. Tag the source as `v<version>`, create the GitHub release `v<version>` in `CrossCrease/nhl17`, and upload all four files unchanged. Mark prereleases as prereleases, because the launcher's feed URL (`releases/latest/download/crosscrease-nhl17-feed.json`) only follows full releases.

The release tag must contain the exact source of the published `shadPS4.exe`. shadPS4 is GPL-2.0-or-later, and the runtime package's `README.txt` points players to that tag.

## How the installed runtime runs

The launcher starts `tools\run_nhl17.ps1` from the installed package with `-DataDirectory %LOCALAPPDATA%\CrossCrease\games\nhl17`. shadPS4 keeps its `user` folder in its working directory, so the script starts the emulator from that data folder. Saves, settings and shader caches therefore survive runtime updates. On first start the script copies the default `config.json` / `users.json` and creates the empty player home folders. Without those folders, shadPS4 would ask a "Save Migration" question in a window nobody can see. Without `-DataDirectory`, the script behaves exactly as before for the developer workspace.

**Play online** runs `tools\run_nhl17_online.ps1` with the installed online package, the player's online ID and the server (`default` uses the address in `server_connection.json`). Like `run_nhl17_server.ps1`, it changes the player's network configuration only for that session and restores it when the game closes.
