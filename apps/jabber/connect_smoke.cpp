// Console smoke: TCP + STARTTLS + register IQ-get against a public host (Wine/Windows).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <string>

#include "xmpp/socket_tls.h"

int main(int argc, char **argv) {
    const char *host = argc > 1 ? argv[1] : "yax.im";
    jabber::TlsSocket sock;
    std::printf("connect %s:5222\n", host);
    if (!sock.connect_tcp(host, 5222)) {
        std::printf("FAIL connect: %s\n", sock.last_error.c_str());
        return 1;
    }
    std::string open =
        std::string("<?xml version='1.0'?><stream:stream to='") + host +
        "' xmlns='jabber:client' xmlns:stream='http://etherx.jabber.org/streams' "
        "version='1.0'>";
    if (!sock.send_all(open)) {
        std::printf("FAIL send stream: %s\n", sock.last_error.c_str());
        return 1;
    }
    std::string buf;
    char tmp[4096];
    for (int i = 0; i < 40 && buf.find("stream:features") == std::string::npos; ++i) {
        int n = sock.recv_some(tmp, sizeof(tmp));
        if (n <= 0) {
            std::printf("FAIL recv features: %s\n", sock.last_error.c_str());
            return 1;
        }
        buf.append(tmp, tmp + n);
    }
    if (buf.find("starttls") == std::string::npos) {
        std::printf("FAIL no starttls\n");
        return 1;
    }
    std::printf("STARTTLS…\n");
    if (!sock.send_all("<starttls xmlns='urn:ietf:params:xml:ns:xmpp-tls'/>")) {
        std::printf("FAIL send starttls: %s\n", sock.last_error.c_str());
        return 1;
    }
    buf.clear();
    for (int i = 0; i < 40 && buf.find("proceed") == std::string::npos; ++i) {
        int n = sock.recv_some(tmp, sizeof(tmp));
        if (n <= 0) {
            std::printf("FAIL proceed: %s\n", sock.last_error.c_str());
            return 1;
        }
        buf.append(tmp, tmp + n);
    }
    std::printf("TLS handshake…\n");
    if (!sock.start_tls(host)) {
        std::printf("FAIL tls: %s\n", sock.last_error.c_str());
        return 1;
    }
    if (!sock.send_all(open)) {
        std::printf("FAIL secure stream send: %s\n", sock.last_error.c_str());
        return 1;
    }
    buf.clear();
    for (int i = 0; i < 40 && buf.find("stream:features") == std::string::npos; ++i) {
        int n = sock.recv_some(tmp, sizeof(tmp));
        if (n <= 0) {
            std::printf("FAIL secure features: %s\n", sock.last_error.c_str());
            return 1;
        }
        buf.append(tmp, tmp + n);
    }
    std::printf("register IQ-get…\n");
    if (!sock.send_all(
            "<iq type='get' id='reg1'><query xmlns='jabber:iq:register'/></iq>")) {
        std::printf("FAIL reg send: %s\n", sock.last_error.c_str());
        return 1;
    }
    buf.clear();
    for (int i = 0; i < 40; ++i) {
        int n = sock.recv_some(tmp, sizeof(tmp));
        if (n <= 0) {
            std::printf("FAIL reg recv: %s\n", sock.last_error.c_str());
            return 1;
        }
        buf.append(tmp, tmp + n);
        if (buf.find("jabber:iq:register") != std::string::npos) break;
    }
    if (buf.find("type='error'") != std::string::npos ||
        buf.find("type=\"error\"") != std::string::npos) {
        std::printf("FAIL register error\n%s\n", buf.c_str());
        return 1;
    }
    if (buf.find("jabber:iq:register") == std::string::npos) {
        std::printf("FAIL no register form\n%s\n", buf.c_str());
        return 1;
    }
    std::printf("OK connect+TLS+register form on %s\n", host);
    sock.close();
    return 0;
}
