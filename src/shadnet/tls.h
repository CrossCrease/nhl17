// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once
#include <mutex>
#include <string>
#include "shadnet/client.h"

typedef struct ssl_st SSL;
typedef struct ssl_ctx_st SSL_CTX;

namespace ShadNet {

// TLS over an already connected shadNet socket. The server certificate must chain to the CA
// certificate file; the system certificate store is not used. Reads and writes may come from
// different threads: the socket is non-blocking and every SSL call is serialized.
class TlsChannel {
public:
    TlsChannel() = default;
    ~TlsChannel();
    TlsChannel(const TlsChannel&) = delete;
    TlsChannel& operator=(const TlsChannel&) = delete;

    bool Connect(ShadSocketHandle sock, const std::string& ca_file, u32 timeout_ms);
    // Bytes read, 0 when the connection is closed or failed, -1 when nothing arrived in wait_ms.
    int Read(u8* buf, u32 n, u32 wait_ms);
    bool Write(const u8* data, u32 n);
    void Close();

private:
    std::mutex m_mutex;
    SSL_CTX* m_ctx = nullptr;
    SSL* m_ssl = nullptr;
    ShadSocketHandle m_sock = SHAD_INVALID_SOCK;
};

} // namespace ShadNet
