// SPDX-FileCopyrightText: Copyright 2026 CrossCrease
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstring>
#include <fstream>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>
#include <miniz.h>
#include "common/logging/log.h"
#include "core/file_sys/file_patches.h"

namespace Core::FileSys::FilePatches {

namespace {

constexpr std::array<char, 8> ChunkzipMagic{'c', 'h', 'u', 'n', 'k', 'z', 'i', 'p'};
constexpr u64 ChunkzipFirstBlock = 0x28;
constexpr u32 BlockDeflate = 1;

struct ChunkzipText {
    std::string file;
    u64 offset;
    std::vector<std::pair<std::string, std::string>> replacements;
};

struct Overlay {
    u64 start;
    std::vector<u8> bytes;
};

std::atomic_bool g_any_patch{false};
std::mutex g_mutex;
std::vector<ChunkzipText> g_patches;
std::unordered_map<std::string, std::vector<Overlay>> g_overlays;

std::string Lower(std::string text) {
    std::ranges::transform(text, text.begin(), [](unsigned char c) { return std::tolower(c); });
    return text;
}

u32 ReadBigEndian(const u8* data) {
    return (u32{data[0]} << 24) | (u32{data[1]} << 16) | (u32{data[2]} << 8) | u32{data[3]};
}

void WriteBigEndian(u8* data, u32 value) {
    data[0] = static_cast<u8>(value >> 24);
    data[1] = static_cast<u8>(value >> 16);
    data[2] = static_cast<u8>(value >> 8);
    data[3] = static_cast<u8>(value);
}

bool ReadAt(std::ifstream& stream, u64 offset, void* data, u64 size) {
    stream.seekg(static_cast<std::streamoff>(offset));
    stream.read(static_cast<char*>(data), static_cast<std::streamsize>(size));
    return static_cast<u64>(stream.gcount()) == size;
}

std::optional<Overlay> BuildChunkzip(const std::filesystem::path& host_path,
                                     const ChunkzipText& patch) {
    std::ifstream stream(host_path, std::ios::binary);
    std::array<u8, ChunkzipFirstBlock + 8> header{};
    if (!stream || !ReadAt(stream, patch.offset, header.data(), header.size()) ||
        std::memcmp(header.data(), ChunkzipMagic.data(), ChunkzipMagic.size()) != 0) {
        LOG_WARNING(Kernel_Fs, "File patch {:#x} in {}: no chunkzip record there", patch.offset,
                    patch.file);
        return std::nullopt;
    }
    const u32 uncompressed_size = ReadBigEndian(&header[12]);
    const u32 block_size = ReadBigEndian(&header[16]);
    const u32 block_count = ReadBigEndian(&header[20]);
    const u32 compressed_size = ReadBigEndian(&header[ChunkzipFirstBlock]);
    const u32 block_type = ReadBigEndian(&header[ChunkzipFirstBlock + 4]);
    if (block_count != 1 || block_type != BlockDeflate || uncompressed_size > block_size) {
        LOG_WARNING(Kernel_Fs, "File patch {:#x} in {}: unsupported chunkzip layout",
                    patch.offset, patch.file);
        return std::nullopt;
    }

    std::vector<u8> compressed(compressed_size);
    std::vector<u8> text(uncompressed_size);
    if (!ReadAt(stream, patch.offset + ChunkzipFirstBlock + 8, compressed.data(),
                compressed.size()) ||
        tinfl_decompress_mem_to_mem(text.data(), text.size(), compressed.data(),
                                    compressed.size(), 0) != text.size()) {
        LOG_WARNING(Kernel_Fs, "File patch {:#x} in {}: chunk does not inflate", patch.offset,
                    patch.file);
        return std::nullopt;
    }
    for (const auto& [find, replace] : patch.replacements) {
        const auto found = std::search(text.begin(), text.end(), find.begin(), find.end());
        if (found == text.end()) {
            LOG_WARNING(Kernel_Fs, "File patch {:#x} in {}: text not found: {}", patch.offset,
                        patch.file, find);
            return std::nullopt;
        }
        std::ranges::copy(replace, found);
    }

    std::vector<u8> recompressed(compressed_size);
    const int flags = tdefl_create_comp_flags_from_zip_params(MZ_UBER_COMPRESSION,
                                                              -MZ_DEFAULT_WINDOW_BITS,
                                                              MZ_DEFAULT_STRATEGY);
    const size_t new_size = tdefl_compress_mem_to_mem(recompressed.data(), recompressed.size(),
                                                      text.data(), text.size(), flags);
    if (new_size == 0) {
        LOG_WARNING(Kernel_Fs, "File patch {:#x} in {}: patched chunk does not fit in {} bytes",
                    patch.offset, patch.file, compressed_size);
        return std::nullopt;
    }

    Overlay overlay{.start = patch.offset + ChunkzipFirstBlock,
                    .bytes = std::vector<u8>(8 + compressed_size)};
    WriteBigEndian(&overlay.bytes[0], static_cast<u32>(new_size));
    WriteBigEndian(&overlay.bytes[4], BlockDeflate);
    std::memcpy(&overlay.bytes[8], recompressed.data(), new_size);
    LOG_INFO(Kernel_Fs, "File patch {:#x} in {}: {} replacement(s), chunk {} -> {} bytes",
             patch.offset, patch.file, patch.replacements.size(), compressed_size, new_size);
    return overlay;
}

const std::vector<Overlay>& OverlaysFor(const std::filesystem::path& host_path) {
    const auto key = host_path.generic_string();
    if (const auto it = g_overlays.find(key); it != g_overlays.end()) {
        return it->second;
    }
    auto& overlays = g_overlays[key];
    const auto lower_path = Lower(key);
    for (const auto& patch : g_patches) {
        const auto suffix = "/" + Lower(patch.file);
        if (lower_path.size() >= suffix.size() && lower_path.ends_with(suffix)) {
            if (auto overlay = BuildChunkzip(host_path, patch)) {
                overlays.push_back(std::move(*overlay));
            }
        }
    }
    return overlays;
}

} // namespace

void AddChunkzipText(const std::string& file, u64 offset, const std::string& find,
                     const std::string& replace) {
    if (find.empty() || find.size() != replace.size()) {
        LOG_ERROR(Kernel_Fs, "File patch {:#x} in {}: find and replace must have the same length",
                  offset, file);
        return;
    }
    std::scoped_lock lock{g_mutex};
    const auto it = std::ranges::find_if(g_patches, [&](const ChunkzipText& patch) {
        return patch.offset == offset && patch.file == file;
    });
    if (it != g_patches.end()) {
        it->replacements.emplace_back(find, replace);
    } else {
        g_patches.push_back({file, offset, {{find, replace}}});
    }
    g_overlays.clear();
    g_any_patch = true;
}

void Apply(const std::filesystem::path& host_path, u64 position, u8* data, u64 size) {
    if (!g_any_patch || size == 0) {
        return;
    }
    std::scoped_lock lock{g_mutex};
    for (const auto& overlay : OverlaysFor(host_path)) {
        const u64 begin = std::max(position, overlay.start);
        const u64 end = std::min(position + size, overlay.start + overlay.bytes.size());
        if (begin < end) {
            std::memcpy(data + (begin - position), overlay.bytes.data() + (begin - overlay.start),
                        end - begin);
        }
    }
}

} // namespace Core::FileSys::FilePatches
