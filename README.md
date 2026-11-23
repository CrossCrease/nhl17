<!--
SPDX-FileCopyrightText: 2026 CrossCrease
SPDX-FileCopyrightText: 2024 shadPS4 Emulator Project
SPDX-License-Identifier: GPL-2.0-or-later
-->

# CrossCrease NHL 17

CrossCrease NHL 17 is the runtime that lets you play **NHL 17 (PlayStation 4)** on Windows as part of [CrossCrease](https://github.com/CrossCrease). It is a fork of the [shadPS4](https://github.com/shadps4-emu/shadPS4) emulator with compatibility, performance and online work for NHL 17, plus the scripts that start the game the supported way.

This is **not** an official shadPS4 release. Please report problems with this build to CrossCrease, not to the shadPS4 project. The original shadPS4 README is kept in [README.shadPS4.md](README.shadPS4.md).

## Playing

Most players never build this project. They use the [CrossCrease launcher](https://github.com/CrossCrease/launcher):

1. Install the launcher and select **NHL 17**.
2. Press **Download**. The launcher installs the latest runtime from this repository's [releases](https://github.com/CrossCrease/nhl17/releases) and checks its checksum.
3. Open **Game settings** and select your game's `eboot.bin` (or its folder).
4. Press **Play**. The first time, the launcher asks for your online ID (the server can stay `default`). The game then connects to the CrossCrease server. With **Start with the game update** turned off, the base game (1.00) starts online too; base-game players play online against other base-game players only.

### What you need

- Your own copy of NHL 17 for PS4 (CUSA04326 or CUSA04330), dumped from your own console, with update **1.08**. Put the update next to the game folder, in a folder with the same name plus `-UPDATE`.
- Windows 10/11 x64 and a Vulkan-capable GPU.

On Linux, players currently run the Windows runtime under Proton. A native Linux runtime is in development; see [documents/linux-runtime.md](documents/linux-runtime.md).

CrossCrease never includes or downloads game files, console firmware, keys or saves.

## Repository layout

| Path | Contents |
| --- | --- |
| `src/`, `externals/`, `CMakeLists.txt` | The emulator source (shadPS4 plus CrossCrease changes). |
| `tools/run_nhl17.ps1` | Supported way to start the game (configuration, environment, separate player data folder). Applies `patches/nhl17-compat.xml` together with any `-PatchFile`. |
| `patches/nhl17-compat.xml` | CrossCrease compatibility patch for update 1.08, applied in memory (addresses are ELF virtual address + 0x400000, the shadPS4 convention). Currently fixes the main-menu invitation accept that stayed on "Connecting", and the empty Ranked OTP Quick Match tile on the Online Team Play hub (a `chunkzip_text` line that corrects one hub-script line in `boot.big` as the game reads it; the file on disk is not changed). The file's comments explain each change. |
| `patches/nhl17-resolution-720p.xml`, `patches/nhl17-resolution-1080p.xml`, `patches/nhl17-resolution-1440p.xml` | Optional render resolution patches for the base game (1.00) and the update (1.08), applied in memory when `-Resolution 720p`, `1080p` or `1440p` is chosen. The game normally draws at 1600x900 and scales to 1080p; 720p (1280x720) needs about a third less graphics work, for slower graphics cards. The files' comments explain the patched values. |
| `tools/start/CrossCreaseNhl17.cs`, `tools/build_start_program.ps1` | `crosscrease-nhl17.exe`, what the launcher (0.9.1+) runs: `play` starts the game connected to a CrossCrease server using the runtime package's `online\` files (the base game with `-BaseOnly`; `-Resolution 720p\|900p\|1080p\|1440p` and `-FrameRate 60\|120` for the graphics options; 120 runs offline gameplay at up to 120 fps, online matches stay near 60) and writes `<data>\logs\online-<time>\launch.log`; `rename` renames an online ID. It needs no PowerShell, so it also runs under Wine/Proton. It does the same as the scripts below; keep them in step. |
| `tools/run_nhl17_online.ps1`, `tools/rename_online_id.ps1` | The same as `crosscrease-nhl17.exe play` / `rename`, as PowerShell scripts: what launchers up to 0.9.0 run, and handy for developer tests. |
| `tools/run_nhl17_server.ps1` | Developer toggle between offline, CrossCrease and custom servers. See [SERVER-CONNECTION.md](SERVER-CONNECTION.md). |
| `tools/control_nhl17.ps1`, `tools/capture_nhl17_window.ps1` | Developer automation: press pad buttons and take window screenshots. With a game started by `tools/run_nhl17.ps1` (which sets `SHADPS4_PAD_PIPE=true`), buttons go through the emulator's local pad pipe `\\.\pipe\shadps4-pad-<pid>` (on Linux the Unix socket `$XDG_RUNTIME_DIR/shadps4-pad-<pid>.sock`), so the game window does not need focus; screenshots work while it is covered but not minimised. |
| `tools/linux/` | Native Linux runtime (in development): `build_release.sh` and `Containerfile` build `shadps4` reproducibly, `package_linux.py` builds the `linux-x64` package, and `crosscrease-nhl17` is the Linux start program (same `play` and `rename` as `crosscrease-nhl17.exe`). See [documents/linux-runtime.md](documents/linux-runtime.md). |
| `tools/package_release.ps1`, `tools/release/` | Build the downloadable release packages. See [RELEASING.md](RELEASING.md). |
| `tools/hardware/` | Hardware tests used to verify emulation changes. |
| `tools/*.py`, other `tools/*.ps1` | Developer analysis and capture tools. |
| `ONLINE-MODES.md`, `ONLINE-RESEARCH.md` | Status and research notes for NHL 17 online modes. |

Local-only folders such as `game/`, `game-UPDATE/`, `runtime/`, `build*/` and `artifacts/` are ignored by Git and must never be committed.

## Building

On Windows with Visual Studio Build Tools (C++), LLVM (`clang-cl`), CMake, Ninja and Python:

```powershell
git clone --recursive https://github.com/CrossCrease/nhl17.git
cd nhl17
powershell -NoProfile -ExecutionPolicy Bypass -File tools\build_windows.ps1
```

Copy the resulting `shadPS4.exe` and the Visual C++ runtime DLLs into `runtime\`, put your game in `game\` (and the update in `game-UPDATE\`), then run `tools\run_nhl17.ps1`. On Linux, `tools/linux/build_release.sh` builds and packages the native runtime in a container; see [documents/linux-runtime.md](documents/linux-runtime.md). For other platforms see the upstream build documents in [documents/](documents/).

## Releases and versioning

Releases follow semantic versioning. The version lives in `tools/release/VERSION`, and each GitHub release `v<version>` carries the runtime package (which includes the online connection files), the launcher feed and checksums. The tag contains the exact source of the released `shadPS4.exe`. See [RELEASING.md](RELEASING.md).

## License and attribution

CrossCrease NHL 17 is licensed under the **GNU General Public License v2.0 or later**, like shadPS4; see [LICENSE](LICENSE) and [LICENSES/](LICENSES/). It is built on the work of the shadPS4 team and contributors; full credits are in [README.shadPS4.md](README.shadPS4.md) and the Git history. Bundled third-party components keep their own licenses (see `externals/` and `REUSE.toml`).

NHL and the NHL Shield are trademarks of the National Hockey League. EA SPORTS and NHL 17 are trademarks of Electronic Arts Inc. PlayStation is a trademark of Sony Interactive Entertainment. CrossCrease is not affiliated with or endorsed by any of them.
