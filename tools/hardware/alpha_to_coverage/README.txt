OpenOrbis alpha-to-coverage hardware probe

Status: source prepared; not built with OpenOrbis or run on PS4 hardware.
This is a homebrew integration probe, not a unit test or hardware validation claim.

Use a separate, disposable checkout of the OpenOrbis-compatible OpenGNM examples:
https://github.com/PS4-OpenGNM/freegnm-examples
Its triangle sample provides memory allocation, shader loading, VideoOut and packaging.
Apply with: python prepare.py /absolute/path/to/freegnm-examples/triangle
Then follow the sample's OpenOrbis/OpenGNM build instructions (USE_OPENGNM=1),
including GLSL/psbc shader compilation, and install the resulting homebrew package.
Record the exact toolchain, examples and OpenGNM revisions used.

The replacement shaders draw solid blue with three alpha columns: 0, 0.5, 1.
The four screen rows set DB_ALPHA_TO_MASK.ENABLE and
DB_SHADER_CONTROL.ALPHA_TO_MASK_DISABLE respectively to:
  row 0: 0,0
  row 1: 1,0
  row 2: 0,1
  row 3: 1,1
The render target starts white. Rows 0,2,3 should be identical and fully blue.
In row 1, the alpha=0 area should retain white and alpha=1 should be blue.
Do not require a particular result for alpha=0.5 on this single-sample target.
Compare the four rows on PS4 hardware and the emulator, with screenshots.
This isolates enable/disable and endpoint behavior without title assets.

The existing triangle sample has a single-sample target. This probe does not
validate four-sample coverage thresholds, per-pixel dither offsets, sample layout,
or interaction with shader-exported sample masks. Those remain follow-up hardware
coverage; native Vulkan alpha-to-coverage does not reproduce programmable GCN
ALPHA_TO_MASK_OFFSET0..3 / OFFSET_ROUND exactly.

Register definitions checked against the vendored Mesa gfx7.json:
DB_ALPHA_TO_MASK 0xA2DC, enable bit 0, offsets bits 8..15;
DB_SHADER_CONTROL 0xA203, disable bit 11. Command header is SET_CONTEXT_REG.
Only apply to the simple supplied fragment shader: the probe writes the complete
DB_SHADER_CONTROL value and intentionally uses no depth, discard or stencil export.
