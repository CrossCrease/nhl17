// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <httplib.h>
#include "core/emulator_settings.h"

namespace ShadNet {

// True when base_url ("scheme://host[:port]") is the configured shadNet WebAPI server.
inline bool IsWebApiServer(const std::string& base_url) {
    const std::string server = EmulatorSettings.GetShadNetWebApiServer();
    return !server.empty() && base_url == server;
}

// An HTTPS shadNet WebAPI server must present a certificate issued by the CA in
// Network.shadnet_ca_certificate, whatever the game asked for. As with sign-in, only that
// private CA is trusted, so the host name is not checked (servers may be reached by IP).
// Returns false when the client is not talking to such a server.
inline bool PinWebApiClient(httplib::Client& client, const std::string& base_url) {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
    const std::string ca_file = EmulatorSettings.GetShadNetCaCertificate();
    if (ca_file.empty() || base_url.rfind("https://", 0) != 0 || !IsWebApiServer(base_url)) {
        return false;
    }
    client.set_ca_cert_path(ca_file);
    client.enable_server_certificate_verification(true);
    client.enable_server_hostname_verification(false);
    return true;
#else
    return false;
#endif
}

} // namespace ShadNet
