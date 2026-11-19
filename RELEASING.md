# Releasing CrossCrease NHL 17

CrossCrease players never build this project. The CrossCrease launcher downloads the NHL 17 runtime, which includes the online connection files, from this repository's GitHub releases, verifies them, and installs them per user. This document describes how those releases are produced.

## What gets published

`tools\package_release.ps1` produces `artifacts\release\<version>\`:

| File | Contents |
| --- | --- |
| `crosscrease-nhl17-runtime-<version>-windows-x64.zip` | `runtime\shadPS4.exe` and its Visual C++ runtime DLLs, `runtime\defaults\user\` (default `config.json` and `users.json`), `tools\crosscrease-nhl17.exe` (built from `tools\start\CrossCreaseNhl17.cs` by `tools\build_start_program.ps1`), `tools\run_nhl17.ps1`, `tools\run_nhl17_online.ps1`, `tools\rename_online_id.ps1`, `LICENSE`, `LICENSES\`, a `README.txt` that names the exact source tag, and `online\`: the public client files from the Blaze project's NHL 17 player pack (`server_connection.json`, `requirements.json`, `README.md`, and `generated\` with the server's public CA certificate, trust patch and trust manifest). |
| `crosscrease-nhl17-feed.json` | The list the launcher reads: release version, minimum launcher version, and each package's version, size and SHA-256. |
| `SHA256SUMS.txt` | Checksum of the ZIP file. |

Packages are built from an allowlist. The script never includes game files, `game-UPDATE`, console firmware, `keys.json`, saves, caches, logs, or the developer's `runtime\user` folder. It refuses to package if any of the online files contains a PEM private key or if the trust manifest reports one.

## Steps

1. Build the runtime: `powershell -NoProfile -ExecutionPolicy Bypass -File tools\build_windows.ps1`, then copy the resulting `shadPS4.exe` into `runtime\` as usual.
2. Set the version in `tools\release\VERSION` and add an entry to `CHANGELOG.md` (semantic versioning; use `-rc.1` etc. for prereleases).
3. If the default player configuration changed, update `tools\release\defaults\user\config.json` / `users.json`. They must not contain personal addresses, online IDs or credentials.
4. Package:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File tools\package_release.ps1 -ServerAddress nhl17.crosscrease.com
   ```

   Public releases use `nhl17.crosscrease.com` as the default server. Each CrossCrease game server has its own host name, `<game id>.crosscrease.com`, so one game's server can move to another machine by changing one DNS record. Releases up to 0.4.0 use `crosscrease.com`, so that name must keep pointing at the NHL 17 server while those releases are in use.

   Options: `-ServerAddress <host>` sets the default server in `online\server_connection.json` (without it the package keeps the developer default `127.0.0.1`; the script warns when the default is loopback or private, or does not resolve), `-MinimumLauncher <version>` sets the oldest launcher that may install this release, and `-OnlineDirectory` / `-RuntimeDirectory` select other inputs.
5. Test it with the launcher before publishing: put the feed into `%LOCALAPPDATA%\CrossCrease\feeds.json` (see the launcher README), then download, play and update through the launcher.
   Release order when the launcher profile depends on a new runtime (launcher 0.6.0 requires runtime 0.2.0 through `minimum_versions`): publish the NHL 17 release first, then the launcher. The feed's `minimum_launcher` makes older launchers ask for a launcher update before they install it.
6. Tag the source as `v<version>`, create the GitHub release `v<version>` in `CrossCrease/nhl17`, and upload all three files unchanged. Mark prereleases as prereleases, because the launcher's feed URL (`releases/latest/download/crosscrease-nhl17-feed.json`) only follows full releases.

The release tag must contain the exact source of the published `shadPS4.exe`. shadPS4 is GPL-2.0-or-later, and the runtime package's `README.txt` points players to that tag.

## How the installed runtime runs

The launcher starts `tools\run_nhl17.ps1` from the installed package with `-DataDirectory %LOCALAPPDATA%\CrossCrease\games\nhl17`. shadPS4 keeps its `user` folder in its working directory, so the script starts the emulator from that data folder. Saves, settings and shader caches therefore survive runtime updates. On first start the script copies the default `config.json` / `users.json` and creates the empty player home folders. Without those folders, shadPS4 would ask a "Save Migration" question in a window nobody can see. Without `-DataDirectory`, the script behaves exactly as before for the developer workspace.

The launcher's **Play** runs `tools\crosscrease-nhl17.exe play` (launcher 0.9.1+; launchers up to 0.9.0 run `tools\run_nhl17_online.ps1`, which does the same) with the player's online ID and the server (`default` uses the address in `online\server_connection.json`). It uses the runtime package's own `online\` folder. With `-BaseOnly` (the game update turned off) it starts the base game (1.00) online the same way; the server trust patch covers both versions. Like `run_nhl17_server.ps1`, it changes the player's network configuration only for that session and restores it when the game closes. The program needs no PowerShell, so it also runs under Wine/Proton, and it writes `logs\online-<time>\launch.log` in the data folder. A launcher whose NHL 17 profile needs a newer runtime (`minimum_versions`) must be released after that runtime.
