import json
import pathlib
import sys
import traceback
import time
import renderdoc as rd

root = pathlib.Path.cwd()
try:
    targets = []
    ident = rd.EnumerateRemoteTargets("localhost", 0)
    while ident:
        targets.append(ident)
        ident = rd.EnumerateRemoteTargets("localhost", ident)
    (root / "artifacts/rdoc-targets.json").write_text(json.dumps(targets))
    for ident in targets:
        target = rd.CreateTargetControl("localhost", ident, "NHL17 diagnostics", True)
        if target and target.Connected():
            (root / "artifacts/rdoc-target-info.json").write_text(json.dumps({
                "pid": target.GetPID(), "api": target.GetAPI(), "target": target.GetTarget()}))
            target.TriggerCapture(5)
            messages = []
            captures = 0
            until = time.monotonic() + 15
            while time.monotonic() < until and target.Connected():
                message = target.ReceiveMessage(None)
                if message.type != rd.TargetControlMessageType.Noop:
                    messages.append(str(message.type))
                if message.type == rd.TargetControlMessageType.NewCapture:
                    messages.append(message.newCapture.path)
                    captures += 1
                    if captures == 5:
                        break
                time.sleep(0.05)
            (root / "artifacts/rdoc-messages.json").write_text(json.dumps(messages))
            target.Shutdown()
except Exception:
    (root / "artifacts/rdoc-probe-error.txt").write_text(traceback.format_exc())
finally:
    sys.exit(0)
