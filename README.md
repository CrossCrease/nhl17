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
4. Press **Play**. **Play online** additionally downloads the online connection files and asks for your online ID.

### What you need

- Your own copy of NHL 17 for PS4 (CUSA04326 or CUSA04330), dumped from your own console, with update **1.08**. Put the update next to the game folder, in a folder with the same name plus `-UPDATE`.
- Windows 10/11 x64 and a Vulkan-capable GPU.

CrossCrease never includes or downloads game files, console firmware, keys or saves.

## Repository layout

| Path | Contents |
| --- | --- |
| `src/`, `externals/`, `CMakeLists.txt` | The emulator source (shadPS4 plus CrossCrease changes). |
| `tools/run_nhl17.ps1` | Supported way to start the game (configuration, environment, separate player data folder). |
| `tools/run_nhl17_online.ps1`, `tools/run_nhl17_server.ps1` | Start the game connected to a CrossCrease server. See [SERVER-CONNECTION.md](SERVER-CONNECTION.md). |
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

Copy the resulting `shadPS4.exe` and the Visual C++ runtime DLLs into `runtime\`, put your game in `game\` (and the update in `game-UPDATE\`), then run `tools\run_nhl17.ps1`. For other platforms see the upstream build documents in [documents/](documents/).

## Releases and versioning

Releases follow semantic versioning. The version lives in `tools/release/VERSION`, and each GitHub release `v<version>` carries the runtime package, the online package, the launcher feed and checksums. The tag contains the exact source of the released `shadPS4.exe`. See [RELEASING.md](RELEASING.md).

## License and attribution

CrossCrease NHL 17 is licensed under the **GNU General Public License v2.0 or later**, like shadPS4; see [LICENSE](LICENSE) and [LICENSES/](LICENSES/). It is built on the work of the shadPS4 team and contributors; full credits are in [README.shadPS4.md](README.shadPS4.md) and the Git history. Bundled third-party components keep their own licenses (see `externals/` and `REUSE.toml`).

NHL and the NHL Shield are trademarks of the National Hockey League. EA SPORTS and NHL 17 are trademarks of Electronic Arts Inc. PlayStation is a trademark of Sony Interactive Entertainment. CrossCrease is not affiliated with or endorsed by any of them.
