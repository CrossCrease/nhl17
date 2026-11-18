// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <atomic>
#include <chrono>
#include <fstream>
#include <fmt/chrono.h>
#include <fmt/format.h>
#include "common/crash_report.h"
#include "common/path_util.h"
#include "common/scm_rev.h"

namespace Common {

void WriteCrashReport(std::string_view kind, std::string_view code, std::string_view where) {
    static std::atomic_flag written;
    if (written.test_and_set()) {
        return;
    }
    try {
        const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
        std::ofstream file(FS::GetUserPath(FS::PathType::LogDir) / "crash.txt",
                           std::ios::binary | std::ios::trunc);
        file << fmt::format("format=1\nkind={}\ncode={}\naddress={}\ntime={:%Y-%m-%dT%H:%M:%SZ}\n"
                            "version={} {}\nlog=shad_log.txt\n",
                            kind, code, where, now, g_version, g_scm_desc);
    } catch (...) {
    }
}

} // namespace Common
