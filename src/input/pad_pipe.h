// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <string>
#include <thread>

#include "common/types.h"

namespace Input {

class GameControllers;

// Opt-in local pad control (SHADPS4_PAD_PIPE=true): a named pipe per emulator process,
// \\.\pipe\shadps4-pad-<pid>, that presses pad 1 buttons without window focus. The pressed
// buttons also drive emulator overlays (ImGui gamepad navigation).
class PadPipe {
public:
    void Start(GameControllers* controllers);

    bool IsActive() const {
        return m_controllers != nullptr;
    }

    // Share never reaches the game (PS4 titles cannot read it); it only drives overlays.
    static constexpr u32 ShareButton = 0x1;

    // Currently pressed buttons as an OrbisPadButtonDataOffset mask, plus ShareButton.
    u32 Buttons() const {
        return m_buttons.load(std::memory_order_relaxed);
    }

private:
    void ServeLoop();
    std::string HandleCommand(const std::string& line);

    GameControllers* m_controllers{};
    std::atomic<u32> m_buttons{};
    std::jthread m_thread;
};

} // namespace Input
