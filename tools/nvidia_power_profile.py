import argparse
import ctypes as c
import json
import pathlib

parser = argparse.ArgumentParser(description="Inspect or manage an isolated NHL17 NVIDIA power profile.")
parser.add_argument("executable", type=pathlib.Path)
parser.add_argument("--mode", choices=["inspect", "maximum", "restore"], default="inspect")
parser.add_argument("--state", type=pathlib.Path, default=pathlib.Path("artifacts/nvidia-power-state.json"))
args = parser.parse_args()
U = c.c_uint32
H = c.c_void_p
W = c.c_wchar * 2048


class Value(c.Union):
    _pack_ = 4
    _fields_ = [("dword", U), ("bytes", c.c_ubyte * 4100)]


class Setting(c.Structure):
    _pack_ = 4
    _fields_ = [("version", U), ("name", W), ("id", U), ("type", U),
                ("location", U), ("predefined", U), ("predefined_valid", U),
                ("default", Value), ("current", Value)]


class App(c.Structure):
    _fields_ = [("version", U), ("predefined", U), ("name", W), ("friendly", W),
                ("launcher", W), ("file_in_folder", W), ("flags", U), ("command", W)]


class Profile(c.Structure):
    _fields_ = [("version", U), ("name", W), ("gpu_support", U),
                ("predefined", U), ("apps", U), ("settings", U)]


def versioned(cls, version=1):
    result = cls()
    result.version = c.sizeof(cls) | (version << 16)
    return result


dll = c.CDLL("nvapi64.dll")
query = dll.nvapi_QueryInterface
query.restype = H
query.argtypes = [U]


def api(identifier, *params):
    address = query(identifier)
    if not address:
        raise RuntimeError(f"NVAPI function {identifier:x} unavailable")
    return c.CFUNCTYPE(c.c_int, *params)(address)


def check(status):
    if status != 0:
        raise RuntimeError(f"NVAPI status {status}")


check(api(0x0150e828)())
session = H()
check(api(0x0694d52e, c.POINTER(H))(c.byref(session)))
try:
    check(api(0x375dbd6b, H)(session))
    profile = H()
    app = versioned(App, 4)
    path = str(args.executable.resolve())
    found = api(0xeee566b2, H, c.c_wchar_p, c.POINTER(H), c.POINTER(App))(
        session, path, c.byref(profile), c.byref(app))
    profile_name = "NHL17 shadPS4 local performance"
    marker = pathlib.Path(path).parent / ".nhl17-nvidia-performance"
    marker_text = "NHL17 isolated NVIDIA application profile\n"
    if args.mode == "maximum":
        if found != -166:
            raise RuntimeError(f"Expected no existing application profile, got {found}; leaving it unchanged")
        if args.state.exists() or marker.exists():
            raise RuntimeError("Previous profile state or marker exists; inspect or restore it first")
        info = versioned(Profile)
        info.name = profile_name
        check(api(0xcc176068, H, c.POINTER(Profile), c.POINTER(H))(
            session, c.byref(info), c.byref(profile)))
        app = versioned(App, 4)
        app.name = pathlib.Path(path).name.lower()
        app.friendly = "NHL 17 local shadPS4 runtime"
        app.file_in_folder = marker.name
        check(api(0x4347a9de, H, H, c.POINTER(App))(session, profile, c.byref(app)))
        setting = versioned(Setting)
        setting.id = 0x1057eb71
        setting.current.dword = 1
        check(api(0x577dd202, H, H, c.POINTER(Setting))(session, profile, c.byref(setting)))
        args.state.write_text(json.dumps({
            "executable": path, "created_profile": profile_name,
            "marker": str(marker), "prior_application_lookup_status": found,
            "setting_id": "0x1057eb71", "applied_value": 1,
        }, indent=2) + "\n")
        marker.write_text(marker_text)
        check(api(0xfcbc7e14, H)(session))
        print(json.dumps({"created_profile": profile_name, "power_policy": 1,
                          "application": app.name, "required_file": app.file_in_folder,
                          "restore_state": str(args.state.resolve())}, indent=2))
        raise SystemExit(0)
    if args.mode == "restore":
        state = json.loads(args.state.read_text())
        if state["executable"] != path or state["created_profile"] != profile_name:
            raise RuntimeError("Restore state does not match this runtime")
        check(found)
        info = versioned(Profile)
        check(api(0x61cd6fd6, H, H, c.POINTER(Profile))(session, profile, c.byref(info)))
        if info.name != profile_name or info.apps != 1 or app.file_in_folder != marker.name:
            raise RuntimeError("Profile ownership changed; refusing to remove it")
        if marker.read_text() != marker_text:
            raise RuntimeError("Runtime marker changed; refusing to remove it")
        check(api(0x17093206, H, H)(session, profile))
        check(api(0xfcbc7e14, H)(session))
        marker.unlink()
        state["restored"] = True
        args.state.write_text(json.dumps(state, indent=2) + "\n")
        print(json.dumps({"removed_profile": profile_name, "restored": True}))
        raise SystemExit(0)
    if found != 0:
        if found != -166:
            check(found)
        check(api(0xda8466a0, H, c.POINTER(H))(session, c.byref(profile)))
    info = versioned(Profile)
    check(api(0x61cd6fd6, H, H, c.POINTER(Profile))(session, profile, c.byref(info)))
    setting = versioned(Setting)
    status = api(0x73bf8338, H, H, U, c.POINTER(Setting))(
        session, profile, 0x1057eb71, c.byref(setting))
    if status not in (0, -160):
        check(status)
    print(json.dumps({
        "executable": path, "application_lookup_status": found,
        "application": app.name, "profile": info.name,
        "setting_id": "0x1057eb71", "setting_query_status": status,
        "power_policy": setting.current.dword if status == 0 else None,
        "setting_location": setting.location, "predefined": setting.predefined,
        "note": "Read-only. Policy 1 is prefer maximum performance; 5 is optimal power.",
    }, indent=2))
finally:
    api(0xdad9cff8, H)(session)
