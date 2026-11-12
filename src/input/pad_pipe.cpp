// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "input/pad_pipe.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <utility>

#include "common/logging/log.h"
#include "common/thread.h"
#include "core/libraries/pad/pad.h"
#include "input/controller.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace Input {

using Libraries::Pad::OrbisPadButtonDataOffset;

namespace {

constexpr std::array<std::pair<std::string_view, OrbisPadButtonDataOffset>, 16> PadButtons{{
    {"cross", OrbisPadButtonDataOffset::Cross},
    {"circle", OrbisPadButtonDataOffset::Circle},
    {"square", OrbisPadButtonDataOffset::Square},
    {"triangle", OrbisPadButtonDataOffset::Triangle},
    {"up", OrbisPadButtonDataOffset::Up},
    {"down", OrbisPadButtonDataOffset::Down},
    {"left", OrbisPadButtonDataOffset::Left},
    {"right", OrbisPadButtonDataOffset::Right},
    {"l1", OrbisPadButtonDataOffset::L1},
    {"r1", OrbisPadButtonDataOffset::R1},
    {"l2", OrbisPadButtonDataOffset::L2},
    {"r2", OrbisPadButtonDataOffset::R2},
    {"l3", OrbisPadButtonDataOffset::L3},
    {"r3", OrbisPadButtonDataOffset::R3},
    {"options", OrbisPadButtonDataOffset::Options},
    {"touchpad", OrbisPadButtonDataOffset::TouchPad},
}};

} // namespace

void PadPipe::Start(GameControllers* controllers) {
#ifdef _WIN32
    const char* enabled = std::getenv("SHADPS4_PAD_PIPE");
    if (!enabled || std::strcmp(enabled, "true") != 0) {
        return;
    }
    m_controllers = controllers;
    m_thread = std::jthread([this] {
        Common::SetCurrentThreadName("Pad pipe");
        ServeLoop();
    });
#endif
}

std::string PadPipe::HandleCommand(const std::string& line) {
    std::istringstream stream(line);
    std::string name;
    int pressed = -1;
    stream >> name >> pressed;
    std::ranges::transform(name, name.begin(), [](unsigned char c) { return std::tolower(c); });
    const auto button = std::ranges::find(PadButtons, name, &decltype(PadButtons)::value_type::first);
    if (button == PadButtons.end() || (pressed != 0 && pressed != 1)) {
        return "ERR expected <button> <0|1>";
    }
    auto* controller = (*m_controllers)[0];
    if (button->second == OrbisPadButtonDataOffset::L2) {
        controller->Axis(Axis::TriggerLeft, pressed ? 255 : 0, false);
    } else if (button->second == OrbisPadButtonDataOffset::R2) {
        controller->Axis(Axis::TriggerRight, pressed ? 255 : 0, false);
    }
    controller->Button(button->second, pressed == 1);
    return "OK";
}

void PadPipe::ServeLoop() {
#ifdef _WIN32
    const auto pipe_name = "\\\\.\\pipe\\shadps4-pad-" + std::to_string(GetCurrentProcessId());
    LOG_INFO(Input, "Pad pipe listening on {}", pipe_name);
    while (true) {
        HANDLE pipe = CreateNamedPipeA(pipe_name.c_str(), PIPE_ACCESS_DUPLEX,
                                       PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT |
                                           PIPE_REJECT_REMOTE_CLIENTS,
                                       1, 4096, 4096, 0, nullptr);
        if (pipe == INVALID_HANDLE_VALUE) {
            LOG_ERROR(Input, "Pad pipe could not be created: {}", GetLastError());
            return;
        }
        if (ConnectNamedPipe(pipe, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED) {
            std::string pending;
            std::array<char, 512> buffer;
            DWORD read = 0;
            while (ReadFile(pipe, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr) &&
                   read > 0) {
                pending.append(buffer.data(), read);
                for (auto end = pending.find('\n'); end != std::string::npos;
                     end = pending.find('\n')) {
                    auto line = pending.substr(0, end);
                    pending.erase(0, end + 1);
                    if (!line.empty() && line.back() == '\r') {
                        line.pop_back();
                    }
                    if (line.empty()) {
                        continue;
                    }
                    const auto reply = HandleCommand(line) + "\n";
                    DWORD written = 0;
                    WriteFile(pipe, reply.data(), static_cast<DWORD>(reply.size()), &written,
                              nullptr);
                }
            }
        }
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }
#endif
}

} // namespace Input
