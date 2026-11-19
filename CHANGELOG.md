# Changelog

Changes to CrossCrease NHL 17 releases. Versions follow [semantic versioning](https://semver.org/); the version source of truth is `tools/release/VERSION`. See [RELEASING.md](RELEASING.md) for how releases are built.

## Unreleased

- Linux (Proton or Wine): NHL 17 no longer crashes a few seconds after the window opens
  (report #1). Wine's C runtime dropped the read buffer of game files whenever the emulator
  checked a file's size, so the next read skipped ahead, the game misread an archive table
  and never mounted `boot.big`. Read-only files are no longer flushed.
- Graphics: textures the game paints at run time and later reads with more mip levels (for
  example EASHL club jerseys) keep their content. Before, the texture cache threw the painted
  image away, so some clients showed a club's jersey black with no trim or crest, and each
  client could show different uniforms in the same match.
- Pad pipe (developer tests): raw trigger commands `l2axis|r2axis <0-255>` (trigger value
  only) and `l2button|r2button <0|1>` (digital bit only), next to `<button> <0|1>`.
- Developer tests: `SHADPS4_LOCAL_IP` (opt-in environment variable) makes an emulator instance
  report that address as its own. Several instances on one PC can then each use the game's
  fixed P2P port, for example 127.0.0.1/.2/.3 with a server on loopback. Windows does not send
  from a 127.x address to a LAN address, so all parts must be on loopback. Normal play is
  unaffected.
- Network capture (`SHADPS4_NET_CAPTURE_DIR`, developer only): also records `bind` (local
  address and port) and the socket type in each `socket` event.
- Crash logs: an unhandled guest exception also logs the registers and the stack values that
  point into the guest module range, so the game call chain can be reconstructed.

## 0.4.6 — Starts under Linux (Proton/Wine)

- Linux (Proton or Wine): the runtime package has `tools\crosscrease-nhl17.exe`, which starts
  the game online (`play`) and renames an online ID (`rename`) exactly like
  `run_nhl17_online.ps1` and `rename_online_id.ps1`, without Windows PowerShell. Proton and
  Wine do not provide PowerShell, so with the scripts a window flashed and the game never
  started. Each start writes `logs\online-<time>\launch.log` in the player's data folder
  (with the Wine version when there is one), and a failed start shows a message naming it.
  The CrossCrease launcher runs it from 0.9.1; older launchers keep using the scripts, which
  are still included.
- The emulator (`shadPS4.exe`) and the online connection files are the same as in 0.4.5.

## 0.4.5 — Online Team Play hub stats

- Online Team Play: the hub's Ranked OTP Quick Match tile shows the player's games, rank and
  goals-assists-points (with the placeholder avatar, as on the Online Versus hub). The
  game's own OTP hub scripts ignored the stats once they arrived and waited forever for an
  avatar that cannot be downloaded, so the tile stayed empty. The compatibility patch
  (update 1.08) corrects those two script lines in `boot.big` the way the Online Versus hub
  already does it, as the game reads the file. The game files on disk are not changed.
  Needs a CrossCrease NHL 17 server with OTP stats (server 0.3.7 or newer).
- Patches: new patch line type `chunkzip_text` (`File`, `Offset`, `Find`, `Replace`). It
  replaces text of the same length inside a compressed record of a game file while the game
  reads it. The record is rebuilt from the player's own file, so no game data ships with the patch.
- Nothing else changed since 0.4.4.

## 0.4.4 — Crash reports

- Crash file for crash reports: when the emulator catches a crash (an unhandled exception or a
  failed assertion), it writes `user\log\crash.txt` next to `shad_log.txt` with a few
  `key=value` lines: `kind` (`exception` or `assert`), `code`, `address` (the exception address,
  or `file:line` of the assertion), `time` and `version`. `run_nhl17_online.ps1` writes one with
  `kind=exit` when the game ends with a non-zero exit code and the emulator wrote none. The
  CrossCrease launcher (0.9.0 and newer) finds the file and offers to send a crash report to
  the CrossCrease team. Nothing else changed since 0.4.3.

## 0.4.3 — Encrypted game services

- PSN WebAPI requests (profile, entitlements, sessions and invitations, which carry the
  player's sign-in token) go to the server over HTTPS (`psn_webapi_tls_port`, 31319, in
  `online\server_connection.json`). The emulator accepts only a certificate issued by the
  CrossCrease CA in `online\generated\`, whatever the game asks for and even with
  `Network.disable_https`. There is no fallback to plain HTTP.
- Blaze (matchmaking, stats, EASHL, HUT) is encrypted too. That needs no runtime change: a
  CrossCrease server 0.3.5 or newer tells the game to use TLS, and the game checks the server
  against the CA in the trust patch every release already loads, so older runtimes get it as
  well.
- Needs a CrossCrease server 0.3.5 or newer. Older servers have no HTTPS WebAPI, so online
  features that use it fail against them.

## 0.4.2 — Encrypted sign-in

- Signing in to the CrossCrease server is encrypted (TLS). Before, the online ID, password and
  sign-in token crossed the network in plain text. The emulator's shadNet connection, its
  server check and `tools/rename_online_id.ps1` connect to the server's encrypted sign-in port
  (`identity_tls_port`, 31318, in `online\server_connection.json`) and accept only a server
  certificate issued by the CrossCrease CA that ships in `online\generated\`; the Windows
  certificate store is not used. There is no fallback to the unencrypted port.
- New emulator setting `Network.shadnet_ca_certificate` (CA certificate file; empty keeps the
  unencrypted connection). `tools/run_nhl17_online.ps1` sets it for the session.
- Needs a CrossCrease server 0.3.4 or newer. Older servers have no encrypted sign-in, so this
  runtime cannot sign in to them.

## 0.4.1 — Own server name

- The default server is `nhl17.crosscrease.com` (before: `crosscrease.com`). Both names reach the same server, so players on older releases keep connecting and nobody has to change a setting. A custom Server setting in the launcher is not affected.
- `tools/package_release.ps1` warns when the default server name does not resolve.
- The emulator is unchanged from 0.4.0.

## 0.4.0 — Base game online

- The base game (1.00, **Start with the game update** turned off) now plays online. Before, `tools/run_nhl17_online.ps1 -BaseOnly` started it offline. Base-game players are matched only with other base-game players, and update players only with update players. Online play uses the roster built into each game version; nothing is downloaded. Needs a CrossCrease server 0.3.0 or newer (older servers sign the base game in, but Ranked Quick Match stops at "Unable to download rosters").
- The base game cannot load a profile saved by the update, and its "Create New Profile" replaces it. When an update is installed, `tools/run_nhl17.ps1 -BaseOnly` with a data folder first copies the player's saves to `<data folder>\save-backups\<date>-before-base-game\` (newest 5 kept).
- `tools/run_nhl17_online.ps1` has `-AllowMultipleInstances` for developer tests with a second player on one PC (each needs its own runtime folder).
- Verified on one PC against a local server: two base-game players matched in Ranked Quick Match and reached live gameplay. Not verified: a base-game player against a CrossCrease server on the Internet.

## 0.3.0 — Online passwords, renaming

- The default server is `crosscrease.com`. Players can leave the launcher's Server setting at `default`.
- Requires CrossCrease launcher 0.7.0 or newer; older launchers ask for a launcher update first.

- `tools/run_nhl17_online.ps1` signs in with the player's password. The CrossCrease launcher 0.7.0 passes its Password setting in the `CROSSCREASE_SETTING_PASSWORD` environment variable; developers can use `-Password`. Without either, the shared developer password `local-only` is sent, which CrossCrease servers on the Internet refuse.
- The password is never written to disk: the script leaves `shadnet_password` in `users.json` empty and hands the password to the emulator in `SHADPS4_SHADNET_PASSWORD`, which the emulator uses for the primary user and then removes from its environment.
- A rejected sign-in shows a notification: wrong password, online ID not accepted, or refused (too many wrong passwords or a suspended account). Before, the game only stayed offline. These server warnings stay on screen for 20 seconds (other notifications 6), so they are still visible after the boot logos.
- A friend request from the friends overlay (F2) that the server refuses shows why: no player with that online ID, already friends, or refused (you blocked them, or a friend list is full). Before, the name stayed under sent requests with no reply. Blocking needs a CrossCrease server with blocking support.
- New `tools/rename_online_id.ps1` renames the player's online ID on the server and keeps the account (HUT, stats, friends, password). The CrossCrease launcher runs it when a player changes their Online ID in Player settings and chooses to rename. The password comes from `CROSSCREASE_SETTING_PASSWORD` (developers can use `-Password`). Needs a CrossCrease server with renaming; older servers give a clear message. The runtime package includes it.

## 0.2.0 — Online files included, background pad input, invite fix

- The runtime package now includes the online connection files in `online\`. The separate `crosscrease-nhl17-online-<version>.zip` is no longer published.
- `tools/run_nhl17_online.ps1` uses the bundled `online\` folder by default and starts the base game offline with `-BaseOnly`. The CrossCrease launcher's single **Play** button runs it.
- The feed requires CrossCrease launcher 0.6.0 or newer.
- Pad pipe: started with `SHADPS4_PAD_PIPE=true` (set by `tools/run_nhl17.ps1`), the emulator accepts pad 1 button presses on the local named pipe `\\.\pipe\shadps4-pad-<pid>` (`<button> <1|0>` per line). Input reaches the game while its window is in the background. `tools/control_nhl17.ps1` uses the pipe automatically and no longer needs `-Focus -NativeInput` with such a runtime; button names include `L1`/`R1`/`L2`/`R2`/`L3`/`R3`/`Touchpad`.
- Pad pipe buttons also navigate the emulator's own overlays (for example the session invitation dialog and prompt), so they can be operated in the background too. A new `share` pipe button (`control_nhl17.ps1 -Keys Share`, hold it with `-HoldMs 800`) reaches only the overlays, never the game; holding it focuses an invitation prompt.
- Invitation dialog: the friend list and the pending-invitation list can be reached with the controller's D-pad (previously gamepad navigation skipped over them, so a controller user could not tick a friend).
- Accepting a HUT Play a Friend invitation from the main menu no longer stays on "Connecting". New compatibility patch `patches/nhl17-compat.xml` (update 1.08 only, applied in memory; game files are not changed). The cause is in the game's own menu logic: on the main menu, the invite flow's online check sends its login request before the invite flow clears all pending menu requests, so the login reply was thrown away. The patch keeps the replies of requests made after a game mode was left (at most 8). `tools/run_nhl17.ps1` combines this patch with the server trust patch, because the emulator accepts one patch file; the runtime package now includes `patches\`. Verified with two players on one PC: a main-menu accept reaches Select Team on both, and accepting inside HUT still works.

## 0.1.1 — Update test

- No gameplay or compatibility changes. This release exists to verify that the CrossCrease launcher offers and installs runtime updates (the previous version stays on disk for rollback, and player data is untouched).
- `shadPS4.exe` is rebuilt from this tag, so its built-in commit information changes.

## 0.1.0 — First test release

- First CrossCrease NHL 17 runtime and online connection packages, installed by the CrossCrease launcher.
- Based on shadPS4 0.21.0 with CrossCrease NHL 17 compatibility, performance and online work.
