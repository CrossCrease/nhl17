import argparse
import json
import pathlib
import re

parser = argparse.ArgumentParser(description="Summarize completed guest flip timing samples.")
parser.add_argument("log", type=pathlib.Path)
parser.add_argument("--last", type=int, default=60)
parser.add_argument("--first", type=int, default=0)
parser.add_argument("--flip-rate", type=int)
parser.add_argument("--output", type=pathlib.Path)
args = parser.parse_args()
pattern = re.compile(
    r"Frame stats: frames=(\d+) elapsed_ms=([\d.]+) fps=([\d.]+) "
    r"min_ms=([\d.]+) max_ms=([\d.]+)"
)
rows = []
rate_changed = False
current_rate = None
for line in args.log.read_text(encoding="utf-8", errors="replace").splitlines():
    rate_change = re.search(r"Flip rate: handle=\d+ rate=(\d+)", line)
    if rate_change:
        new_rate = int(rate_change[1])
        rate_changed |= new_rate != current_rate
        current_rate = new_rate
    match = pattern.search(line)
    if not match:
        continue
    rate = re.search(r"flip_rate=(\d+)", line)
    if args.flip_rate is None or (
        rate and int(rate[1]) == args.flip_rate and not rate_changed
    ):
        pacing = re.search(
            r"late_frames=(\d+) empty_slots=(\d+) wakeup_max_ms=([\d.]+) present_max_ms=([\d.]+)", line
        )
        counters = tuple(map(float, pacing.groups())) if pacing else (None,) * 4
        rows.append((*map(float, match.groups()), *counters))
    rate_changed = False
if args.first > 0:
    rows = rows[:args.first]
elif args.last > 0:
    rows = rows[-args.last:]
if not rows:
    raise SystemExit("No frame timing samples found.")
frames = sum(row[0] for row in rows)
elapsed = sum(row[1] for row in rows)
result = {
    "log": str(args.log.resolve()),
    "samples": len(rows),
    "flip_rate_filter": args.flip_rate,
    "duration_seconds": round(elapsed / 1000, 3),
    "completed_frames": int(frames),
    "average_fps": round(frames * 1000 / elapsed, 3),
    "minimum_sample_fps": min(row[2] for row in rows),
    "maximum_sample_fps": max(row[2] for row in rows),
    "longest_frame_interval_ms": max(row[4] for row in rows),
    "note": (
        "Includes all phases in the selected window. Sample minima are not 1% lows."
        if args.flip_rate is None else
        "Only steady samples at the requested flip rate; transitions omitted. Samples may be noncontiguous. Minima are not 1% lows."
    ),
}
if args.flip_rate is not None:
    target_fps = 60 / (args.flip_rate + 1)
    result.update({
        "target_fps": target_fps,
        "samples_below_target_minus_1_fps": sum(row[2] < target_fps - 1 for row in rows),
    })
    if args.flip_rate == 0:
        result["samples_below_59_fps"] = result["samples_below_target_minus_1_fps"]
if all(row[5] is not None for row in rows):
    result.update({
        "frames_over_1_5x_target": int(sum(row[5] for row in rows)),
        "empty_vblank_slots": int(sum(row[6] for row in rows)),
        "maximum_wakeup_lateness_ms": max(row[7] for row in rows),
        "maximum_present_call_ms": max(row[8] for row in rows),
    })
output = json.dumps(result, indent=2) + "\n"
if args.output:
    args.output.write_text(output, encoding="utf-8")
print(output, end="")
