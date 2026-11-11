#!/usr/bin/env python3
import argparse
import hashlib
import json
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def printable_strings(data, minimum=4):
    start = None
    for index, value in enumerate(data):
        if 0x20 <= value <= 0x7E:
            if start is None:
                start = index
        elif start is not None:
            if index - start >= minimum:
                yield start, data[start:index].decode("ascii")
            start = None
    if start is not None and len(data) - start >= minimum:
        yield start, data[start:].decode("ascii")


def inspect_binary(path):
    data = path.read_bytes()
    strings = list(printable_strings(data))
    blaze_paths = []
    redirectors = []
    version_fields = []
    for offset, value in strings:
        if "BlazeSDK/" in value:
            match = re.search(r"BlazeSDK/([^/\\]+)", value)
            if match:
                blaze_paths.append({"offset": offset, "version": match.group(1), "string": value})
        if "gosredirector" in value:
            redirectors.extend(
                {"offset": offset + match.start(), "hostname": match.group(0).lower()}
                for match in re.finditer(r"[a-z0-9.-]+\.gosredirector\.[a-z0-9.-]+", value, re.I)
            )
        if value in {"blazeSDKVersion", "blazeSDKBuildDate", "dirtySDKVersion", "clientVersion"}:
            version_fields.append({"offset": offset, "field": value})
    return {
        "path": str(path.relative_to(ROOT)).replace("\\", "/"),
        "bytes": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
        "blaze_paths": sorted(blaze_paths, key=lambda item: (item["version"], item["offset"])),
        "redirectors": sorted(
            {item["hostname"]: item for item in redirectors}.values(),
            key=lambda item: item["hostname"],
        ),
        "version_fields": sorted(version_fields, key=lambda item: item["offset"]),
    }


def read_online_metadata(root):
    values = {}
    for relative in (
        "nhl/data/kettle/config/titleinfo.ini",
        "nhl/data/kettle/config/na_versioninfo.ini",
    ):
        path = root / relative
        if not path.exists():
            continue
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            line = line.strip()
            if line and not line.startswith(";") and "=" in line:
                key, value = line.split("=", 1)
                values[key.strip()] = value.strip()
    return values


def main():
    parser = argparse.ArgumentParser(description="Extract NHL 17 online build evidence.")
    parser.add_argument("--base", type=Path, default=ROOT / "game/eboot.bin")
    parser.add_argument("--update", type=Path, default=ROOT / "game-UPDATE/eboot.bin")
    parser.add_argument(
        "--output", type=Path, default=ROOT / "artifacts/online-research/static-client-evidence.json"
    )
    args = parser.parse_args()
    evidence = {
        "scope": "Static executable and shipped metadata inspection; no network traffic",
        "base": inspect_binary(args.base.resolve()),
        "update": inspect_binary(args.update.resolve()),
        "base_metadata": read_online_metadata(ROOT / "game"),
        "update_metadata": read_online_metadata(ROOT / "game-UPDATE"),
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")
    print(args.output.resolve())


if __name__ == "__main__":
    main()
