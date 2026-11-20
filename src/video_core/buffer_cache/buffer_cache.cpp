// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <chrono>
#include <cstring>
#include <magic_enum/magic_enum.hpp>
#include <xxhash.h>

#include "common/alignment.h"
#include "common/performance_scope.h"
#include "core/debug_state.h"
#include "core/memory.h"
#include "video_core/amdgpu/liverpool.h"
#include "video_core/buffer_cache/buffer.h"
#include "video_core/buffer_cache/buffer_cache.h"
#include "video_core/buffer_cache/memory_tracker.h"
#include "video_core/buffer_cache/region_definitions.h"
#include "video_core/renderer_vulkan/vk_graphics_pipeline.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_runtime.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/texture_cache/texture_cache.h"

#include <vk_mem_alloc.h>

namespace VideoCore {

static constexpr size_t GDS_BUFFER_SIZE = 64_KB;
static constexpr size_t NULL_INDEX_BUFFER_SIZE = 16;
static constexpr size_t STREAM_BUFFER_SIZE = 128_MB;
constexpr std::optional<u32> CmaskColorExpandedValue(u32 num_samples) {
    switch (num_samples) {
    case 1:
        return 0xFFFFFFFF;
    case 2:
        return 0xDDDDDDDD;
    case 4:
        return 0xEEEEEEEE;
    case 8:
        return 0xFFFFFFFF;
    default:
        return 0;
    }
}
static_assert(CmaskColorExpandedValue(1) == 0xFFFFFFFF);
static_assert(CmaskColorExpandedValue(2) == 0xDDDDDDDD);
static_assert(CmaskColorExpandedValue(4) == 0xEEEEEEEE);
static_assert(CmaskColorExpandedValue(8) == 0xFFFFFFFF);

static constexpr auto ARENA_USAGE =
    vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst |
    vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eStorageBuffer |
    vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eVertexBuffer |
    vk::BufferUsageFlagBits::eIndirectBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress;

std::optional<u32> FindMemoryType(const vk::PhysicalDeviceMemoryProperties& properties,
                                  vk::MemoryPropertyFlags wanted, u32 memory_type_bits) {
    for (u32 i = 0; i < properties.memoryTypeCount; ++i) {
        if (((memory_type_bits >> i) & 1) == 0) {
            continue;
        }
        const auto flags = properties.memoryTypes[i].propertyFlags;
        if ((flags & wanted) == wanted) {
            return i;
        }
    }
    return std::nullopt;
}

BufferCache::BufferCache(const Vulkan::Instance& instance_, Vulkan::Scheduler& scheduler_,
                         Vulkan::Runtime& runtime_, AmdGpu::Liverpool* liverpool_,
                         TextureCache& texture_cache_, PageManager& tracker)
    : instance{instance_}, scheduler{scheduler_}, runtime{runtime_},
      staging_pool{runtime_.GetStagingPool()}, liverpool{liverpool_},
      memory{Core::Memory::Instance()}, texture_cache{texture_cache_},
      memory_tracker{std::make_unique<MemoryTracker>(tracker)},
      stream_buffer{instance, scheduler, MemoryType::Stream, STREAM_BUFFER_SIZE},
      gds_buffer{instance, 0, GDS_BUFFER_SIZE, MemoryType::Stream, "GDS Buffer"},
      memory_semaphore{instance} {
    const vk::BufferCreateInfo probe_ci = {
        .flags =
            vk::BufferCreateFlagBits::eSparseBinding | vk::BufferCreateFlagBits::eSparseResidency,
        .size = ARENA_PAGE_SIZE,
        .usage = SupportedUsage(instance, ARENA_USAGE),
        .sharingMode = vk::SharingMode::eExclusive,
    };
    const vk::DeviceBufferMemoryRequirements req_info = {
        .pCreateInfo = &probe_ci,
    };
    const auto device = instance.GetDevice();
    const auto reqs = device.getBufferMemoryRequirements(req_info).memoryRequirements;
    block_size = Common::AlignUp(std::max<u64>(reqs.alignment, MIN_BLOCK_SIZE), reqs.alignment);
    ASSERT_MSG(std::popcount(block_size) == 1, "Sparse block size {} is not a power of 2",
               block_size);
    block_shift = std::bit_width(block_size) - 1;
    blocks_per_arena_page = ARENA_PAGE_SIZE / block_size;
    blocks_per_arena_page_shift = ARENA_PAGE_BITS - block_shift;
    arena_memory_type_index =
        FindMemoryType(instance.GetMemoryProperties(), vk::MemoryPropertyFlagBits::eDeviceLocal,
                       reqs.memoryTypeBits)
            .value();

    const u64 bda_pagetable_size =
        (blocks_per_arena_page * NUM_ARENA_PAGES) * sizeof(vk::DeviceAddress);
    fault_manager = std::make_unique<FaultManager>(instance, scheduler, *this, block_shift,
                                                   blocks_per_arena_page * NUM_ARENA_PAGES);
    bda_pagetable_buffer = std::make_unique<Buffer>(
        instance, 0, bda_pagetable_size, MemoryType::DeviceLocal, "BDA Page Table Buffer");
    runtime.FillBuffer(bda_pagetable_buffer.get(), 0u, bda_pagetable_size, 0u);
    if (!instance.IsMaintenance6Supported()) {
        null_index_buffer = std::make_unique<Buffer>(instance, 0, NULL_INDEX_BUFFER_SIZE,
                                                     MemoryType::DeviceLocal, "Null Index Buffer");
        runtime.FillBuffer(null_index_buffer.get(), 0u, NULL_INDEX_BUFFER_SIZE, 0u);
    }
}

BufferCache::~BufferCache() = default;

void BufferCache::TickFrame() {
    memory_tracker->TickFrame();
    if (std::exchange(fault_process_pending, false)) {
        fault_manager->ProcessFaultBuffer();
    }
}

void BufferCache::InvalidateMemory(VAddr device_addr, u64 size, bool assume_locks) {
    memory_tracker->InvalidateRegion(device_addr, size, [this, device_addr, size, assume_locks] {
        ReadMemory(device_addr, size, true, false, assume_locks);
    });
}

void BufferCache::UnmarkRegionAsGpuModified(VAddr device_addr, u64 size, bool is_write) {
    memory_tracker->UnmarkRegionAsGpuModified(device_addr, size, is_write);
    gpu_modified_ranges.Subtract(device_addr, size);
}

void BufferCache::ReadMemory(VAddr device_addr, u64 size, bool is_write, bool async,
                             bool assume_locks) {
    // GPU-modified ranges come as many small scattered islands,
    // so the download is widened to a window around the request
    static const u64 WindowSize = []() -> u64 {
        const char* value = std::getenv("SHADPS4_BUFFER_READBACK_KIB");
        if (!value) {
            return 512_KB;
        }
        const std::string_view text{value};
        u64 kib{};
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), kib);
        if (error != std::errc{} || end != text.data() + text.size() || kib < 64 ||
            kib > 4096 || !std::has_single_bit(kib)) {
            return 512_KB;
        }
        return kib * 1024;
    }();
    // SHADPS4_READBACK_SNAPSHOTS=0 serves every readback with the exact download path.
    static const bool no_snapshot = [] {
        const char* value = std::getenv("SHADPS4_READBACK_SNAPSHOTS");
        return value && value[0] == '0';
    }();
    const auto get_window = [this, device_addr, size](const Buffer*& arena) {
        const u32 first_block = device_addr >> block_shift;
        const u32 last_block = (device_addr + size - 1) >> block_shift;
        arena = GetArena(first_block, last_block);
        const VAddr arena_end = arena->cpu_addr + arena->size_bytes;
        const VAddr window_start =
            std::max<VAddr>(Common::AlignDown(device_addr, WindowSize), arena->cpu_addr);
        const VAddr window_end = std::min<VAddr>(
            std::max<VAddr>(window_start + WindowSize, device_addr + size), arena_end);
        return std::pair{window_start, window_end};
    };
    const auto flush_request = [this, device_addr, size, is_write, async, get_window] {
        const Buffer* arena{};
        const auto [window_start, window_end] = get_window(arena);
        DownloadMemory(arena, window_start, window_end - window_start, async);
        if (is_write) {
            memory_tracker->MarkRegionAsCpuModified(device_addr, size);
        }
    };
    const bool use_snapshots = is_write && !async && !no_snapshot;
    const VAddr page_addr = Common::AlignDown(device_addr, BYTES_PER_PAGE);
    if (assume_locks) {
        // Already on the GPU thread: serve the fault here.
        if (use_snapshots) {
            const Buffer* arena{};
            const auto [window_start, window_end] = get_window(arena);
            SnapshotRead read;
            if (PrepareSnapshotRead(device_addr, window_start, window_end, read)) {
                WriteSnapshotRead(read);
                if (CommitSnapshotRead(read, page_addr)) {
                    memory_tracker->MarkRegionAsCpuModified(device_addr, size);
                    return;
                }
            }
        }
        flush_request();
        return;
    }
    if (use_snapshots) {
        // The GPU thread picks the snapshots, this guest thread waits for them, so the GPU
        // thread keeps feeding the GPU in the meantime. When the GPU writes the page again
        // while this thread waits, the snapshot path is tried again before the exact path.
        static constexpr int MaxAttempts = 3;
        for (int attempt = 0; attempt < MaxAttempts; ++attempt) {
            SnapshotRead read;
            bool prepared = false;
            liverpool->SendCommand<true>([&] {
                const Buffer* arena{};
                const auto [window_start, window_end] = get_window(arena);
                prepared = PrepareSnapshotRead(device_addr, window_start, window_end, read);
                if (!prepared) {
                    flush_request();
                }
            });
            if (!prepared) {
                return;
            }
            WriteSnapshotRead(read);
            bool committed = false;
            liverpool->SendCommand<true>([&] {
                committed = CommitSnapshotRead(read, page_addr);
                if (committed) {
                    memory_tracker->MarkRegionAsCpuModified(device_addr, size);
                } else if (attempt + 1 == MaxAttempts) {
                    flush_request();
                }
            });
            if (committed) {
                return;
            }
            PERFORMANCE_SCOPE("ReadbackCommitRetry");
        }
        return;
    }
    liverpool->SendCommand<true>(std::move(flush_request));
}

void BufferCache::DownloadMemory(const Buffer* arena, VAddr device_addr, u64 size, bool async) {
    PERFORMANCE_SCOPE("DownloadBufferMemory");
    boost::container::small_vector<vk::BufferCopy, 1> copies;
    u64 total_size_bytes = 0;
    const VAddr arena_base = arena->cpu_addr;
    memory_tracker->ForEachDownloadRange<false>(device_addr, size, [&](u64 address, u64 size) {
        const auto add_download = [&](VAddr start, VAddr end) {
            const u64 new_offset = start - arena_base;
            const u64 new_size = end - start;
            copies.push_back(vk::BufferCopy{
                .srcOffset = new_offset,
                .dstOffset = total_size_bytes,
                .size = new_size,
            });
            // Align up to avoid cache conflicts
            constexpr u64 align = 64ULL;
            constexpr u64 mask = ~(align - 1ULL);
            total_size_bytes += (new_size + align - 1) & mask;
        };
        gpu_modified_ranges.ForEachInRange(address, size, add_download);
        gpu_modified_ranges.Subtract(address, size);
    });
    if (total_size_bytes == 0) {
        return;
    }
    const auto download =
        staging_pool.Request(total_size_bytes, VideoCore::MemoryType::HostCached, 16, async);
    for (auto& copy : copies) {
        copy.dstOffset += download.offset;
    }
    runtime.CopyBuffer(arena, download.buffer, copies);

    const auto write_data = [this, arena_base, device_addr, size, download, async,
                             copies = std::move(copies)] {
        download.Invalidate();
        for (const auto& copy : copies) {
            auto* dst_addr = std::bit_cast<u8*>(arena_base + copy.srcOffset);
            memory->TryWriteBacking(dst_addr, download.mapped + (copy.dstOffset - download.offset),
                                    copy.size);
        }
        memory_tracker->UnmarkRegionAsGpuModified(device_addr, size, false);

        if (async) {
            staging_pool.FreeDeferred(download);
        }
    };

    if (async) {
        scheduler.DeferOperation(std::move(write_data));
    } else {
        {
            PERFORMANCE_SCOPE("WaitBufferDownload");
            scheduler.Finish();
        }
        write_data();
    }
}

std::pair<const Buffer*, u64> BufferCache::ObtainBuffer(VAddr device_addr, u32 size,
                                                        ObtainBufferFlags flags) {
    const bool is_written = True(flags & ObtainBufferFlags::IsWritten);
    const bool is_texel_buffer = True(flags & ObtainBufferFlags::IsTexelBuffer);
    const bool skip_stream_buffer = True(flags & ObtainBufferFlags::IgnoreStreamBuffer);
    // For read-only buffers use device local stream buffer to reduce renderpass breaks.
    if (!is_written && !skip_stream_buffer && size <= STREAM_THRESHOLD &&
        !IsRegionGpuModified(device_addr, size)) {
        const auto [data, offset] = stream_buffer.Map(size, instance.UniformMinAlignment());
        memory->CopySparseMemory(device_addr, data, size);
        stream_buffer.Commit();
        return {&stream_buffer, offset};
    }
    const u64 first_block = device_addr >> block_shift;
    const u64 last_block = (device_addr + size - 1) >> block_shift;
    const auto* arena = GetArena(first_block, last_block);
    EnsureResident(arena, first_block, last_block);
    if (is_written) {
        texture_cache.MarkAsMaybeGpuDirty(device_addr, size);
    }
    SynchronizeMemory(arena, device_addr, size, is_written, is_texel_buffer);
    if (is_written) {
        gpu_modified_ranges.Add(device_addr, size);
        NoteGpuWrite(device_addr, size);
    }
    return {arena, arena->Offset(device_addr)};
}

std::pair<const Buffer*, u64> BufferCache::ObtainBufferForImage(VAddr device_addr, u32 size) {
    if (IsRegionGpuModified(device_addr, size)) {
        return ObtainBuffer(device_addr, size);
    }
    const auto staging = staging_pool.Request(size, VideoCore::MemoryType::HostUncached,
                                              instance.StorageMinAlignment());
    memory->CopySparseMemory(device_addr, staging.mapped, staging.size);
    staging.Flush();
    return {staging.buffer, staging.offset};
}

bool BufferCache::IsRegionCpuModified(VAddr addr, size_t size) {
    return memory_tracker->IsRegionCpuModified(addr, size);
}

bool BufferCache::IsRegionGpuModified(VAddr addr, size_t size) {
    return memory_tracker->IsRegionGpuModified(addr, size);
}

void BufferCache::MarkRegionAsGpuModified(VAddr addr, size_t size) {
    texture_cache.MarkAsMaybeGpuDirty(addr, size);
    memory_tracker->MarkRegionAsGpuModified(addr, size);
    gpu_modified_ranges.Add(addr, size);
    // Callers mark the region after recording the command that writes it.
    NoteGpuWrite(addr, size);
    SealGpuWrites();
}

void BufferCache::ReadEdgeImagePages(const Image& image) {
    // May happen that after downloading the image and invalidating region,
    // that there were GPU modified ranges that are lost due to CPU reuploading.
    // This doesn't change tracker state and it is spected to call DownloadImageMemory after this.
    const VAddr image_addr = image.info.guest_address;
    const u64 image_size = image.info.guest_size;
    const VAddr image_end = image_addr + image_size;
    const VAddr page_start = PageManager::GetPageAddr(image_addr);
    const VAddr page_end = PageManager::GetNextPageAddr(image_end - 1);
    boost::container::small_vector<vk::BufferCopy, 2> copies;
    u64 total_size_bytes = 0;
    const auto [buffer, offset] = ObtainBuffer(page_start, page_end - page_start);
    const auto add_download = [&](VAddr start, VAddr end) {
        const u64 new_offset = start - buffer->CpuAddr();
        const u64 new_size = end - start;
        copies.push_back(vk::BufferCopy{
            .srcOffset = new_offset,
            .dstOffset = total_size_bytes,
            .size = new_size,
        });
        // Align up to avoid cache conflicts
        constexpr u64 align = 64ULL;
        constexpr u64 mask = ~(align - 1ULL);
        total_size_bytes += (new_size + align - 1) & mask;
    };
    gpu_modified_ranges.ForEachInRange(page_start, image_addr - page_start, add_download);
    gpu_modified_ranges.ForEachInRange(image_end, page_end - image_end, add_download);
    gpu_modified_ranges.Subtract(page_start, page_end - page_start);
    if (total_size_bytes == 0) {
        return;
    }
    const auto download =
        staging_pool.Request(total_size_bytes, VideoCore::MemoryType::HostCached, 16, true);
    for (auto& copy : copies) {
        // Modify copies to have the staging offset in mind
        copy.dstOffset += download.offset;
    }
    scheduler.EndRendering();
    runtime.CopyBuffer(buffer, download.buffer, copies);
    scheduler.DeferOperation(
        [this, base = buffer->CpuAddr(), download, copies = std::move(copies)]() {
            download.Invalidate();
            auto* memory = Core::Memory::Instance();
            for (const auto& copy : copies) {
                const VAddr copy_device_addr = base + copy.srcOffset;
                const u64 dst_offset = copy.dstOffset - download.offset;
                memory->TryWriteBacking(std::bit_cast<u8*>(copy_device_addr),
                                        download.mapped + dst_offset, copy.size);
            }
            staging_pool.FreeDeferred(download);
        });
}

void BufferCache::SynchronizeDmaBuffers() {
    fault_process_pending = true;
    for (const auto& range : resident_ranges) {
        const u64 page = range.start >> (ARENA_PAGE_BITS - block_shift);
        const VAddr device_addr = range.start << block_shift;
        const u64 size = (range.end - range.start) << block_shift;
        SynchronizeMemory(address_space[page], device_addr, size, false, false);
    }
}

const Buffer* BufferCache::GetArena(u64 first_block, u64 last_block) {
    const u64 first_page = first_block >> blocks_per_arena_page_shift;
    const u64 last_page = last_block >> blocks_per_arena_page_shift;
    ASSERT_MSG(last_page - first_page <= 1,
               "Buffer request cannot span more than two VA arena pages");

    const auto* first_arena = address_space[first_page];
    const auto* last_arena = address_space[last_page];
    if (first_arena == last_arena) {
        if (!first_arena) {
            const u64 base_block = Common::AlignDownPow2<u64>(first_block, blocks_per_arena_page);
            const u64 num_pages = last_page - first_page + 1;
            const auto* new_arena =
                &arenas.emplace_back(instance, base_block << block_shift,
                                     num_pages << ARENA_PAGE_BITS, MemoryType::Sparse);
            address_space[first_page] = new_arena;
            address_space[last_page] = new_arena;
        }
        return address_space[first_page];
    }

    LOG_WARNING(Render, "Migrating arena");

    const u64 first_addr = first_arena ? first_arena->cpu_addr : (first_page << ARENA_PAGE_BITS);
    const u64 first_size = first_arena ? first_arena->size_bytes : ARENA_PAGE_SIZE;
    const u64 last_size = last_arena ? last_arena->size_bytes : ARENA_PAGE_SIZE;

    const u64 base_block = first_addr >> block_shift;
    const u64 total_size = first_size + last_size;
    const u64 end_block = (first_addr + total_size) >> block_shift;
    auto* new_arena = &arenas.emplace_back(instance, first_addr, total_size, MemoryType::Sparse);
    auto* bind = BindsForArena(new_arena);
    resident_ranges.ForEachInRange(base_block, end_block, [&](const Backing& backing) {
        const u64 start = std::max(base_block, backing.start);
        const u64 end = std::min(end_block, backing.end);
        bind->binds.push_back(vk::SparseMemoryBind{
            .resourceOffset = (start - base_block) << block_shift,
            .size = (end - start) << block_shift,
            .memory = backing.memory,
            .memoryOffset = (backing.offset + start - backing.start) << block_shift,
        });
    });

    u64 base_page = first_addr >> ARENA_PAGE_BITS;
    for (u32 page = 0; page < (first_size >> ARENA_PAGE_BITS); ++page) {
        address_space[base_page + page] = new_arena;
    }
    base_page = last_page;
    for (u32 page = 0; page < (last_size >> ARENA_PAGE_BITS); ++page) {
        address_space[base_page + page] = new_arena;
    }
    return new_arena;
}

void BufferCache::EnsureResident(const Buffer* arena, u64 first_block, u64 last_block) {
    u32 resident_blocks{};
    IntervalList bind_ranges;
    resident_ranges.ForEachGap(first_block, last_block + 1, [&](u64 start, u64 end) {
        resident_blocks += end - start;
        bind_ranges.Add({start, end});
    });

    if (bind_ranges.Empty()) {
        return;
    }

    const bool has_bda = instance.IsBufferDeviceAddressSupported();
    const vk::MemoryAllocateFlagsInfo alloc_flags = {
        .flags = vk::MemoryAllocateFlagBits::eDeviceAddress,
    };
    const vk::MemoryAllocateInfo alloc_info = {
        .pNext = has_bda ? &alloc_flags : nullptr,
        .allocationSize = resident_blocks << block_shift,
        .memoryTypeIndex = arena_memory_type_index,
    };
    const auto device_memory = Vulkan::Check(instance.GetDevice().allocateMemory(alloc_info));

    boost::container::small_vector<vk::BufferCopy, 8> copies;
    const auto staging =
        staging_pool.Request(resident_blocks * sizeof(vk::DeviceAddress), MemoryType::HostUncached);

    u64 memory_offset{};
    ArenaBinds* binds = BindsForArena(arena);
    auto* bda_addrs = reinterpret_cast<vk::DeviceAddress*>(staging.mapped);
    u64 offset = staging.offset;
    for (const auto& range : bind_ranges) {
        Backing backing;
        backing.start = range.start;
        backing.end = range.end;
        backing.memory = device_memory;
        backing.offset = memory_offset >> block_shift;
        resident_ranges.Add(backing);

        LOG_INFO(Render, "Making range start={}, end={} resident", backing.start, backing.end);

        const auto& bind = binds->binds.emplace_back(vk::SparseMemoryBind{
            .resourceOffset = (range.start << block_shift) - arena->cpu_addr,
            .size = (range.end - range.start) << block_shift,
            .memory = device_memory,
            .memoryOffset = memory_offset,
        });
        memory_offset += bind.size;

        if (!has_bda) {
            continue;
        }
        for (u32 block = 0; block < bind.size; block += block_size) {
            *(bda_addrs++) = arena->BufferDeviceAddress() + bind.resourceOffset + block;
        }
        const u64 copy_size = (backing.end - backing.start) * sizeof(vk::DeviceAddress);
        copies.emplace_back(offset, backing.start * sizeof(vk::DeviceAddress), copy_size);
        offset += copy_size;
    }

    if (has_bda) {
        staging.Flush();
        runtime.CopyBuffer(staging.buffer, bda_pagetable_buffer.get(), copies);
    }
}

bool BufferCache::SynchronizeMemory(const Buffer* arena, VAddr device_addr, u32 size,
                                    bool is_written, bool is_texel_buffer) {
    PERFORMANCE_SCOPE("SynchronizeBufferMemory");
    boost::container::small_vector<vk::BufferCopy, 4> copies;
    // Where each copy reads from: guest memory (nullptr) or a hot page's checked contents.
    boost::container::small_vector<const u8*, 4> sources;
    size_t total_size_bytes{};
    const auto add_range = [&](u64 addr, u64 size, const u8* source) {
        copies.emplace_back(total_size_bytes, addr, size);
        sources.push_back(source);
        total_size_bytes += size;
    };
    if (is_written) {
        memory_tracker->ForEachUploadRange(device_addr, size, true, [&](u64 addr, u64 size) {
            add_range(addr, size, nullptr);
        });
    } else {
        memory_tracker->ForEachUploadRange(
            device_addr, size, [&](u64 addr, u64 size) { add_range(addr, size, nullptr); },
            [&](VAddr page_addr, bool fresh) {
                bool keep_hot = true;
                if (const auto* page = CheckHotPage(arena, page_addr, fresh, keep_hot)) {
                    add_range(page_addr, page->data.size(), page->data.data());
                }
                return keep_hot;
            });
    }
    if (!copies.empty()) {
        const auto staging = staging_pool.Request(total_size_bytes, MemoryType::HostUncached);
        for (size_t i = 0; i < copies.size(); ++i) {
            auto& copy = copies[i];
            if (sources[i]) {
                std::memcpy(staging.mapped + copy.srcOffset, sources[i], copy.size);
            } else {
                memory->CopySparseMemory(copy.dstOffset, staging.mapped + copy.srcOffset,
                                         copy.size);
            }
            copy.srcOffset += staging.offset;
            copy.dstOffset -= arena->cpu_addr;
        }
        staging.Flush();
        runtime.CopyBuffer(staging.buffer, arena, copies);
    }
    if (is_texel_buffer && !is_written) {
        return SynchronizeMemoryFromImage(arena, device_addr, size);
    }
    return false;
}

void BufferCache::NoteGpuWrite(VAddr device_addr, u64 size) {
    if (num_readback_pages.load(std::memory_order_relaxed) == 0) {
        return;
    }
    std::scoped_lock lk{readback_mutex};
    const VAddr end = device_addr + size;
    for (auto it = readback_pages.lower_bound(Common::AlignDown(device_addr, BYTES_PER_PAGE));
         it != readback_pages.end() && it->first < end; ++it) {
        if (it->second.state != ReadbackState::Unsealed) {
            it->second.state = ReadbackState::Unsealed;
            readback_unsealed.push_back(it->first);
        }
    }
}

void BufferCache::SealGpuWrites() {
    if (num_readback_pages.load(std::memory_order_relaxed) == 0) {
        return;
    }
    std::scoped_lock lk{readback_mutex};
    for (const VAddr page_addr : readback_unsealed) {
        const auto it = readback_pages.find(page_addr);
        if (it != readback_pages.end() && it->second.state == ReadbackState::Unsealed) {
            it->second.state = ReadbackState::Pending;
            readback_pending.push_back(page_addr);
        }
    }
    readback_unsealed.clear();
}

bool BufferCache::HasPendingReadbackSnapshots() {
    if (num_readback_pages.load(std::memory_order_relaxed) == 0) {
        return false;
    }
    std::scoped_lock lk{readback_mutex};
    return !readback_pending.empty();
}

void BufferCache::RecordReadbackSnapshots() {
    // Pages that have not needed a readback for this many frames stop being copied.
    static constexpr u32 ReadbackIdleFrames = 1200;
    if (num_readback_pages.load(std::memory_order_relaxed) == 0) {
        return;
    }
    PERFORMANCE_SCOPE("RecordReadbackSnapshots");
    std::scoped_lock lk{readback_mutex};
    // The submission being recorded signals the tick before the current one.
    const u64 tick = scheduler.CurrentTick() - 1;
    boost::container::small_vector<std::pair<const Buffer*, vk::BufferCopy>, 32> copies;
    for (const VAddr page_addr : readback_pending) {
        const auto it = readback_pages.find(page_addr);
        if (it == readback_pages.end() || it->second.state != ReadbackState::Pending) {
            continue;
        }
        const u64 block = page_addr >> block_shift;
        const auto* arena = GetArena(block, block);
        copies.emplace_back(arena, vk::BufferCopy{
                                       .srcOffset = arena->Offset(page_addr),
                                       .dstOffset = u64(it->second.slot) * BYTES_PER_PAGE,
                                       .size = BYTES_PER_PAGE,
                                   });
        it->second.state = ReadbackState::Snapshotted;
        it->second.snapshot_tick = tick;
    }
    readback_pending.clear();
    for (size_t i = 0; i < copies.size();) {
        boost::container::small_vector<vk::BufferCopy, 32> batch;
        const Buffer* arena = copies[i].first;
        for (; i < copies.size() && copies[i].first == arena; ++i) {
            batch.push_back(copies[i].second);
        }
        runtime.CopyBuffer(arena, readback_snapshots.get(), batch);
    }
    if (++readback_submits % 512 != 0) {
        return;
    }
    const u32 frame = memory_tracker->CurrentFrame();
    for (auto it = readback_pages.begin(); it != readback_pages.end();) {
        const auto& page = it->second;
        if (frame - page.last_fault_frame > ReadbackIdleFrames &&
            page.state != ReadbackState::Unsealed && page.state != ReadbackState::Pending) {
            readback_free_slots.push_back(page.slot);
            it = readback_pages.erase(it);
        } else {
            ++it;
        }
    }
    num_readback_pages.store(readback_pages.size(), std::memory_order_relaxed);
}

bool BufferCache::PrepareSnapshotRead(VAddr device_addr, VAddr window_start, VAddr window_end,
                                      SnapshotRead& read) {
    // Up to this many pages (16 MiB of host memory) are kept as readback pages.
    static constexpr u32 ReadbackSlots = 4096;
    const VAddr page_addr = Common::AlignDown(device_addr, BYTES_PER_PAGE);
    bool pending = false;
    {
        std::scoped_lock lk{readback_mutex};
        const auto it = readback_pages.find(page_addr);
        pending = it != readback_pages.end() && it->second.state == ReadbackState::Pending;
    }
    if (pending) {
        // Written in the open command buffer: submitting it records the snapshot.
        PERFORMANCE_SCOPE("ReadbackSubmitPending");
        scheduler.Flush();
    }
    {
        std::scoped_lock lk{readback_mutex};
        const u32 frame = memory_tracker->CurrentFrame();
        auto it = readback_pages.find(page_addr);
        if (it == readback_pages.end()) {
            // First readback of this page: take the exact path and copy the page from now on.
            PERFORMANCE_SCOPE("ReadbackMissNew");
            if (!readback_snapshots) {
                readback_snapshots = std::make_unique<Buffer>(
                    instance, 0, u64(ReadbackSlots) * BYTES_PER_PAGE, MemoryType::HostCached,
                    "ReadbackSnapshots");
                for (u32 slot = ReadbackSlots; slot > 0; --slot) {
                    readback_free_slots.push_back(slot - 1);
                }
            }
            if (!readback_free_slots.empty()) {
                readback_pages.emplace(page_addr, ReadbackPage{
                                                      .slot = readback_free_slots.back(),
                                                      .state = ReadbackState::Invalid,
                                                      .last_fault_frame = frame,
                                                      .snapshot_tick = 0,
                                                  });
                readback_free_slots.pop_back();
                num_readback_pages.store(readback_pages.size(), std::memory_order_relaxed);
            }
            return false;
        }
        it->second.last_fault_frame = frame;
        if (it->second.state != ReadbackState::Snapshotted) {
            PERFORMANCE_SCOPE("ReadbackMissNoSnapshot");
            return false;
        }
        // Serve the other readback pages of the download window too, as the exact path would.
        for (auto jt = readback_pages.lower_bound(window_start);
             jt != readback_pages.end() && jt->first < window_end; ++jt) {
            if (jt->second.state != ReadbackState::Snapshotted ||
                !memory_tracker->IsRegionGpuModified(jt->first, BYTES_PER_PAGE)) {
                continue;
            }
            auto& page = read.pages.emplace_back();
            page.addr = jt->first;
            page.slot = jt->second.slot;
            page.snapshot_tick = jt->second.snapshot_tick;
            gpu_modified_ranges.ForEachInRange(jt->first, BYTES_PER_PAGE,
                                               [&](VAddr start, VAddr end) {
                                                   page.ranges.emplace_back(start, end);
                                               });
            read.tick = std::max(read.tick, jt->second.snapshot_tick);
        }
    }
    if (read.pages.empty()) {
        return false;
    }
    PERFORMANCE_SCOPE("ReadbackSnapshotHit");
    if (!scheduler.IsFree(read.tick)) {
        // Hand the recorded work to the GPU so it stays busy while the snapshot is awaited.
        scheduler.Flush();
    }
    return true;
}

void BufferCache::WriteSnapshotRead(const SnapshotRead& read) {
    auto* semaphore = scheduler.GetWorkSemaphore();
    if (!semaphore->IsFree(read.tick)) {
        PERFORMANCE_SCOPE("WaitReadbackSnapshot");
        semaphore->Wait(read.tick);
    }
    for (const auto& page : read.pages) {
        const u64 slot_offset = u64(page.slot) * BYTES_PER_PAGE;
        readback_snapshots->Invalidate(slot_offset, BYTES_PER_PAGE);
        const u8* data = readback_snapshots->mapped_data.data() + slot_offset;
        for (const auto& [start, end] : page.ranges) {
            memory->TryWriteBacking(std::bit_cast<u8*>(start), data + (start - page.addr),
                                    end - start);
        }
    }
}

bool BufferCache::CommitSnapshotRead(const SnapshotRead& read, VAddr page_addr) {
    bool served = false;
    std::scoped_lock lk{readback_mutex};
    for (const auto& page : read.pages) {
        const auto it = readback_pages.find(page.addr);
        // A page the GPU wrote again since its snapshot stays GPU modified.
        if (it == readback_pages.end() || it->second.state != ReadbackState::Snapshotted ||
            it->second.snapshot_tick != page.snapshot_tick) {
            continue;
        }
        gpu_modified_ranges.Subtract(page.addr, BYTES_PER_PAGE);
        memory_tracker->UnmarkRegionAsGpuModified(page.addr, BYTES_PER_PAGE, false);
        served |= page.addr == page_addr;
    }
    return served;
}

const BufferCache::HotPage* BufferCache::CheckHotPage(const Buffer* owner, VAddr page_addr,
                                                     bool fresh,
                                                     bool& keep_hot) {
    // A hot page that has not changed for this many frames is write-protected again.
    static constexpr u32 HotPageIdleFrames = 120;
    PERFORMANCE_SCOPE("HotPageCheck");
    const u32 frame = memory_tracker->CurrentFrame();
    std::scoped_lock lk{hot_pages_mutex};
    auto& page = hot_pages[page_addr];
    if (!page) {
        page = std::make_unique<HotPage>();
        fresh = true;
    } else if (!fresh && page->last_check_frame == frame) {
        // Checked once in this guest submission already: the guest writes per-frame data
        // before it submits, so comparing again within the same submission finds nothing new.
        return nullptr;
    }
    page->last_check_frame = frame;
    std::array<u8, BYTES_PER_PAGE> current;
    memory->CopySparseMemory(page_addr, current.data(), current.size());
    if (fresh || std::memcmp(page->data.data(), current.data(), current.size()) != 0) {
        PERFORMANCE_SCOPE("HotPageUpload");
        page->data = current;
        page->last_change_frame = frame;
        return page.get();
    }
    if (frame - page->last_change_frame > HotPageIdleFrames) {
        hot_pages.erase(page_addr);
        keep_hot = false;
    }
    return nullptr;
}

bool BufferCache::SynchronizeMemoryFromImage(const Buffer* arena, VAddr device_addr, u32 size) {
    if (const auto meta = texture_cache.GetMetaInfo(device_addr)) {
        if (meta->type == TextureCache::MetaType::HTile) {
            static constexpr u32 ZmaskUncompressed = 0xf;
            runtime.FillBuffer(arena, arena->Offset(device_addr), size, ZmaskUncompressed);
            return true;
        } else if (meta->type == TextureCache::MetaType::CMask) {
            const auto expanded_value = CmaskColorExpandedValue(meta->num_samples);
            if (!expanded_value) {
                LOG_WARNING(Render_Vulkan,
                            "Unsupported CMask sample count: address={:#x}, size={:#x}, "
                            "samples={}, owner={}:{}",
                            device_addr, size, meta->num_samples, meta->owner_id.index,
                            meta->owner_uid);
                return false;
            }
            runtime.FillBuffer(arena, arena->Offset(device_addr), size, *expanded_value);
            return true;
        } else {
            LOG_WARNING(Render_Vulkan, "Unhandled metadata type {}",
                        magic_enum::enum_name(meta->type));
        }
    }
    const ImageId image_id = texture_cache.FindImageFromRange(device_addr, size);
    if (!image_id) {
        return false;
    }
    Image& image = texture_cache.GetImage(image_id);
    ASSERT_MSG(device_addr == image.info.guest_address,
               "Texel buffer aliases image subresources {:x} : {:x}", device_addr,
               image.info.guest_address);
    const u64 arena_offset = arena->Offset(device_addr);
    boost::container::small_vector<vk::BufferImageCopy, 8> buffer_copies;
    for (u32 mip = 0; mip < image.info.resources.levels; mip++) {
        const auto& mip_info = image.info.mips_layout[mip];
        const u32 width = std::max(image.info.size.width >> mip, 1u);
        const u32 height = std::max(image.info.size.height >> mip, 1u);
        const u32 depth = std::max(image.info.size.depth >> mip, 1u);
        if (arena_offset + mip_info.offset + mip_info.size > arena->size_bytes) {
            break;
        }
        buffer_copies.push_back(vk::BufferImageCopy{
            .bufferOffset = mip_info.offset,
            .bufferRowLength = mip_info.pitch,
            .bufferImageHeight = mip_info.height,
            .imageSubresource{
                .aspectMask = image.aspect_mask & ~vk::ImageAspectFlagBits::eStencil,
                .mipLevel = mip,
                .baseArrayLayer = 0,
                .layerCount = image.info.resources.layers,
            },
            .imageOffset = {0, 0, 0},
            .imageExtent = {width, height, depth},
        });
    }
    if (buffer_copies.empty()) {
        return false;
    }
    auto& tile_manager = texture_cache.GetTileManager();
    tile_manager.TileImage(image, buffer_copies, arena, arena_offset);
    return true;
}

void BufferCache::SubmitPendingArenaBinds(Vulkan::SubmitInfo& info) {
    if (pending_binds.empty()) {
        return;
    }

    std::vector<vk::SparseBufferMemoryBindInfo> buffer_binds;
    buffer_binds.reserve(pending_binds.size());

    for (const auto& binds : pending_binds) {
        buffer_binds.emplace_back(vk::SparseBufferMemoryBindInfo{
            .buffer = binds.arena->Handle(),
            .bindCount = static_cast<u32>(binds.binds.size()),
            .pBinds = binds.binds.data(),
        });
    }

    const u64 signal_tick = memory_semaphore.NextTick();
    const auto signal_sema = memory_semaphore.Handle();

    const vk::TimelineSemaphoreSubmitInfo timeline_si = {
        .signalSemaphoreValueCount = 1u,
        .pSignalSemaphoreValues = &signal_tick,
    };

    const vk::BindSparseInfo sparse_info = {
        .pNext = &timeline_si,
        .bufferBindCount = static_cast<u32>(buffer_binds.size()),
        .pBufferBinds = buffer_binds.data(),
        .signalSemaphoreCount = 1u,
        .pSignalSemaphores = &signal_sema,
    };

    info.AddWait(signal_sema, signal_tick);
    auto submit_result = instance.GetGraphicsQueue().bindSparse(sparse_info);
    ASSERT_MSG(submit_result != vk::Result::eErrorDeviceLost, "Device lost during submit");

    pending_binds.clear();
}

} // namespace VideoCore
