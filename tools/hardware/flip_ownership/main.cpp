#include <orbis/_types/video.h>
#include <orbis/libkernel.h>

#include <cstdio>
#include <cstring>

extern "C" {
int32_t sceVideoOutOpen(int32_t, int32_t, int32_t, const void*);
int32_t sceVideoOutClose(int32_t);
int32_t sceVideoOutRegisterBuffers(int32_t, int32_t, void* const*, int32_t,
                                   const OrbisVideoOutBufferAttribute*);
int32_t sceVideoOutUnregisterBuffers(int32_t, int32_t);
void sceVideoOutSetBufferAttribute(void*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                                   uint32_t);
int32_t sceVideoOutSetFlipRate(int32_t, int32_t);
int32_t sceVideoOutGetFlipStatus(int32_t, OrbisVideoOutFlipStatus*);
int32_t sceVideoOutGetBufferLabelAddress(int32_t, uintptr_t*);
int32_t sceVideoOutSubmitFlip(int32_t, int32_t, uint32_t, int64_t);
int32_t sceGnmRequestFlipAndSubmitDone(uint32_t*, uint32_t, uint32_t, uint32_t, uint32_t, int64_t);
}

constexpr size_t Align(size_t size) {
    return (size + 0x3fff) & ~size_t{0x3fff};
}

constexpr uint32_t Width = 1280;
constexpr uint32_t Height = 720;
constexpr size_t FrameSize = Align(Width * Height * sizeof(uint32_t));
constexpr size_t CommandSize = 256;
constexpr size_t SlotCount = 8;
constexpr size_t AllocationSize = 2 * FrameSize + Align(CommandSize * SlotCount);

FILE* output;
volatile uint64_t* labels;

bool Check(const char* operation, int32_t result) {
    if (result >= 0) {
        return true;
    }
    std::fprintf(output, "error,%s,%08x\n", operation, static_cast<uint32_t>(result));
    std::fflush(output);
    return false;
}

void Record(const char* phase, const char* event, int64_t argument,
            const OrbisVideoOutFlipStatus& status) {
    std::fprintf(output, "%s,%s,%llu,%lld,%lld,%d,%d,%d,%llu,%llu\n", phase, event,
                 static_cast<unsigned long long>(sceKernelGetProcessTime()),
                 static_cast<long long>(argument), static_cast<long long>(status.flipArg),
                 status.currentBuffer, status.numGpuFlipPending, status.numFlipPending,
                 static_cast<unsigned long long>(labels[0]),
                 static_cast<unsigned long long>(labels[1]));
}

bool WaitFor(int handle, int64_t argument, const char* phase) {
    const auto start = sceKernelGetProcessTime();
    OrbisVideoOutFlipStatus status{};
    while (sceKernelGetProcessTime() - start < 5000000) {
        if (!Check("GetFlipStatus", sceVideoOutGetFlipStatus(handle, &status))) {
            return false;
        }
        if (status.flipArg >= argument) {
            Record(phase, "drained", argument, status);
            std::fflush(output);
            return true;
        }
        sceKernelUsleep(100);
    }
    Record(phase, "timeout", argument, status);
    std::fflush(output);
    return false;
}

bool RunPhase(int handle, uint32_t* commands, int64_t& argument, int rate, int queue_depth) {
    if (!Check("SetFlipRate", sceVideoOutSetFlipRate(handle, rate))) {
        return false;
    }
    char phase[40];
    std::snprintf(phase, sizeof(phase), "%dhz-depth%d", 60 / (rate + 1), queue_depth);
    int64_t slot_arguments[SlotCount]{};
    const int64_t initial_argument = argument;
    for (auto& slot_argument : slot_arguments) {
        slot_argument = initial_argument;
    }
    int64_t observed = initial_argument;
    auto last_progress = sceKernelGetProcessTime();
    for (int i = 0; i < 600;) {
        OrbisVideoOutFlipStatus status{};
        if (!Check("GetFlipStatus", sceVideoOutGetFlipStatus(handle, &status))) {
            return false;
        }
        if (status.flipArg != observed) {
            observed = status.flipArg;
            last_progress = sceKernelGetProcessTime();
            Record(phase, "presented", argument, status);
        }
        if (sceKernelGetProcessTime() - last_progress > 5000000) {
            Record(phase, "timeout", argument, status);
            std::fflush(output);
            return false;
        }
        const size_t slot = static_cast<size_t>(i) % SlotCount;
        if (argument - status.flipArg >= queue_depth || status.flipArg < slot_arguments[slot]) {
            sceKernelUsleep(100);
            continue;
        }
        auto* command = commands + slot * CommandSize / sizeof(uint32_t);
        std::memset(command, 0, CommandSize);
        const int64_t next_argument = argument + 1;
        Record(phase, "before-submit", next_argument, status);
        if (!Check("RequestFlipAndSubmitDone",
                   sceGnmRequestFlipAndSubmitDone(command, CommandSize, handle, i % 2,
                                                  ORBIS_VIDEO_OUT_FLIP_VSYNC, next_argument))) {
            return false;
        }
        argument = next_argument;
        slot_arguments[slot] = argument;
        Record(phase, "after-submit", argument, status);
        ++i;
    }
    return WaitFor(handle, argument, phase);
}

int main() {
    output = std::fopen("/data/flip-ownership.csv", "w");
    if (output == nullptr) {
        output = stdout;
    }
    std::fprintf(output, "phase,event,time_us,submitted_arg,presented_arg,current_buffer,"
                         "gpu_pending,flip_pending,label0,label1\n");
    off_t physical = 0;
    if (!Check("AllocateDirectMemory",
               sceKernelAllocateDirectMemory(0, ORBIS_KERNEL_MAIN_DMEM_SIZE, AllocationSize, 0x4000,
                                             ORBIS_KERNEL_WC_GARLIC, &physical))) {
        return 1;
    }
    void* memory = nullptr;
    if (!Check("MapDirectMemory",
               sceKernelMapDirectMemory(&memory, AllocationSize,
                                        ORBIS_KERNEL_PROT_CPU_RW | ORBIS_KERNEL_PROT_GPU_RW, 0,
                                        physical, 0x4000))) {
        return 1;
    }
    void* buffers[2] = {memory, static_cast<uint8_t*>(memory) + FrameSize};
    for (int buffer = 0; buffer < 2; ++buffer) {
        auto* pixels = static_cast<uint32_t*>(buffers[buffer]);
        for (size_t i = 0; i < Width * Height; ++i) {
            pixels[i] = 0xff303030;
        }
        for (uint32_t y = 32; y < 64; ++y) {
            for (uint32_t x = 32 + buffer * 40; x < 64 + buffer * 40; ++x) {
                pixels[y * Width + x] = 0xff606060;
            }
        }
    }
    __asm__ volatile("sfence" ::: "memory");
    const int handle = sceVideoOutOpen(ORBIS_VIDEO_USER_MAIN, ORBIS_VIDEO_OUT_BUS_MAIN, 0, nullptr);
    if (!Check("VideoOutOpen", handle)) {
        return 1;
    }
    OrbisVideoOutBufferAttribute attribute{};
    sceVideoOutSetBufferAttribute(&attribute, ORBIS_VIDEO_OUT_PIXEL_FORMAT_A8B8G8R8_SRGB,
                                  ORBIS_VIDEO_OUT_TILING_MODE_LINEAR,
                                  ORBIS_VIDEO_OUT_ASPECT_RATIO_16_9, Width, Height, Width);
    const int group = sceVideoOutRegisterBuffers(handle, 0, buffers, 2, &attribute);
    uintptr_t label_address = 0;
    if (!Check("RegisterBuffers", group) ||
        !Check("GetBufferLabelAddress", sceVideoOutGetBufferLabelAddress(handle, &label_address)) ||
        label_address == 0) {
        return 1;
    }
    labels = reinterpret_cast<volatile uint64_t*>(label_address);
    OrbisVideoOutFlipStatus initial{};
    if (!Check("GetFlipStatus", sceVideoOutGetFlipStatus(handle, &initial))) {
        return 1;
    }
    int64_t argument = initial.flipArg;
    auto* commands = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(memory) + 2 * FrameSize);
    for (int rate = 0; rate <= 1; ++rate) {
        for (int depth = 1; depth <= 3; ++depth) {
            if (!RunPhase(handle, commands, argument, rate, depth)) {
                return 1;
            }
        }
    }
    ++argument;
    if (!Check("BlankFlip",
               sceVideoOutSubmitFlip(handle, -1, ORBIS_VIDEO_OUT_FLIP_VSYNC, argument)) ||
        !WaitFor(handle, argument, "blank")) {
        return 1;
    }
    Check("UnregisterBuffers", sceVideoOutUnregisterBuffers(handle, group));
    Check("VideoOutClose", sceVideoOutClose(handle));
    Check("Munmap", sceKernelMunmap(memory, AllocationSize));
    Check("ReleaseDirectMemory", sceKernelReleaseDirectMemory(physical, AllocationSize));
    std::fprintf(output, "complete\n");
    std::fflush(output);
    if (output != stdout) {
        std::fclose(output);
    }
    return 0;
}
