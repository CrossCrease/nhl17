import argparse
import json
import pathlib
import struct

parser = argparse.ArgumentParser(description="Inspect cached vertex/TCS location extents.")
parser.add_argument("cache", type=pathlib.Path)
parser.add_argument("--output", type=pathlib.Path)
args = parser.parse_args()


def inspect(path, data=None):
    data = path.read_bytes() if data is None else data
    words = struct.unpack(f"<{len(data) // 4}I", data)
    if words[0] != 0x07230203:
        raise ValueError(f"Invalid SPIR-V header: {path}")
    types, constants, variables, locations, names = {}, {}, {}, {}, {}
    entry = None
    pos = 5
    while pos < len(words):
        count, op = words[pos] >> 16, words[pos] & 65535
        if not count or pos + count > len(words):
            raise ValueError(f"Invalid instruction: {path}:{pos}")
        a = words[pos + 1:pos + count]
        if op == 15:
            entry = a[0]
        elif op == 5:
            names[a[0]] = struct.pack(f"<{len(a)-1}I", *a[1:]).split(b"\0")[0].decode()
        elif op in (21, 22, 23, 24, 28, 29, 30, 32):
            types[a[0]] = (op, a[1:])
        elif op == 43:
            constants[a[1]] = a[2]
        elif op == 59:
            variables[a[1]] = (a[0], a[2])
        elif op == 71 and a[1] == 30:
            locations[a[0]] = a[2]
        pos += count

    def slots(type_id):
        op, a = types[type_id]
        if op == 28:
            return slots(a[0]) * constants[a[1]]
        if op in (21, 22, 23):
            return 1
        raise ValueError(f"Unsupported interface type {op}: {path}")

    result = {"file": path.name, "model": entry, "inputs": [], "outputs": []}
    for var, location in locations.items():
        if var not in variables:
            continue
        pointer, storage = variables[var]
        if storage not in (1, 3):
            continue
        type_id = types[pointer][1][1]
        if entry == 1 and storage in (1, 3):
            if types[type_id][0] != 28:
                continue
            type_id = types[type_id][1][0]
        result["inputs" if storage == 1 else "outputs"].append({
            "name": names.get(var, str(var)), "location": location, "slots": slots(type_id)
        })
    return result


modules = {}
for path in args.cache.glob("*.spv"):
    seed, index = path.stem.split("_")
    seed, index = int(seed, 16), int(index)
    key = (seed ^ (index + 0x9e3779b9 + (seed << 12) + (seed >> 4))) & ((1 << 64) - 1)
    modules[key] = inspect(path)

pairs = {}
for path in args.cache.glob("g_*.key"):
    data = path.read_bytes()
    version, kind, *stages = struct.unpack_from("<II6Q", data)
    if version != 3 or kind != 0:
        raise ValueError(f"Unsupported pipeline schema: {path}")
    if stages[1]:
        tcs = modules[stages[1]]
    else:
        tcs = None
        offset = data.find(b"\x03\x02\x23\x07")
        while offset >= 8:
            count = struct.unpack_from("<Q", data, offset - 8)[0]
            if 5 <= count <= (len(data) - offset) // 4:
                auxiliary = inspect(path, data[offset:offset + count * 4])
                if auxiliary["model"] == 1:
                    tcs = auxiliary
                    break
            offset = data.find(b"\x03\x02\x23\x07", offset + 4)
        if tcs is None:
            continue
    vertex = modules[stages[3]]
    pair = (vertex["file"], tcs["file"])
    if pair not in pairs:
        provided = {
            i for v in vertex["outputs"]
            for i in range(v["location"], v["location"] + v["slots"])
        }
        requested = {
            i for v in tcs["inputs"]
            for i in range(v["location"], v["location"] + v["slots"])
        }
        pairs[pair] = {
            "vertex": vertex,
            "tcs": tcs,
            "fragment": modules.get(stages[0]),
            "missing_locations": sorted(requested - provided),
            "pipelines": [],
        }
    pairs[pair]["pipelines"].append(path.name)

report = {
    "note": "Location extent diagnostic for 32-bit scalar/vector/array interfaces; not a full SPIR-V validator.",
    "pairs": list(pairs.values()),
}
output = json.dumps(report, indent=2) + "\n"
if args.output:
    args.output.write_text(output, encoding="utf-8")
print(output, end="")
