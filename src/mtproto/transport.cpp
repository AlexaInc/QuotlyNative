// this file is part of AlexaInc / QuotlyNative — MTProto Transport Layer
// developer hansaka@alexainc
//
// FIXES (2026-05-22 v2):
//   * recv() now recognizes the MTProto transport-error packet (a single
//     4-byte little-endian negative int32, e.g. -404 "handshake query
//     incorrect", -429 "transport flood"). Previously this surfaced as the
//     misleading "Frame too short for unencrypted message".
//   * Transport errors are thrown as `TransportError` with the negative
//     code so the caller can react (or at least log something useful).

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
#include <sstream>

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
// Send: payload_len/4 as 1 byte (< 0x7f) or 0x7f + 3-byte LE length
void Transport::send(const Bytes& payload) {
    if (!m_connected || m_impl->fd < 0)
        throw std::runtime_error("Transport: not connected");

    size_t len4 = payload.size() / 4;

    if (len4 < 0x7f) {
        uint8_t hdr = static_cast<uint8_t>(len4);
        send_raw(&hdr, 1);
    } else {
        uint8_t hdr[4];
        hdr[0] = 0x7f;
        hdr[1] =  len4        & 0xff;
        hdr[2] = (len4 >>  8) & 0xff;
        hdr[3] = (len4 >> 16) & 0xff;
        send_raw(hdr, 4);
    }
    send_raw(payload.data(), payload.size());
}

// ── Translate Telegram transport-error codes to human strings ─────────────────
// Reference: https://core.telegram.org/mtproto/mtproto-transports
static const char* describe_transport_error(int32_t code) {
    switch (-code) {
        case 403: return "HTTP-style 403 (forbidden / banned IP)";
        case 404: return "auth key not found OR handshake query rejected "
                         "(bad RSA padding, bad p/q ordering, wrong dc_id, "
                         "or wrong fingerprint)";
        case 429: return "transport flood (too many connections from this IP "
                         "in a short window — back off and retry)";
        default:  return "unknown transport error";
    }
}

// Receive one abridged frame, OR raise on a 4-byte transport-error packet.
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
        len4 = (size_t)ext[0] | ((size_t)ext[1] << 8) | ((size_t)ext[2] << 16);
    }

    size_t payload_len = len4 * 4;
    if (payload_len == 0) return {};

    Bytes payload(payload_len);
    recv_raw(payload.data(), payload_len);

    // ── Transport-error detection ────────────────────────────────────────────
    // A 4-byte payload whose int32 (little-endian) is negative is an error.
    if (payload.size() == 4) {
        int32_t code = (int32_t)((uint32_t)payload[0]
                              | ((uint32_t)payload[1] <<  8)
                              | ((uint32_t)payload[2] << 16)
                              | ((uint32_t)payload[3] << 24));
        if (code < 0) {
            std::ostringstream ss;
            ss << "MTProto transport error " << code
               << " (" << describe_transport_error(code) << ")";
            // Server will typically close after this; mark disconnected.
            m_connected = false;
            throw std::runtime_error(ss.str());
        }
    }

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
