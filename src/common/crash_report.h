// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string_view>

namespace Common {

// Writes crash.txt next to the log (key=value lines) so a launcher can offer to report the
// crash on its next start. Only the first call per run writes; later calls do nothing.
void WriteCrashReport(std::string_view kind, std::string_view code, std::string_view where);

} // namespace Common
