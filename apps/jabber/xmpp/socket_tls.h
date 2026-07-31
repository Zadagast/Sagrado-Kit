// Winsock TCP + optional Schannel TLS (STARTTLS) for XMPP.
#pragma once
#define SECURITY_WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <wincrypt.h>
#include <schannel.h>
#include <security.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "secur32.lib")
#pragma comment(lib, "crypt32.lib")

namespace jabber {

struct TlsSocket {
    SOCKET sock = INVALID_SOCKET;
    bool tls = false;
    CredHandle cred{};
    CtxtHandle ctx{};
    bool cred_ok = false;
    bool ctx_ok = false;
    SecPkgContext_StreamSizes sizes{};
    std::vector<char> recv_enc;
    std::vector<char> recv_plain;
    size_t plain_off = 0;

    static bool wsa_once() {
        static bool ok = false;
        static bool tried = false;
        if (!tried) {
            WSADATA w;
            ok = (WSAStartup(MAKEWORD(2, 2), &w) == 0);
            tried = true;
        }
        return ok;
    }

    void close() {
        if (ctx_ok) {
            DeleteSecurityContext(&ctx);
            ctx_ok = false;
        }
        if (cred_ok) {
            FreeCredentialsHandle(&cred);
            cred_ok = false;
        }
        if (sock != INVALID_SOCKET) {
            closesocket(sock);
            sock = INVALID_SOCKET;
        }
        tls = false;
        recv_enc.clear();
        recv_plain.clear();
        plain_off = 0;
    }

    bool connect_tcp(const std::string &host, int port) {
        close();
        if (!wsa_once()) return false;
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo *res = nullptr;
        std::string ps = std::to_string(port);
        if (getaddrinfo(host.c_str(), ps.c_str(), &hints, &res) != 0) return false;
        bool ok = false;
        for (addrinfo *p = res; p; p = p->ai_next) {
            sock = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
            if (sock == INVALID_SOCKET) continue;
            if (::connect(sock, p->ai_addr, (int)p->ai_addrlen) == 0) {
                ok = true;
                break;
            }
            closesocket(sock);
            sock = INVALID_SOCKET;
        }
        freeaddrinfo(res);
        DWORD tv = 15000;
        if (ok) {
            setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv, sizeof(tv));
            setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char *)&tv, sizeof(tv));
        }
        return ok;
    }

    bool send_raw(const void *data, int n) {
        const char *p = (const char *)data;
        while (n > 0) {
            int s = ::send(sock, p, n, 0);
            if (s <= 0) return false;
            p += s;
            n -= s;
        }
        return true;
    }

    int recv_raw(char *buf, int n) { return ::recv(sock, buf, n, 0); }

    bool send_all(const std::string &s) {
        if (!tls) return send_raw(s.data(), (int)s.size());
        size_t off = 0;
        while (off < s.size()) {
            size_t chunk = s.size() - off;
            if (chunk > sizes.cbMaximumMessage) chunk = sizes.cbMaximumMessage;
            std::vector<char> buf(sizes.cbHeader + chunk + sizes.cbTrailer);
            memcpy(buf.data() + sizes.cbHeader, s.data() + off, chunk);
            SecBuffer bufs[4]{};
            bufs[0].BufferType = SECBUFFER_STREAM_HEADER;
            bufs[0].pvBuffer = buf.data();
            bufs[0].cbBuffer = sizes.cbHeader;
            bufs[1].BufferType = SECBUFFER_DATA;
            bufs[1].pvBuffer = buf.data() + sizes.cbHeader;
            bufs[1].cbBuffer = (ULONG)chunk;
            bufs[2].BufferType = SECBUFFER_STREAM_TRAILER;
            bufs[2].pvBuffer = buf.data() + sizes.cbHeader + chunk;
            bufs[2].cbBuffer = sizes.cbTrailer;
            bufs[3].BufferType = SECBUFFER_EMPTY;
            SecBufferDesc desc{SECBUFFER_VERSION, 4, bufs};
            SECURITY_STATUS st = EncryptMessage(&ctx, 0, &desc, 0);
            if (st != SEC_E_OK) return false;
            int total = int(bufs[0].cbBuffer + bufs[1].cbBuffer + bufs[2].cbBuffer);
            if (!send_raw(buf.data(), total)) return false;
            off += chunk;
        }
        return true;
    }

    bool ensure_plain() {
        if (plain_off < recv_plain.size()) return true;
        recv_plain.clear();
        plain_off = 0;
        char tmp[8192];
        int n = recv_raw(tmp, sizeof(tmp));
        if (n <= 0) return false;
        if (!tls) {
            recv_plain.assign(tmp, tmp + n);
            return true;
        }
        recv_enc.insert(recv_enc.end(), tmp, tmp + n);
        for (;;) {
            SecBuffer bufs[4]{};
            bufs[0].BufferType = SECBUFFER_DATA;
            bufs[0].pvBuffer = recv_enc.data();
            bufs[0].cbBuffer = (ULONG)recv_enc.size();
            bufs[1].BufferType = SECBUFFER_EMPTY;
            bufs[2].BufferType = SECBUFFER_EMPTY;
            bufs[3].BufferType = SECBUFFER_EMPTY;
            SecBufferDesc desc{SECBUFFER_VERSION, 4, bufs};
            SECURITY_STATUS st = DecryptMessage(&ctx, &desc, 0, nullptr);
            if (st == SEC_E_OK) {
                for (int i = 0; i < 4; ++i) {
                    if (bufs[i].BufferType == SECBUFFER_DATA && bufs[i].pvBuffer)
                        recv_plain.insert(recv_plain.end(),
                                          (char *)bufs[i].pvBuffer,
                                          (char *)bufs[i].pvBuffer + bufs[i].cbBuffer);
                }
                size_t leftover = 0;
                char *extra = nullptr;
                for (int i = 0; i < 4; ++i) {
                    if (bufs[i].BufferType == SECBUFFER_EXTRA && bufs[i].pvBuffer) {
                        leftover = bufs[i].cbBuffer;
                        extra = (char *)bufs[i].pvBuffer;
                    }
                }
                if (leftover && extra)
                    recv_enc.assign(extra, extra + leftover);
                else
                    recv_enc.clear();
                return !recv_plain.empty() || ensure_plain();
            }
            if (st == SEC_E_INCOMPLETE_MESSAGE) {
                n = recv_raw(tmp, sizeof(tmp));
                if (n <= 0) return false;
                recv_enc.insert(recv_enc.end(), tmp, tmp + n);
                continue;
            }
            return false;
        }
    }

    int recv_some(char *buf, int n) {
        if (!ensure_plain()) return -1;
        size_t avail = recv_plain.size() - plain_off;
        int take = (int)std::min(avail, size_t(n));
        memcpy(buf, recv_plain.data() + plain_off, take);
        plain_off += size_t(take);
        return take;
    }

    bool start_tls(const std::string &host) {
        SCHANNEL_CRED sc{};
        sc.dwVersion = SCHANNEL_CRED_VERSION;
        sc.grbitEnabledProtocols = SP_PROT_TLS1_0 | SP_PROT_TLS1_1 | SP_PROT_TLS1_2;
        sc.dwFlags = SCH_CRED_NO_DEFAULT_CREDS | SCH_CRED_MANUAL_CRED_VALIDATION;
        SECURITY_STATUS st =
            AcquireCredentialsHandleA(nullptr, (SEC_CHAR *)UNISP_NAME_A, SECPKG_CRED_OUTBOUND,
                                      nullptr, &sc, nullptr, nullptr, &cred, nullptr);
        if (st != SEC_E_OK) return false;
        cred_ok = true;

        SecBuffer outb{};
        outb.BufferType = SECBUFFER_TOKEN;
        SecBufferDesc outd{SECBUFFER_VERSION, 1, &outb};
        DWORD attr = 0;
        st = InitializeSecurityContextA(
            &cred, nullptr, (SEC_CHAR *)host.c_str(),
            ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT | ISC_REQ_CONFIDENTIALITY |
                ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM,
            0, 0, nullptr, 0, &ctx, &outd, &attr, nullptr);
        ctx_ok = true;
        if (st != SEC_I_CONTINUE_NEEDED && st != SEC_E_OK) return false;
        if (outb.cbBuffer && outb.pvBuffer) {
            if (!send_raw(outb.pvBuffer, (int)outb.cbBuffer)) return false;
            FreeContextBuffer(outb.pvBuffer);
        }

        std::vector<char> inbuf;
        while (st == SEC_I_CONTINUE_NEEDED || st == SEC_E_INCOMPLETE_MESSAGE) {
            if (st != SEC_E_INCOMPLETE_MESSAGE || inbuf.empty()) {
                char tmp[8192];
                int n = recv_raw(tmp, sizeof(tmp));
                if (n <= 0) return false;
                inbuf.insert(inbuf.end(), tmp, tmp + n);
            }
            SecBuffer inbufs[2]{};
            inbufs[0].BufferType = SECBUFFER_TOKEN;
            inbufs[0].pvBuffer = inbuf.data();
            inbufs[0].cbBuffer = (ULONG)inbuf.size();
            inbufs[1].BufferType = SECBUFFER_EMPTY;
            SecBufferDesc ind{SECBUFFER_VERSION, 2, inbufs};
            outb = {};
            outb.BufferType = SECBUFFER_TOKEN;
            outd = {SECBUFFER_VERSION, 1, &outb};
            st = InitializeSecurityContextA(
                &cred, &ctx, (SEC_CHAR *)host.c_str(),
                ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT | ISC_REQ_CONFIDENTIALITY |
                    ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM,
                0, 0, &ind, 0, nullptr, &outd, &attr, nullptr);
            if (st == SEC_E_INCOMPLETE_MESSAGE) continue;
            if (inbufs[1].BufferType == SECBUFFER_EXTRA && inbufs[1].pvBuffer) {
                size_t left = inbufs[1].cbBuffer;
                char *p = (char *)inbufs[1].pvBuffer;
                inbuf.assign(p, p + left);
            } else {
                inbuf.clear();
            }
            if (outb.cbBuffer && outb.pvBuffer) {
                if (!send_raw(outb.pvBuffer, (int)outb.cbBuffer)) return false;
                FreeContextBuffer(outb.pvBuffer);
            }
            if (st == SEC_E_OK) break;
            if (st != SEC_I_CONTINUE_NEEDED) return false;
        }
        QueryContextAttributes(&ctx, SECPKG_ATTR_STREAM_SIZES, &sizes);
        tls = true;
        return true;
    }
};

} // namespace jabber
