import argparse
import json
import pathlib
import re

parser = argparse.ArgumentParser(description="Correlate late guest flips with overlapping renderer scopes.")
parser.add_argument("log", type=pathlib.Path)
parser.add_argument("--flip-rate", type=int, default=0)
parser.add_argument("--output", type=pathlib.Path)
args = parser.parse_args()
scopes = []
hitches = []
current_rate = None
rate_changed = False
first_frame_at = None
for line in args.log.read_text(encoding="utf-8", errors="replace").splitlines():
    change = re.search(r"Flip rate: handle=\d+ rate=(\d+)", line)
    if change:
        new_rate = int(change[1])
        rate_changed |= current_rate != new_rate
        current_rate = new_rate
    slow = re.search(r"\(([^)]+)\).*Scope slow: name=(\w+) duration_ms=([\d.]+) at_ms=([\d.]+)", line)
    if slow:
        scopes.append({"thread": slow[1], "name": slow[2],
                       "duration_ms": float(slow[3]), "at_ms": float(slow[4])})
    frame = re.search(r"Frame stats: frames=(\d+) elapsed_ms=([\d.]+) fps=([\d.]+) "
                      r"min_ms=([\d.]+) max_ms=([\d.]+) at_ms=([\d.]+) flip_rate=(\d+) "
                      r"late_frames=(\d+) empty_slots=(\d+)", line)
    if not frame:
        continue
    selected = int(frame[7]) == args.flip_rate and not rate_changed
    rate_changed = False
    if not selected:
        continue
    at = float(frame[6])
    elapsed = float(frame[2])
    if first_frame_at is None:
        first_frame_at = at - elapsed
    if int(frame[8]):
        hitches.append({"at_ms": at, "elapsed_ms": elapsed,
                        "seconds_since_first_selected_frame": round((at-first_frame_at)/1000, 3),
                        "fps": float(frame[3]), "longest_interval_ms": float(frame[5]),
                        "late_frames": int(frame[8]), "empty_slots": int(frame[9])})
for hitch in hitches:
    end = hitch["at_ms"]
    start = end - hitch["elapsed_ms"]
    hitch["overlapping_slow_scopes"] = [
        scope for scope in scopes
        if scope["at_ms"] >= start and scope["at_ms"] - scope["duration_ms"] <= end
    ]
result = {"log": str(args.log.resolve()), "flip_rate": args.flip_rate,
          "note": "Scope overlap with a one-second timing sample is correlation, not proof of cause. Nested durations must not be added.",
          "hitches": hitches}
output = json.dumps(result, indent=2) + "\n"
if args.output:
    args.output.write_text(output, encoding="utf-8")
print(output, end="")
