OpenOrbis flip-pending homebrew probe

Status: source supplied; not built with the OpenOrbis SDK or run on PS4 hardware.

Use the OpenOrbis C++ application template. Replace its main.cpp with this file;
link SceGnmDriver, SceVideoOut, kernel and SceLibcInternal, then use the template's
normal OELF/SELF/package pipeline. No proprietary shaders or game files are needed.
Capture stdout using the template's usual debug output setup. Record firmware,
SDK revision and whether the run is native PS4 hardware or emulator.

The program allocates two linear color surfaces and a GPU-readable gate. A
WAIT_REG_MEM packet prevents the GPU from reaching the prepared flip until the
CPU releases the gate 100 ms after submission. The program reports flip status
before submission, immediately after submission, while still gated, and after
completion, then repeats on the other surface. It always releases the gate before
reporting a submission failure or waiting for completion.

Behavior under investigation: immediately after successful SubmitAndFlip, pending
and GPU-pending should include the accepted request, even while the GPU is gated.
Completed count/argument should remain unchanged while gated. On presentation,
the completed count/argument should advance and pending counters should drain.
The emulator previously reported zero pending throughout the gated interval.
These expectations need confirmation on console; this source is not a claim of
hardware validation. A separate observation of the brief GPU-ready/display-pending
transition would be needed to validate precisely when GPU-pending decrements.

References:
https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain
https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/blob/master/include/orbis/_types/video.h
https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/blob/master/include/orbis/VideoOut.h

No unit tests are added. This probe is a standalone OpenOrbis homebrew application.
