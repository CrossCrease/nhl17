// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/arch.h"
#include <string>
#include <fmt/format.h>
#include "common/assert.h"
#include "common/crash_report.h"
#include "core/signals.h"
#include "emulator.h"

#if defined(ARCH_X86_64)
#define Crash() __asm__ __volatile__("int $3")
#elif defined(ARCH_ARM64)
#define Crash() __asm__ __volatile__("brk 0")
#else
#error "Missing Crash() implementation for target CPU architecture."
#endif

static std::string g_assert_location = "unknown";

void assert_fail_location(const char* file, int line) {
    std::string_view path = file ? file : "";
    const auto slash = path.find_last_of("/\\");
    g_assert_location =
        fmt::format("{}:{}", slash == std::string_view::npos ? path : path.substr(slash + 1), line);
}

void assert_fail_impl() {
    Common::WriteCrashReport("assert", "assert", g_assert_location);
    Core::Signals::Instance()->RemoveHandlers();
    Common::Singleton<Core::Emulator>::Instance()->Shutdown();
    Crash();
}

[[noreturn]] void unreachable_impl() {
    assert_fail_impl();
    throw std::runtime_error("Unreachable code");
}

void assert_fail_debug_msg(const char* msg) {
    LOG_CRITICAL(Debug, "Assertion Failed!\n{}", msg);
    assert_fail_impl();
}
