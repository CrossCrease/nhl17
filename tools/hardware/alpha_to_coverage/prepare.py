import pathlib
import shutil
import sys

sample = pathlib.Path(sys.argv[1]).resolve()
source = sample / 'src/main.c'
text = source.read_text()
needle = 'gnmDrawCmdDrawIndexAuto(&cmdbuf, 3);'
if text.count(needle) != 1:
    raise SystemExit('Expected one triangle draw in a fresh OpenGNM triangle sample')
text = text.replace('int main(void)', '#include "alpha_probe.h"\n\nint main(void)', 1)
text = text.replace(needle, 'draw_alpha_probe(&cmdbuf, display.screenw, display.screenh);')
source.with_suffix('.c.before-alpha-probe').write_text(source.read_text())
source.write_text(text)
root = pathlib.Path(__file__).resolve().parent
shutil.copyfile(root / 'probe.h', sample / 'src/alpha_probe.h')
for name in ('tri.vert.glsl', 'tri.frag.glsl'):
    target = sample / 'assets/misc' / name
    shutil.copyfile(target, target.with_suffix(target.suffix + '.before-alpha-probe'))
    shutil.copyfile(root / name, target)
print('Prepared OpenOrbis/OpenGNM alpha-to-coverage homebrew source:', sample)
