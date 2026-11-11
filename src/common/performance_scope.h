// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <string_view>
#include "common/logging/log.h"

namespace Common {

struct PerformanceSample {
    std::chrono::steady_clock::time_point started{};
    unsigned long long calls = 0;
    double total_ms = 0;
    double maximum_ms = 0;
};

class PerformanceScope {
public:
    PerformanceScope(const char* name, PerformanceSample& sample) : name{name}, sample{sample} {
        if (Enabled()) {
            started = std::chrono::steady_clock::now();
        }
    }

    ~PerformanceScope() {
        if (!Enabled()) {
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        if (sample.started == std::chrono::steady_clock::time_point{}) {
            sample.started = started;
        }
        const double duration_ms = std::chrono::duration<double, std::milli>(now - started).count();
        if (duration_ms >= 8.0 &&
            (std::string_view{name} != "GpuProducerIdle" || duration_ms >= 25.0)) {
            LOG_INFO(Render_Vulkan, "Scope slow: name={} duration_ms={:.3f} at_ms={:.3f}", name,
                     duration_ms,
                     std::chrono::duration<double, std::milli>(now.time_since_epoch()).count());
        }
        ++sample.calls;
        sample.total_ms += duration_ms;
        sample.maximum_ms = std::max(sample.maximum_ms, duration_ms);
        const double elapsed_ms =
            std::chrono::duration<double, std::milli>(now - sample.started).count();
        if (elapsed_ms >= 1000.0) {
            LOG_INFO(Render_Vulkan,
                     "Scope stats: name={} calls={} elapsed_ms={:.3f} total_ms={:.3f} "
                     "max_ms={:.3f} at_ms={:.3f}",
                     name, sample.calls, elapsed_ms, sample.total_ms, sample.maximum_ms,
                     std::chrono::duration<double, std::milli>(now.time_since_epoch()).count());
            sample = {.started = now};
        }
    }

private:
    static bool Enabled() {
        static const bool enabled = [] {
            const char* value = std::getenv("SHADPS4_PROFILE_SCOPES");
            return value && std::string_view{value} == "1";
        }();
        return enabled;
    }

    const char* name;
    PerformanceSample& sample;
    std::chrono::steady_clock::time_point started{};
};

} // namespace Common

#define PERFORMANCE_SCOPE(name)                                                                    \
    static thread_local Common::PerformanceSample performance_sample;                              \
    const Common::PerformanceScope performance_scope {                                             \
        name, performance_sample                                                                   \
    }
