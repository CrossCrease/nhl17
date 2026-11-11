import json
import pathlib
import sys
import traceback
import renderdoc as rd

root = pathlib.Path.cwd()
out = root / "artifacts/gpu-frame"
out.mkdir(exist_ok=True)
cap = None
controller = None
try:
    paths = list((root / "runtime/user/captures").rglob("*.rdc"))
    message_file = root / "artifacts/rdoc-messages.json"
    if message_file.exists():
        paths += [pathlib.Path(p) for p in json.loads(message_file.read_text()) if p.endswith(".rdc")]
    path = max(paths, key=lambda p: p.stat().st_size)
    (out / "capture.txt").write_text(str(path))
    cap = rd.OpenCaptureFile()
    status = cap.OpenFile(str(path), "", None)
    if status != rd.ResultCode.Succeeded:
        raise RuntimeError(str(status))
    status, controller = cap.OpenCapture(rd.ReplayOptions(), None)
    if status != rd.ResultCode.Succeeded:
        raise RuntimeError(str(status))
    actions = []
    def walk(nodes):
        for a in nodes:
            actions.append(a)
            walk(a.children)
    walk(controller.GetRootActions())
    resources = {str(r.resourceId): r.name for r in controller.GetResources()}
    data = [{"event": a.eventId, "name": a.customName, "flags": str(a.flags),
             "count": a.numIndices, "outputs": [str(x) for x in a.outputs],
             "depth": str(a.depthOut)} for a in actions]
    (out / "actions.json").write_text(json.dumps(data, indent=2))
    controller.SetFrameEvent(max(a.eventId for a in actions), True)
    targets = {str(x) for a in actions for x in list(a.outputs) + [a.depthOut]}
    textures = []
    for t in controller.GetTextures():
        if str(t.resourceId) not in targets or t.width < 128:
            continue
        lo, hi = controller.GetMinMax(t.resourceId, rd.Subresource(), rd.CompType.Typeless)
        item = {"id": str(t.resourceId), "name": resources.get(str(t.resourceId), ""),
                "width": t.width, "height": t.height, "format": t.format.Name(),
                "min": list(lo.floatValue), "max": list(hi.floatValue)}
        textures.append(item)
        save = rd.TextureSave()
        save.resourceId = t.resourceId
        save.destType = rd.FileType.PNG
        save.alpha = rd.AlphaMapping.Discard
        filename = str(t.resourceId).replace("::", "_") + ".png"
        controller.SaveTexture(save, str(out / filename))
    (out / "textures.json").write_text(json.dumps(textures, indent=2))
except Exception:
    (out / "error.txt").write_text(traceback.format_exc())
finally:
    if controller:
        controller.Shutdown()
    if cap:
        cap.Shutdown()
    sys.exit(0)
