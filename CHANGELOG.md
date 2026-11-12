# Changelog

Changes to CrossCrease NHL 17 releases. Versions follow [semantic versioning](https://semver.org/); the version source of truth is `tools/release/VERSION`. See [RELEASING.md](RELEASING.md) for how releases are built.

## Unreleased

- Pad pipe: started with `SHADPS4_PAD_PIPE=true` (set by `tools/run_nhl17.ps1`), the emulator accepts pad 1 button presses on the local named pipe `\\.\pipe\shadps4-pad-<pid>` (`<button> <1|0>` per line). Input reaches the game while its window is in the background. `tools/control_nhl17.ps1` uses the pipe automatically and no longer needs `-Focus -NativeInput` with such a runtime; button names include `L1`/`R1`/`L2`/`R2`/`L3`/`R3`/`Touchpad`.

## 0.1.1 — Update test

- No gameplay or compatibility changes. This release exists to verify that the CrossCrease launcher offers and installs runtime updates (the previous version stays on disk for rollback, and player data is untouched).
- `shadPS4.exe` is rebuilt from this tag, so its built-in commit information changes.

## 0.1.0 — First test release

- First CrossCrease NHL 17 runtime and online connection packages, installed by the CrossCrease launcher.
- Based on shadPS4 0.21.0 with CrossCrease NHL 17 compatibility, performance and online work.
