# Changelog

Changes to CrossCrease NHL 17 releases. Versions follow [semantic versioning](https://semver.org/); the version source of truth is `tools/release/VERSION`. See [RELEASING.md](RELEASING.md) for how releases are built.

## Unreleased (0.3.0) — Online passwords

- `tools/run_nhl17_online.ps1` takes `-Password` and signs in with it (the CrossCrease launcher 0.7.0 passes the player's Password setting). Without it the shared developer password `local-only` is sent, which CrossCrease servers on the Internet refuse.
- A rejected sign-in shows a notification: wrong password, online ID not accepted, or refused (too many wrong passwords or a suspended account). Before, the game only stayed offline. These server warnings stay on screen for 20 seconds (other notifications 6), so they are still visible after the boot logos.
- A friend request from the friends overlay (F2) that the server refuses shows why: no player with that online ID, already friends, or refused (you blocked them, or a friend list is full). Before, the name stayed under sent requests with no reply. Blocking needs a CrossCrease server with blocking support.

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
