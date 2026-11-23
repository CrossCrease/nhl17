<!--
SPDX-FileCopyrightText: 2026 CrossCrease
SPDX-License-Identifier: GPL-2.0-or-later
-->

# CrossCrease NHL 17 on Linux (native runtime)

Status: **in development on the `linux-build` branch, not released.** Linux players currently run the
Windows runtime under Proton. This page describes the native Linux build: how it is built and packaged,
how it starts the game, and what has and has not been tested.

## Package

`crosscrease-nhl17-runtime-<version>-linux-x64.tar.gz` holds the same things as the Windows ZIP, for
Linux:

| Path | Contents |
| --- | --- |
| `runtime/shadps4` | The emulator, built from the release tag (x86-64-v3, so AVX2 is needed, as on Windows). C++ runtime linked in; it needs only glibc 2.39+ and the system's graphics, audio and input libraries. |
| `runtime/defaults/user/` | Default `config.json` and `users.json` (the same files as the Windows package). |
| `tools/crosscrease-nhl17` | The start program (Python 3, standard library only). Linux counterpart of `crosscrease-nhl17.exe`; same commands and options. |
| `patches/` | Compatibility patch for update 1.08 and the resolution patches. |
| `online/` | Public connection files for the CrossCrease server (no private keys; checked when packaging). |
| `LICENSE`, `LICENSES/`, `README.txt` | Licenses and the notice naming the source tag. |

Entries are sorted, owned by root, `0755` for the two programs and `0644` for everything else, and
dated from `SOURCE_DATE_EPOCH` (default: the source commit's time), so the same binary, checkout and
server address give a byte-identical archive. (A Windows checkout stores text files with CRLF endings, so
its archive differs from one packaged on Linux; the start program is always written with LF.)

### Requirements

- x86-64 CPU with AVX2.
- glibc 2.39 or newer: Fedora 40+, Ubuntu 24.04+, Debian 13, Arch, Bazzite, SteamOS 3.7+ (to be confirmed
  on a Steam Deck).
- A Vulkan 1.3 driver: Mesa RADV (AMD), ANV (Intel) or the NVIDIA driver.
- X11 or Wayland, and Python 3.8+ for the start program.

## Starting the game

```bash
tools/crosscrease-nhl17 play -Game ~/Games/NHL17/CUSA04326/eboot.bin -OnlineId YourOnlineId
```

`play` does exactly what `crosscrease-nhl17.exe play` does on Windows:

- uses `$XDG_DATA_HOME/CrossCrease/games/nhl17` (normally `~/.local/share/CrossCrease/games/nhl17`) as
  the player's data folder unless `-DataDirectory` names another; the emulator runs with that folder as
  its working directory, so `user/` (saves, settings, shader cache, logs) survives runtime updates;
- seeds `user/config.json` and `user/users.json` from `runtime/defaults/user/` and creates the empty
  player home folders (otherwise shadPS4 asks a "Save Migration" question);
- for that session only, points the network settings at the server in `online/server_connection.json`
  (or `-ServerAddress`), sets the online ID, and restores both files when the game closes;
- combines the compatibility patch (update only), the resolution patch (`-Resolution`) and the server
  trust patch into one patch file;
- sets the same environment (`SHADPS4_NET_HOST_OVERRIDES_JSON`, `SHADPS4_BUFFER_READBACK_KIB=128`,
  `VK_LOADER_LAYERS_DISABLE=~implicit~`, ...) and passes the password in `SHADPS4_SHADNET_PASSWORD`,
  never on a command line or in a file;
- writes `logs/online-<time>/launch.log`, `logs/online.stdout.log`, `logs/online.stderr.log` and
  `logs/online.run.json`, and a `user/log/crash.txt` when the game ends with an error the emulator
  could not report itself.

The password comes from `CROSSCREASE_SETTING_PASSWORD` (what a launcher sets) or `-Password`; in a
terminal the program asks for it. Errors are printed, and shown with `zenity` or `kdialog` when the
program was started without a terminal. `-BaseOnly`, `-Resolution`, `-FrameRate` and `rename` work as on
Windows.

## Building

The supported build runs in a container, so every build uses the same Ubuntu 24.04 base, compiler
(Clang 19 with GCC 14's C++ library, as in upstream shadPS4's Linux CI) and libraries:

```bash
git clone --recursive https://github.com/CrossCrease/nhl17.git
cd nhl17
tools/linux/build_release.sh --server-address nhl17.crosscrease.com
```

It needs `podman` or `docker`, builds the image from `tools/linux/Containerfile`, compiles into
`build-linux-container/` (`build-linux-release/` with `--container none`), writes the stripped emulator to `artifacts/linux/shadps4` and the package with
its `.sha256` file to `artifacts/release-linux/<version>/`. The online files come from the Blaze
project's player pack (`--online-directory`, default `../blaze/.../NHL17/PS4/v1.08/Player`).
`--container none` uses the host's tools instead (Ubuntu 24.04 with the packages in the Containerfile;
WSL works). `--no-package` only builds.

### In a release

1. Build the Linux emulator from the release commit: `tools/linux/build_release.sh --no-package` (in WSL
   or on a Linux machine; the source must be the same commit as the Windows build).
2. Package on Windows as usual and pass it in:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File tools\package_release.ps1 -ServerAddress nhl17.crosscrease.com -LinuxRuntime <path>\shadps4
   ```

   This adds `crosscrease-nhl17-runtime-<version>-linux-x64.tar.gz` to the release folder, a line to
   `SHA256SUMS.txt`, and a package with the id `runtime-linux-x64` to `crosscrease-nhl17-feed.json`.
   The launcher installs packages by id and its NHL 17 profile asks for `runtime`, so Windows
   launchers ignore the Linux package.

## Launcher on Linux

Not decided yet. The launcher is a Windows Forms program; it would need either a Linux port or a
Linux-aware profile (the `runtime-linux-x64` package, `.tar.gz` extraction that keeps file modes, and
`tools/crosscrease-nhl17` as the Play command). Until then players start the game with the start program
above.

## Tested and untested

Tested (2026-11-23, WSL 2 Ubuntu 24.04 on Windows 11, no GPU: Mesa llvmpipe, the CPU Vulkan driver):

- `tools/linux/build_release.sh` builds from a clean checkout of `linux-build`, both in the container
  (podman 4.9 in WSL) and with `--container none`; the
  binary needs only glibc 2.38 and `libudev`, `libuuid`, `libcap` besides the libraries SDL loads at
  run time.
- `package_linux.py` builds the package twice byte for byte the same; `package_release.ps1
  -LinuxRuntime` builds the release with both packages, checksums and feed entries; the Windows-built
  tarball keeps the executable bits and the LF start program.
- From the extracted package, `tools/crosscrease-nhl17 play` starts update 1.08 with the compatibility
  and trust patches (window title `NHL® 17 <01.08>`), the game plays the EA Sports intro and the
  in-engine attract sequence (about 20 fps on llvmpipe), and a Cross press through the pad socket
  opens the title screen ("PRESS ⊗"); a second press opens the first menu (the EULA of a new profile).
  It runs 10+ minutes without errors, and closing
  the start program (SIGTERM) closes the game and restores `config.json` / `users.json`. A crash writes
  `user/log/crash.txt` (`kind=assert`).
- llvmpipe offers only 2 GiB of sparse address space, but the emulator's GPU memory arenas are 4 GiB,
  so on llvmpipe the shipped binary stops after about 10 s with `ErrorOutOfDeviceMemory`. The runs
  above used a test build with 2 GiB arenas (not committed). GPU drivers report far larger limits;
  this is the same on Windows and not a Linux problem, but drivers without large sparse buffers
  (llvmpipe, Intel hasvk) cannot run the game.

Not tested yet:

- A real GPU: AMD/RADV, Intel/ANV, NVIDIA, and frame rates (the player who reported 60 fps natively
  used their own v0.4.10 build on an RX 7600).
- Steam Deck / SteamOS (glibc and Python versions, Gaming Mode), Bazzite, Wayland without XWayland.
- Online sign-in, matches and `rename` against a server, controllers (SDL gamepad input), audio.

Known differences from Windows:

- The developer pad pipe (`SHADPS4_PAD_PIPE=true`, set by the start program) is a Unix socket on Linux,
  `$XDG_RUNTIME_DIR/shadps4-pad-<pid>.sock` (only the user can open it), with the same line protocol
  as the Windows named pipe (`cross 1`, `cross 0`, ... answered with `OK`). `logs/online.pid` names the
  game process. `tools/control_nhl17.ps1` is Windows-only; on Linux any program that writes lines to
  the socket works.
- A crash the emulator catches is reported through its assert path (`kind=assert` in `crash.txt`); on
  Windows unhandled exceptions are reported as `kind=exception`.
- Visual glitches seen on AMD with RADV happen in native builds too; they are a separate driver problem
  and not addressed here.
