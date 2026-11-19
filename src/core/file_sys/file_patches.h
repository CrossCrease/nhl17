// SPDX-FileCopyrightText: Copyright 2026 CrossCrease
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <filesystem>
#include <string>
#include "common/types.h"

namespace Core::FileSys::FilePatches {

// Same-length text replacement inside a raw-deflate "chunkzip" record of a game file. The
// record is rebuilt from the player's own file when it is first read, so no game data ships
// with the patch; reads of that file then see the rebuilt record.
void AddChunkzipText(const std::string& file, u64 offset, const std::string& find,
                     const std::string& replace);

void Apply(const std::filesystem::path& host_path, u64 position, u8* data, u64 size);

} // namespace Core::FileSys::FilePatches
