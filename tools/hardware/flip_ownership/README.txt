OpenOrbis flip ownership hardware probe

Status: source prepared; not built with OpenOrbis or run on console hardware.
This is a diagnostic homebrew, not evidence that the emulator matches hardware.
It contains no game assets or proprietary SDK files.

Purpose
Compare VideoOut buffer labels and completed flip arguments while two static
framebuffers are repeatedly queued A/B/A. Exercise one, two and three pending
flips, at 60 Hz and 30 Hz, with 600 flips per phase. The images remain static,
so queueing a buffer again never overwrites an image still being displayed.
The probe records label values without assuming that hardware labels are
binary or that CPU observations occur atomically with a presentation.

Build
Use the official OpenOrbis PS4 toolchain and its hello_world sample:
https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain
https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/tree/master/samples/hello_world
Copy that sample outside this source tree. Replace its hello_world/main.cpp
with this main.cpp. In its Makefile, add -lSceVideoOut -lSceGnmDriver to LIBS,
preserving the existing standard-library and kernel entries. Set
OO_PS4_TOOLCHAIN to your toolchain installation and build with its supported
make/Clang environment. Keep the sample's packaging assets and instructions.

The VideoOut and Gnm function declarations here fill in untyped declarations
in the public OpenOrbis headers. Their ABI should be checked against the
toolchain version being used. No Sony headers are required.

Run and compare
Run the resulting homebrew on a homebrew-capable PS4 and on the emulator.
Collect /data/flip-ownership.csv (stdout is used if the file cannot be opened).
Record console model, firmware and OpenOrbis revision with the trace.
Success means all six phases finish and the file ends with complete.
An error or five-second timeout is diagnostic; it is not silently retried.
Compare return codes, argument order, queue counts and label transitions.
Multiple flips can complete between samples; a skipped observed argument is
not by itself an ordering error. after-submit rows use the preceding status
snapshot; their labels are sampled after the submit call returns.

The original emulator failure cleared A's label while presenting B even
though a newer GPU flip already owned A. The proposed fix tracks ownership
when the GPU executes its label-lock write, and avoids clearing that newer
ownership when retiring an older presentation. The probe helps test label
semantics; it cannot force the exact host-thread interleaving of the crash.
It also does not cover GPU WaitFlipDone command streams or depth conversion.

Reference for the independently reported race:
https://github.com/ducvd89/Bloodborne-Android/issues/160
The current implementation counts GPU-executed locks, rather than counting
future EOP requests at CPU registration time.
