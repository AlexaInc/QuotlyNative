// this file is part of AlexaInc / QuotlyNative — MTProto Transport Layer
// developer hansaka@alexainc

#include "transport.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <cstring>
#include <stdexcept>
#include <cerrno>
#include <string>

namespace MTProto {

struct Transport::Impl {
    int fd = -1;
};

Transport::Transport() : m_impl(std::make_unique<Impl>()) {}

Transport::~Transport() { close(); }

void Transport::connect(int dc_id) {
    if (dc_id < 1 || dc_id > 5)
        throw std::invalid_argument("DC id must be 1-5");

    close();

    auto& dc = DC_ADDRESSES[dc_id];

    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    std::string port_str = std::to_string(dc.port);
    int rc = getaddrinfo(dc.host, port_str.c_str(), &hints, &res);
    if (rc != 0)
        throw std::runtime_error(std::string("getaddrinfo: ") + gai_strerror(rc));

    int fd = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) {
        freeaddrinfo(res);
        throw std::runtime_error(std::string("socket: ") + strerror(errno));
    }

    // Set send/recv timeout (30 seconds)
    struct timeval tv{ 30, 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (::connect(fd, res->ai_addr, res->ai_addrlen) < 0) {
        ::close(fd);
        freeaddrinfo(res);
        throw std::runtime_error(std::string("connect to DC") + std::to_string(dc_id) +
                                  " (" + dc.host + ":" + port_str + "): " + strerror(errno));
    }
    freeaddrinfo(res);

    m_impl->fd = fd;
    m_connected = true;

    // Abridged framing handshake: send 0xEF
    uint8_t init = 0xEF;
    ::send(fd, &init, 1, MSG_NOSIGNAL);
}

void Transport::send_raw(const uint8_t* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = ::send(m_impl->fd, data + sent, len - sent, MSG_NOSIGNAL);
        if (n <= 0) {
            m_connected = false;
            throw std::runtime_error(std::string("send failed: ") + strerror(errno));
        }
        sent += n;
    }
}

void Transport::recv_raw(uint8_t* buf, size_t len) {
    size_t got = 0;
    while (got < len) {
        ssize_t n = ::recv(m_impl->fd, buf + got, len - got, MSG_WAITALL);
        if (n <= 0) {
            m_connected = false;
            throw std::runtime_error(std::string("recv failed: ") + strerror(errno));
        }
        got += n;
    }
}

// ── Abridged framing ──────────────────────────────────────────────────────────
// Send: payload length / 4 encoded as 1 byte (<127) or 4 bytes (0x7f prefix)
void Transport::send(const Bytes& payload) {
    if (!m_connected || m_impl->fd < 0)
        throw std::runtime_error("Transport: not connected");

    size_t len4 = payload.size() / 4;

    if (len4 < 127) {
        uint8_t hdr = static_cast<uint8_t>(len4);
        send_raw(&hdr, 1);
    } else {
        uint8_t hdr[4];
        hdr[0] = 0x7f;
        hdr[1] = len4 & 0xff;
        hdr[2] = (len4 >> 8) & 0xff;
        hdr[3] = (len4 >> 16) & 0xff;
        send_raw(hdr, 4);
    }
    send_raw(payload.data(), payload.size());
}

// Receive one abridged frame
Bytes Transport::recv() {
    if (!m_connected || m_impl->fd < 0)
        return {};

    uint8_t first = 0;
    recv_raw(&first, 1);

    size_t len4;
    if (first < 0x7f) {
        len4 = first;
    } else {
        uint8_t ext[3];
        recv_raw(ext, 3);
        len4 = ext[0] | (ext[1] << 8) | (ext[2] << 16);
    }

    size_t payload_len = len4 * 4;
    if (payload_len == 0) return {};

    Bytes payload(payload_len);
    recv_raw(payload.data(), payload_len);
    return payload;
}

void Transport::close() {
    if (m_impl && m_impl->fd >= 0) {
        ::close(m_impl->fd);
        m_impl->fd = -1;
    }
    m_connected = false;
}

} // namespace MTProto
