import json
import pathlib
import sys
import traceback
import renderdoc as rd

root = pathlib.Path.cwd()
out = root / "artifacts/gpu-frame"
cap = None
c = None
try:
    cap = rd.OpenCaptureFile()
    cap.OpenFile(str(root / "runtime/user/captures/black-menu-full-09.rdc"), "", None)
    status, c = cap.OpenCapture(rd.ReplayOptions(), None)
    if status != rd.ResultCode.Succeeded:
        raise RuntimeError(str(status))
    names = {str(r.resourceId): r.name for r in c.GetResources()}
    textures = {str(t.resourceId): t for t in c.GetTextures()}
    result = []
    for event in [2196, 2204, 2258, 2332, 2347, 2355, 2363]:
        c.SetFrameEvent(event, True)
        p = c.GetPipelineState()
        row = {"event": event, "shaders": {}, "inputs": [], "outputs": []}
        for stage in [rd.ShaderStage.Vertex, rd.ShaderStage.Hull, rd.ShaderStage.Domain, rd.ShaderStage.Pixel]:
            shader = p.GetShader(stage)
            row["shaders"][str(stage)] = names.get(str(shader), str(shader))
            refl = p.GetShaderReflection(stage)
            if refl:
                (out / (str(event) + "-" + str(stage) + ".spvasm")).write_text(
                    c.DisassembleShader(rd.ResourceId.Null(), refl, ""))
        for desc in p.GetReadOnlyResources(rd.ShaderStage.Pixel):
            rid = desc.descriptor.resource
            item = {"id": str(rid), "name": names.get(str(rid), ""),
                    "index": desc.access.index}
            if str(rid) in textures:
                t = textures[str(rid)]
                lo, hi = c.GetMinMax(rid, rd.Subresource(), rd.CompType.Typeless)
                item.update(width=t.width, height=t.height, min=list(lo.floatValue), max=list(hi.floatValue))
            row["inputs"].append(item)
        for target in p.GetOutputTargets():
            rid = target.resource
            if str(rid) not in textures:
                continue
            lo, hi = c.GetMinMax(rid, rd.Subresource(), rd.CompType.Typeless)
            row["outputs"].append({"id": str(rid), "min": list(lo.floatValue), "max": list(hi.floatValue)})
        result.append(row)
        (out / "passes.json").write_text(json.dumps(result, indent=2))
except Exception:
    (out / "passes-error.txt").write_text(traceback.format_exc())
finally:
    if c:
        c.Shutdown()
    if cap:
        cap.Shutdown()
    sys.exit(0)
