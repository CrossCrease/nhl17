import argparse
import collections
import json
import pathlib
import re

parser = argparse.ArgumentParser(description="Summarize per-thread renderer timing scopes.")
parser.add_argument("log", type=pathlib.Path)
parser.add_argument("--last", type=float, default=60)
parser.add_argument("--output", type=pathlib.Path)
args = parser.parse_args()
content = args.log.read_text(encoding="utf-8", errors="replace")
pattern = re.compile(
    r"\(([^)]+)\).*Scope stats: name=(\w+) calls=(\d+) elapsed_ms=([\d.]+) "
    r"total_ms=([\d.]+) max_ms=([\d.]+) at_ms=([\d.]+)"
)
rows = [(m[1], m[2], *map(float, m.groups()[2:])) for m in pattern.finditer(content)]
if not rows:
    raise SystemExit("No scope timing records found.")
end = max(row[-1] for row in rows)
cutoff = end - args.last * 1000 if args.last > 0 else float("-inf")
groups = collections.defaultdict(list)
for thread, name, calls, elapsed, total, maximum, at in rows:
    if at - elapsed >= cutoff:
        groups[(thread, name)].append((calls, elapsed, total, maximum))
result = []
for (thread, name), samples in groups.items():
    calls = sum(row[0] for row in samples)
    elapsed = sum(row[1] for row in samples)
    total = sum(row[2] for row in samples)
    result.append({
        "thread": thread,
        "scope": name,
        "samples": len(samples),
        "calls": int(calls),
        "window_seconds": round(elapsed / 1000, 3),
        "total_ms": round(total, 3),
        "average_call_ms": round(total / calls, 4),
        "maximum_call_ms": max(row[3] for row in samples),
        "wall_time_percent": round(total * 100 / elapsed, 2),
    })
result.sort(key=lambda row: row["wall_time_percent"], reverse=True)
output = json.dumps({
    "log": str(args.log.resolve()),
    "last_seconds": args.last,
    "note": "Scopes overlap and must not be added. Rows spanning the window boundary are excluded.",
    "scopes": result,
}, indent=2) + "\n"
if args.output:
    args.output.write_text(output, encoding="utf-8")
print(output, end="")
