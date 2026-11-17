// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <chrono>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#include "common/logging/log.h"
#include "shadnet/tls.h"

namespace ShadNet {

static void SetNonBlocking(ShadSocketHandle sock) {
#ifdef _WIN32
    u_long nb = 1;
    ::ioctlsocket(sock, FIONBIO, &nb);
#else
    ::fcntl(sock, F_SETFL, ::fcntl(sock, F_GETFL, 0) | O_NONBLOCK);
#endif
}

static bool WaitSocket(ShadSocketHandle sock, bool for_write, u32 wait_ms) {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(sock, &fds);
    struct timeval tv{static_cast<long>(wait_ms / 1000),
                      static_cast<long>((wait_ms % 1000) * 1000)};
    return ::select(static_cast<int>(sock) + 1, for_write ? nullptr : &fds,
                    for_write ? &fds : nullptr, nullptr, &tv) > 0;
}

static std::string LastSslError() {
    const unsigned long code = ERR_get_error();
    if (code == 0)
        return "no details";
    char text[256];
    ERR_error_string_n(code, text, sizeof(text));
    return text;
}

TlsChannel::~TlsChannel() {
    Close();
}

bool TlsChannel::Connect(ShadSocketHandle sock, const std::string& ca_file, u32 timeout_ms) {
    std::lock_guard lock(m_mutex);
    m_sock = sock;
    m_ctx = SSL_CTX_new(TLS_client_method());
    if (!m_ctx) {
        LOG_ERROR(ShadNet, "TLS: cannot create context: {}", LastSslError());
        return false;
    }
    SSL_CTX_set_min_proto_version(m_ctx, TLS1_2_VERSION);
    if (SSL_CTX_load_verify_locations(m_ctx, ca_file.c_str(), nullptr) != 1) {
        LOG_ERROR(ShadNet, "TLS: cannot load the server CA certificate '{}': {}", ca_file,
                  LastSslError());
        return false;
    }
    SSL_CTX_set_verify(m_ctx, SSL_VERIFY_PEER, nullptr);
    m_ssl = SSL_new(m_ctx);
    BIO* bio = m_ssl ? BIO_new_socket(static_cast<int>(sock), BIO_NOCLOSE) : nullptr;
    if (!bio) {
        LOG_ERROR(ShadNet, "TLS: cannot create session: {}", LastSslError());
        return false;
    }
    SSL_set_bio(m_ssl, bio, bio);
    SetNonBlocking(sock);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (true) {
        const int r = SSL_connect(m_ssl);
        if (r == 1)
            break;
        const int err = SSL_get_error(m_ssl, r);
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        if ((err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) && left.count() > 0) {
            WaitSocket(sock, err == SSL_ERROR_WANT_WRITE, static_cast<u32>(left.count()));
            continue;
        }
        const long verify = SSL_get_verify_result(m_ssl);
        if (verify != X509_V_OK)
            LOG_ERROR(ShadNet, "TLS: the server certificate is not trusted: {}",
                      X509_verify_cert_error_string(verify));
        else
            LOG_ERROR(ShadNet, "TLS handshake failed: {}",
                      left.count() > 0 ? LastSslError() : std::string("timed out"));
        return false;
    }
    LOG_INFO(ShadNet, "TLS connected ({}, {})", SSL_get_version(m_ssl), SSL_get_cipher_name(m_ssl));
    return true;
}

int TlsChannel::Read(u8* buf, u32 n, u32 wait_ms) {
    int err;
    ShadSocketHandle sock;
    {
        std::lock_guard lock(m_mutex);
        if (!m_ssl)
            return 0;
        const int r = SSL_read(m_ssl, buf, static_cast<int>(n));
        if (r > 0)
            return r;
        err = SSL_get_error(m_ssl, r);
        sock = m_sock;
    }
    if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE)
        return 0;
    WaitSocket(sock, err == SSL_ERROR_WANT_WRITE, wait_ms);
    return -1;
}

bool TlsChannel::Write(const u8* data, u32 n) {
    while (true) {
        int err;
        ShadSocketHandle sock;
        {
            std::lock_guard lock(m_mutex);
            if (!m_ssl)
                return false;
            const int r = SSL_write(m_ssl, data, static_cast<int>(n));
            if (r > 0)
                return true;
            err = SSL_get_error(m_ssl, r);
            sock = m_sock;
        }
        if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE)
            return false;
        WaitSocket(sock, err == SSL_ERROR_WANT_WRITE, 250);
    }
}

void TlsChannel::Close() {
    std::lock_guard lock(m_mutex);
    if (m_ssl) {
        SSL_free(m_ssl);
        m_ssl = nullptr;
    }
    if (m_ctx) {
        SSL_CTX_free(m_ctx);
        m_ctx = nullptr;
    }
    m_sock = SHAD_INVALID_SOCK;
}

} // namespace ShadNet
