// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <thread>

namespace Input {

class GameControllers;

// Opt-in local pad control (SHADPS4_PAD_PIPE=true): a named pipe per emulator process,
// \\.\pipe\shadps4-pad-<pid>, that presses pad 1 buttons without window focus.
class PadPipe {
public:
    void Start(GameControllers* controllers);

private:
    void ServeLoop();
    std::string HandleCommand(const std::string& line);

    GameControllers* m_controllers{};
    std::jthread m_thread;
};

} // namespace Input
