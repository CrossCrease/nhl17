#include <cstdint>
#include <cstdio>
#include <cstring>
#include <orbis/VideoOut.h>
#include <orbis/libkernel.h>

extern "C" int sceGnmSubmitAndFlipCommandBuffers(
    uint32_t count, uint32_t** dcb, uint32_t* dcb_sizes, uint32_t** ccb,
    uint32_t* ccb_sizes, uint32_t handle, uint32_t buffer, uint32_t mode, int64_t arg);
extern "C" int sceGnmSubmitDone();

static void report(const char* phase, int handle) {
    OrbisVideoOutFlipStatus status{};
    const int result = sceVideoOutGetFlipStatus(handle, &status);
    std::printf("%s result=%x completed=%llu gpu_pending=%d pending=%d arg=%lld buffer=%d\n",
                phase, result, static_cast<unsigned long long>(status.num),
                status.numGpuFlipPending, status.numFlipPending,
                static_cast<long long>(status.flipArg), status.currentBuffer);
    std::fflush(stdout);
}

int main() {
    constexpr size_t allocation_size = 16 * 1024 * 1024;
    constexpr size_t alignment = 2 * 1024 * 1024;
    off_t physical = 0;
    int result = sceKernelAllocateDirectMemory(0, sceKernelGetDirectMemorySize(),
                                               allocation_size, alignment, 3, &physical);
    if (result != 0) {
        std::printf("allocate failed: %x\n", result);
        return 1;
    }
    void* mapped = nullptr;
    result = sceKernelMapDirectMemory(&mapped, allocation_size, 0x33, 0, physical, alignment);
    if (result != 0) {
        std::printf("map failed: %x\n", result);
        return 1;
    }
    auto* memory = static_cast<uint8_t*>(mapped);
    void* buffers[] = {memory, memory + 4 * 1024 * 1024};
    for (unsigned b = 0; b < 2; ++b) {
        auto* pixels = static_cast<uint32_t*>(buffers[b]);
        for (unsigned i = 0; i < 1280 * 720; ++i) {
            pixels[i] = b ? 0xff00ff00 : 0xffff0000;
        }
    }
    auto* commands = reinterpret_cast<uint32_t*>(memory + 8 * 1024 * 1024);
    auto* gate = reinterpret_cast<uint32_t*>(memory + 12 * 1024 * 1024);
    const int handle = sceVideoOutOpen(ORBIS_VIDEO_USER_MAIN, ORBIS_VIDEO_OUT_BUS_MAIN, 0, nullptr);
    if (handle < 0) {
        std::printf("video open failed: %x\n", handle);
        return 1;
    }
    OrbisVideoOutBufferAttribute attribute{};
    sceVideoOutSetBufferAttribute(&attribute, ORBIS_VIDEO_OUT_PIXEL_FORMAT_A8B8G8R8_SRGB,
                                 ORBIS_VIDEO_OUT_TILING_MODE_LINEAR,
                                 ORBIS_VIDEO_OUT_ASPECT_RATIO_16_9, 1280, 720, 1280);
    result = sceVideoOutRegisterBuffers(handle, 0, buffers, 2, &attribute);
    if (result < 0) {
        std::printf("register failed: %x\n", result);
        return 1;
    }
    sceVideoOutSetFlipRate(handle, ORBIS_VIDEO_OUT_FLIP_30HZ);
    for (unsigned b = 0; b < 2; ++b) {
        std::memset(commands, 0, 71 * sizeof(uint32_t));
        __atomic_store_n(gate, 0, __ATOMIC_SEQ_CST);
        const uintptr_t address = reinterpret_cast<uintptr_t>(gate);
        commands[0] = 0xc0053c00;
        commands[1] = 0x13;
        commands[2] = static_cast<uint32_t>(address);
        commands[3] = static_cast<uint32_t>(address >> 32);
        commands[4] = 1;
        commands[5] = 0xffffffff;
        commands[6] = 4;
        commands[7] = 0xc03e1000;
        commands[8] = 0x68750777;
        asm volatile("sfence" ::: "memory");
        uint32_t size = 71 * sizeof(uint32_t);
        report("before", handle);
        result = sceGnmSubmitAndFlipCommandBuffers(1, &commands, &size, nullptr, nullptr,
                                                   handle, b, ORBIS_VIDEO_OUT_FLIP_VSYNC, 100 + b);
        std::printf("submit result=%x\n", result);
        report("submitted-GPU-gated", handle);
        sceKernelUsleep(100000);
        report("100ms-GPU-gated", handle);
        __atomic_store_n(gate, 1, __ATOMIC_SEQ_CST);
        asm volatile("sfence" ::: "memory");
        if (result != 0) {
            return 1;
        }
        sceGnmSubmitDone();
        bool completed = false;
        for (unsigned attempt = 0; attempt < 2000; ++attempt) {
            OrbisVideoOutFlipStatus status{};
            sceVideoOutGetFlipStatus(handle, &status);
            if (status.flipArg == 100 + b && status.numFlipPending == 0) {
                completed = true;
                break;
            }
            sceKernelUsleep(1000);
        }
        report(completed ? "completed" : "completion-timeout", handle);
        if (!completed) {
            return 1;
        }
    }
    sceKernelUsleep(3000000);
    sceVideoOutClose(handle);
    return 0;
}
