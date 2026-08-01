// Winsock TCP + mbedTLS STARTTLS for XMPP (Wine-safe; select deadlines).
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <wincrypt.h>
#include <windns.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "dnsapi.lib")

namespace jabber {

inline int mbedtls_win_entropy_poll(void * /*data*/, unsigned char *output, size_t len,
                                    size_t *olen) {
    HCRYPTPROV prov = 0;
    if (!CryptAcquireContextA(&prov, nullptr, nullptr, PROV_RSA_FULL,
                              CRYPT_VERIFYCONTEXT)) {
        return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
    }
    BOOL ok = CryptGenRandom(prov, (DWORD)len, output);
    CryptReleaseContext(prov, 0);
    if (!ok) return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
    *olen = len;
    return 0;
}

// mbedTLS calls this via MBEDTLS_ENTROPY_HARDWARE_ALT
extern "C" int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len,
                                     size_t *olen) {
    return mbedtls_win_entropy_poll(data, output, len, olen);
}

// RFC 2782 SRV lookup (priority-ordered). Used for _xmpp-client (STARTTLS)
// and _xmpps-client (XEP-0368 direct TLS) service records.
struct SrvRecord {
    std::string target;
    int port = 0;
    int priority = 0;
    int weight = 0;
};

inline std::vector<SrvRecord> resolve_srv(const std::string &name) {
    std::vector<SrvRecord> out;
    PDNS_RECORDA rec = nullptr;
    DNS_STATUS st =
        DnsQuery_A(name.c_str(), DNS_TYPE_SRV, DNS_QUERY_STANDARD, nullptr,
                   (PDNS_RECORD *)&rec, nullptr);
    if (st != 0 || !rec) return out;
    for (PDNS_RECORDA r = rec; r; r = r->pNext) {
        if (r->wType != DNS_TYPE_SRV) continue;
        SrvRecord s;
        s.target = r->Data.SRV.pNameTarget ? r->Data.SRV.pNameTarget : "";
        s.port = r->Data.SRV.wPort;
        s.priority = r->Data.SRV.wPriority;
        s.weight = r->Data.SRV.wWeight;
        if (!s.target.empty() && s.target != ".") out.push_back(s);
    }
    DnsRecordListFree((PDNS_RECORD)rec, DnsFreeRecordListDeep);
    std::sort(out.begin(), out.end(), [](const SrvRecord &a, const SrvRecord &b) {
        if (a.priority != b.priority) return a.priority < b.priority;
        return a.weight > b.weight;
    });
    return out;
}

struct TlsSocket {
    SOCKET sock = INVALID_SOCKET;
    bool tls = false;
    std::string last_error;

    mbedtls_entropy_context entropy{};
    mbedtls_ctr_drbg_context ctr_drbg{};
    mbedtls_ssl_context ssl{};
    mbedtls_ssl_config conf{};
    bool ssl_ready = false;

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

    void set_err(const std::string &e) { last_error = e; }

    void free_tls() {
        if (ssl_ready) {
            mbedtls_ssl_free(&ssl);
            mbedtls_ssl_config_free(&conf);
            mbedtls_ctr_drbg_free(&ctr_drbg);
            mbedtls_entropy_free(&entropy);
            ssl_ready = false;
        }
        tls = false;
    }

    void close() {
        free_tls();
        if (sock != INVALID_SOCKET) {
            closesocket(sock);
            sock = INVALID_SOCKET;
        }
    }

    bool wait_fd(bool for_write, int timeout_ms) {
        if (sock == INVALID_SOCKET) return false;
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(sock, &fds);
        timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        int r = select(0, for_write ? nullptr : &fds, for_write ? &fds : nullptr, nullptr,
                       &tv);
        return r > 0;
    }

    bool connect_tcp(const std::string &host, int port, int timeout_ms = 8000) {
        close();
        if (!wsa_once()) {
            set_err("Winsock init failed");
            return false;
        }
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo *res = nullptr;
        std::string ps = std::to_string(port);
        if (getaddrinfo(host.c_str(), ps.c_str(), &hints, &res) != 0) {
            set_err("DNS failed for " + host);
            return false;
        }
        // IPv4 first: broken/unroutable IPv6 otherwise stalls sign-on for a
        // full connect timeout per AAAA record before v4 is ever tried.
        std::vector<addrinfo *> order;
        for (addrinfo *p = res; p; p = p->ai_next)
            if (p->ai_family == AF_INET) order.push_back(p);
        for (addrinfo *p = res; p; p = p->ai_next)
            if (p->ai_family != AF_INET) order.push_back(p);
        if (order.size() > 1) timeout_ms = std::min(timeout_ms, 4000);
        bool ok = false;
        for (addrinfo *p : order) {
            sock = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
            if (sock == INVALID_SOCKET) continue;
            u_long nb = 1;
            ioctlsocket(sock, FIONBIO, &nb);
            int cr = ::connect(sock, p->ai_addr, (int)p->ai_addrlen);
            if (cr == 0) {
                ok = true;
                break;
            }
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK || err == WSAEINPROGRESS) {
                if (wait_fd(true, timeout_ms)) {
                    int soerr = 0;
                    int slen = sizeof(soerr);
                    getsockopt(sock, SOL_SOCKET, SO_ERROR, (char *)&soerr, &slen);
                    if (soerr == 0) {
                        ok = true;
                        break;
                    }
                }
            }
            closesocket(sock);
            sock = INVALID_SOCKET;
        }
        freeaddrinfo(res);
        if (!ok) {
            set_err("Could not connect to " + host + ":" + std::to_string(port));
            return false;
        }
        // Leave non-blocking; all I/O uses select deadlines.
        return true;
    }

    bool send_raw(const void *data, int n, int timeout_ms = 15000) {
        const char *p = (const char *)data;
        while (n > 0) {
            if (!wait_fd(true, timeout_ms)) {
                set_err("Send timed out");
                return false;
            }
            int s = ::send(sock, p, n, 0);
            if (s == SOCKET_ERROR) {
                int e = WSAGetLastError();
                if (e == WSAEWOULDBLOCK) continue;
                set_err("Send failed");
                return false;
            }
            if (s == 0) return false;
            p += s;
            n -= s;
        }
        return true;
    }

    int recv_raw(char *buf, int n, int timeout_ms = 15000) {
        if (!wait_fd(false, timeout_ms)) {
            set_err("Receive timed out");
            return -1;
        }
        int r = ::recv(sock, buf, n, 0);
        if (r == SOCKET_ERROR) {
            int e = WSAGetLastError();
            if (e == WSAEWOULDBLOCK) return 0;
            set_err("Receive failed");
            return -1;
        }
        return r;
    }

    static int bio_send(void *ctx, const unsigned char *buf, size_t len) {
        auto *self = static_cast<TlsSocket *>(ctx);
        if (!self->send_raw(buf, (int)len, 15000)) return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
        return (int)len;
    }

    static int bio_recv(void *ctx, unsigned char *buf, size_t len) {
        auto *self = static_cast<TlsSocket *>(ctx);
        int n = self->recv_raw((char *)buf, (int)len, 15000);
        if (n < 0) return MBEDTLS_ERR_SSL_TIMEOUT;
        if (n == 0) return MBEDTLS_ERR_SSL_WANT_READ;
        return n;
    }

    bool send_all(const std::string &s) {
        if (!tls) return send_raw(s.data(), (int)s.size());
        size_t off = 0;
        while (off < s.size()) {
            int r = mbedtls_ssl_write(&ssl, (const unsigned char *)s.data() + off,
                                      s.size() - off);
            if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
            if (r <= 0) {
                char err[128];
                mbedtls_strerror(r, err, sizeof(err));
                set_err(std::string("TLS write: ") + err);
                return false;
            }
            off += (size_t)r;
        }
        return true;
    }

    int recv_some(char *buf, int n) {
        if (!tls) {
            int r = recv_raw(buf, n, 15000);
            return r;
        }
        for (;;) {
            int r = mbedtls_ssl_read(&ssl, (unsigned char *)buf, (size_t)n);
            if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
            if (r == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) return 0;
            if (r < 0) {
                char err[128];
                mbedtls_strerror(r, err, sizeof(err));
                set_err(std::string("TLS read: ") + err);
                return -1;
            }
            return r;
        }
    }

    bool start_tls(const std::string &host) {
        free_tls();
        mbedtls_entropy_init(&entropy);
        mbedtls_ctr_drbg_init(&ctr_drbg);
        mbedtls_ssl_init(&ssl);
        mbedtls_ssl_config_init(&conf);
        ssl_ready = true;

        const char *pers = "SagradoJabber";
        int ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                                        (const unsigned char *)pers, strlen(pers));
        if (ret != 0) {
            set_err("RNG seed failed");
            free_tls();
            return false;
        }
        ret = mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT,
                                          MBEDTLS_SSL_TRANSPORT_STREAM,
                                          MBEDTLS_SSL_PRESET_DEFAULT);
        if (ret != 0) {
            set_err("TLS config failed");
            free_tls();
            return false;
        }
        // Wine often lacks a usable root store; don't hang on verify.
        mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_NONE);
        mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &ctr_drbg);
        ret = mbedtls_ssl_setup(&ssl, &conf);
        if (ret != 0) {
            set_err("TLS setup failed");
            free_tls();
            return false;
        }
        mbedtls_ssl_set_hostname(&ssl, host.c_str());
        mbedtls_ssl_set_bio(&ssl, this, bio_send, bio_recv, nullptr);

        while ((ret = mbedtls_ssl_handshake(&ssl)) != 0) {
            if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE)
                continue;
            char err[128];
            mbedtls_strerror(ret, err, sizeof(err));
            set_err(std::string("TLS handshake failed: ") + err);
            free_tls();
            return false;
        }
        tls = true;
        return true;
    }
};

} // namespace jabber
