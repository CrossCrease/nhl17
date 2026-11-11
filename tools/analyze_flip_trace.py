import argparse
import collections
import json
import pathlib
import re

parser = argparse.ArgumentParser()
parser.add_argument("log", type=pathlib.Path)
args = parser.parse_args()
counts = collections.Counter()
packet = None
mismatches = []
repeated = []
for number, line in enumerate(args.log.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
    match = re.search(r"Flip packet: handle=(\d+) buffer=(\d+) arg=(\d+)", line)
    if match:
        packet = (int(match[2]), int(match[3]))
        counts["packets"] += 1
    match = re.search(r"Flip interrupt: buffer=(\d+) arg=(\d+) label=(\d+)", line)
    if match:
        counts["interrupts"] += 1
        callback = (int(match[1]), int(match[2]))
        if callback != packet or int(match[3]) != 1:
            mismatches.append({"line": number, "packet": packet, "callback": callback,
                               "label": int(match[3])})
    match = re.search(r"Flip present: buffer=(-?\d+) arg=(-?\d+) previous=(-?\d+)", line)
    if match:
        counts["presents"] += 1
        if match[1] == match[3] and int(match[1]) >= 0:
            repeated.append({"line": number, "buffer": int(match[1]), "arg": int(match[2])})
print(json.dumps({"counts": counts, "mismatch_count": len(mismatches),
                  "first_mismatches": mismatches[:8], "last_mismatches": mismatches[-8:],
                  "repeated_present_count": len(repeated),
                  "last_repeated_presents": repeated[-8:]}, indent=2))
