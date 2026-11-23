#!/usr/bin/env python3
# Builds the CrossCrease NHL 17 Linux runtime package from a Linux shadps4 binary:
#
#   python3 tools/linux/package_linux.py --runtime <shadps4> --online-directory <Player folder> \
#       [--server-address nhl17.crosscrease.com] [--output-directory <folder>]
#
# Output (<output>/): crosscrease-nhl17-runtime-<version>-linux-x64.tar.gz and a .sha256 file next to it.
# tools\package_release.ps1 calls this with -LinuxRuntime and lists the package in the release feed;
# tools/linux/build_release.sh calls it for a Linux-only build.
#
# The package is built from an allowlist, like the Windows one: no game files, firmware, keys, saves,
# caches, logs or user folders. The archive is reproducible: sorted entries, fixed owner and modes, and
# timestamps from SOURCE_DATE_EPOCH (default: the time of the source commit).
# Needs only Python 3.8+ and its standard library, on Linux or Windows.
import argparse
import gzip
import hashlib
import io
import json
import os
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
ONLINE_FILES = ["server_connection.json", "requirements.json", "README.md", "generated/nhl17-crosscrease-ca.xml",
                "generated/crosscrease-ca-cert.pem", "generated/trust-manifest.json"]
PATCHES = ["nhl17-compat.xml", "nhl17-resolution-720p.xml", "nhl17-resolution-1080p.xml", "nhl17-resolution-1440p.xml"]
EXECUTABLES = {"runtime/shadps4", "tools/crosscrease-nhl17"}
PRIVATE_KEY = re.compile(rb"-----BEGIN ([A-Z]+ )?PRIVATE KEY-----")

NOTICE = """CrossCrease NHL 17 runtime {version} for Linux (x86-64)

This package is part of CrossCrease. It contains a native Linux build of shadPS4
(https://github.com/shadps4-emu/shadPS4) with CrossCrease changes for NHL 17. It is not an
official shadPS4 release.

shadPS4 is licensed under the GNU General Public License v2.0 or later; see LICENSE and LICENSES.
The complete corresponding source code for this build is available at:
{source}

Start the game (Python 3 is needed for the start program):

  tools/crosscrease-nhl17 play -Game /path/to/CUSA04326/eboot.bin -OnlineId YourOnlineId

It asks for your CrossCrease password, signs in to the server in online/server_connection.json and
keeps your saves, settings and logs in ~/.local/share/CrossCrease/games/nhl17 (pass -DataDirectory to
use another folder). Options: -BaseOnly (base game 1.00), -Resolution 720p|900p|1080p|1440p,
-FrameRate 60|120, -ServerAddress <host>. Rename your online ID with:

  tools/crosscrease-nhl17 rename -OnlineId OldId -NewOnlineId NewId

Requirements: x86-64 CPU with AVX2, glibc 2.39 or newer (for example Fedora 40+, Ubuntu 24.04+,
Bazzite, SteamOS 3.7+), a Vulkan 1.3 driver (Mesa RADV, ANV or NVIDIA), and X11 or Wayland.

The online/ folder holds the public client files for connecting to a CrossCrease server (server
address, the server's public CA certificate and the in-memory trust patch). They come from the
CrossCrease Blaze server project and contain no private keys.

The patches/ folder holds the CrossCrease compatibility patch for NHL 17 update 1.08 and the
optional render resolution patches (720p, 1080p, 1440p), applied in memory when the game starts. They
change a few bytes of game code; the game files are not modified.

No game files, console firmware, keys or saves are included. You need your own copy of NHL 17.
"""


def fail(message):
    print("package_linux: " + message, file=sys.stderr)
    sys.exit(1)


def source_date_epoch():
    if os.environ.get("SOURCE_DATE_EPOCH"):
        return int(os.environ["SOURCE_DATE_EPOCH"])
    try:
        return int(subprocess.check_output(["git", "-C", ROOT, "log", "-1", "--format=%ct"], text=True).strip())
    except (OSError, subprocess.CalledProcessError, ValueError):
        return 0


def copy_listed(source_root, files, target_root):
    for name in files:
        source = os.path.join(source_root, *name.split("/"))
        if not os.path.isfile(source):
            fail("Missing release file: " + source)
        target = os.path.join(target_root, *name.split("/"))
        os.makedirs(os.path.dirname(target), exist_ok=True)
        shutil.copyfile(source, target)


def is_elf_x86_64(path):
    with open(path, "rb") as file:
        header = file.read(20)
    return header[:4] == b"\x7fELF" and header[4] == 2 and header[18:20] == b"\x3e\x00"


def stage_package(stage, runtime, online, server_address, version, source_url):
    if not os.path.isfile(runtime) or not is_elf_x86_64(runtime):
        fail("Not a Linux x86-64 executable: " + runtime)
    os.makedirs(os.path.join(stage, "runtime"))
    shutil.copyfile(runtime, os.path.join(stage, "runtime", "shadps4"))
    copy_listed(os.path.join(ROOT, "tools", "release", "defaults", "user"), ["config.json", "users.json"],
                os.path.join(stage, "runtime", "defaults", "user"))
    copy_listed(os.path.join(ROOT, "tools", "linux"), ["crosscrease-nhl17"], os.path.join(stage, "tools"))
    start_program = os.path.join(stage, "tools", "crosscrease-nhl17")
    with open(start_program, "rb") as file:
        content = file.read().replace(b"\r\n", b"\n")
    with open(start_program, "wb") as file:
        file.write(content)
    copy_listed(os.path.join(ROOT, "patches"), PATCHES, os.path.join(stage, "patches"))
    copy_listed(ROOT, ["LICENSE"], stage)
    shutil.copytree(os.path.join(ROOT, "LICENSES"), os.path.join(stage, "LICENSES"))
    with open(os.path.join(stage, "README.txt"), "w", encoding="ascii", newline="\n") as file:
        file.write(NOTICE.format(version=version, source=source_url))

    # Online connection files (public files only; requirements.json lists what must never ship).
    online_stage = os.path.join(stage, "online")
    copy_listed(online, ONLINE_FILES, online_stage)
    with open(os.path.join(online_stage, "generated", "trust-manifest.json"), encoding="utf-8-sig") as file:
        if json.load(file).get("contains_private_key"):
            fail("The trust manifest says it contains a private key; refusing to package it.")
    for folder, _, files in os.walk(online_stage):
        for name in files:
            with open(os.path.join(folder, name), "rb") as file:
                if PRIVATE_KEY.search(file.read()):
                    fail("A private key was found in the online connection files.")
    connection_file = os.path.join(online_stage, "server_connection.json")
    with open(connection_file, encoding="utf-8-sig") as file:
        connection = json.load(file)
    if server_address:
        if not re.fullmatch(r"[a-zA-Z0-9.:-]+", server_address):
            fail("Invalid server address: " + server_address)
        connection["server_address"] = server_address
        with open(connection_file, "w", encoding="ascii", newline="\n") as file:
            json.dump(connection, file, indent=4)
    default_server = str(connection.get("server_address", ""))
    if re.match(r"^(localhost|127\.|10\.|192\.168\.|172\.(1[6-9]|2[0-9]|3[01])\.)", default_server):
        print("package_linux: warning: the default server is " + default_server + ", which only works on the "
              "packaging machine or its LAN. Pass --server-address <public host name> for a public release.", file=sys.stderr)


def write_tarball(stage, path, mtime, top):
    entries = []
    for folder, folders, files in os.walk(stage):
        folders.sort()
        relative = os.path.relpath(folder, stage).replace(os.sep, "/")
        if relative != ".":
            entries.append((relative, None))
        for name in sorted(files):
            entries.append((relative + "/" + name if relative != "." else name, os.path.join(folder, name)))
    entries.sort(key=lambda entry: entry[0])
    raw = io.BytesIO()
    with tarfile.open(fileobj=raw, mode="w", format=tarfile.PAX_FORMAT) as archive:
        root = tarfile.TarInfo(top)
        root.type, root.mode, root.mtime = tarfile.DIRTYPE, 0o755, mtime
        archive.addfile(root)
        for name, source in entries:
            info = tarfile.TarInfo(top + "/" + name)
            info.mtime, info.uid, info.gid, info.uname, info.gname = mtime, 0, 0, "", ""
            if source is None:
                info.type, info.mode = tarfile.DIRTYPE, 0o755
                archive.addfile(info)
                continue
            info.mode = 0o755 if name in EXECUTABLES else 0o644
            info.size = os.path.getsize(source)
            with open(source, "rb") as file:
                archive.addfile(info, file)
    with open(path, "wb") as file:
        with gzip.GzipFile(filename="", mode="wb", fileobj=file, mtime=mtime, compresslevel=9) as compressed:
            compressed.write(raw.getvalue())


def main():
    parser = argparse.ArgumentParser(description="Build the CrossCrease NHL 17 Linux runtime package.")
    parser.add_argument("--runtime", required=True, help="Linux shadps4 binary built from this source")
    parser.add_argument("--online-directory", default=os.path.join(ROOT, "..", "blaze", "Blaze15.1.1.3.0-nhl.01", "Games", "NHL17", "PS4", "v1.08", "Player"))
    parser.add_argument("--server-address", default="")
    parser.add_argument("--repository", default="CrossCrease/nhl17")
    parser.add_argument("--output-directory", default="")
    args = parser.parse_args()

    with open(os.path.join(ROOT, "tools", "release", "VERSION"), encoding="utf-8") as file:
        version = file.read().strip()
    if not re.fullmatch(r"\d+\.\d+\.\d+(-[0-9A-Za-z.-]+)?", version):
        fail("VERSION is not a semantic version: " + version)
    output = os.path.abspath(args.output_directory or os.path.join(ROOT, "artifacts", "release-linux", version))
    os.makedirs(output, exist_ok=True)
    name = "crosscrease-nhl17-runtime-" + version + "-linux-x64"
    package = os.path.join(output, name + ".tar.gz")
    source_url = "https://github.com/" + args.repository + "/tree/v" + version

    with tempfile.TemporaryDirectory(prefix="crosscrease-linux-") as stage:
        stage_package(stage, os.path.abspath(args.runtime), os.path.abspath(args.online_directory),
                      args.server_address, version, source_url)
        write_tarball(stage, package, source_date_epoch(), name)

    digest = hashlib.sha256()
    with open(package, "rb") as file:
        for chunk in iter(lambda: file.read(1 << 20), b""):
            digest.update(chunk)
    sha256 = digest.hexdigest()
    with open(package + ".sha256", "w", encoding="ascii", newline="\n") as file:
        file.write(sha256 + "  " + os.path.basename(package) + "\n")
    print(json.dumps({"file": os.path.basename(package), "path": package, "size": os.path.getsize(package), "sha256": sha256}))


if __name__ == "__main__":
    main()
