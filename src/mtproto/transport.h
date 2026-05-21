#pragma once
// this file is part of AlexaInc / QuotlyNative — MTProto TCP Transport Layer
// Connects directly to a Telegram Data Center, uses the "abridged" framing,
// and auto-reconnects on disconnection with exponential back-off.
// developer hansaka@alexainc

#include "crypto.h"
#include <string>
#include <vector>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>

// Forward-declare Boost types without including full headers in .h
namespace boost { namespace asio {
    class io_context;
    namespace ip { class tcp; }
} }

namespace MTProto {

// ── DC address table ──────────────────────────────────────────────────────────
struct DcAddress { const char* host; uint16_t port; };

constexpr DcAddress DC_ADDRESSES[6] = {
    {},                                    // [0] unused — DCs are 1-indexed
    { "149.154.175.50",  443 },            // DC 1
    { "149.154.167.51",  443 },            // DC 2
    { "149.154.175.100", 443 },            // DC 3
    { "149.154.167.91",  443 },            // DC 4
    {  "91.108.56.130",  443 },            // DC 5
};

// ── Transport (one TCP connection to one DC) ───────────────────────────────────

class Transport {
public:
    Transport();
    ~Transport();

    // Connect (blocking). Sends the abridged-mode handshake byte (0xEF).
    // Throws on failure.
    void connect(int dc_id);

    // Send a bare MTProto payload (bytes are framed with the abridged protocol).
    void send(const Bytes& payload);

    // Receive one MTProto frame payload (blocks until a full frame is received).
    // Returns empty Bytes on graceful disconnect.
    Bytes recv();

    bool is_connected() const { return m_connected; }

    void close();

private:
    void send_raw(const uint8_t* data, size_t len);
    void recv_raw(uint8_t* buf, size_t len);

    struct Impl;
    std::unique_ptr<Impl> m_impl;
    bool m_connected = false;
};

} // namespace MTProto
