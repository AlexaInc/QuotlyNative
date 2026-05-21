#pragma once
// this file is part of AlexaInc / QuotlyNative — MTProto Encrypted Session Layer
// Handles message encryption/decryption using the established AuthKey.
// developer hansaka@alexainc

#include "auth_key.h"
#include "transport.h"
#include "tl.h"
#include <cstdint>
#include <atomic>

namespace MTProto {

// A Session wraps a Transport + AuthKey into an encrypted MTProto channel.
// It manages msg_id counters, seq_no, and AES-IGE message framing.
class Session {
public:
    explicit Session(Transport& transport, const AuthKey& auth_key);

    // Encrypt and send a TL payload (returns message_id used)
    int64_t send(const Bytes& payload, bool is_content_related = true);

    // Receive and decrypt one message. Returns the inner TL payload.
    // Returns empty Bytes on transport disconnect.
    Bytes recv();

    // Build a msgs_ack TL object for a given message_id
    static Bytes make_ack(int64_t msg_id);

    int64_t session_id()   const { return m_session_id; }
    int64_t server_salt()  const { return m_server_salt; }
    void    set_server_salt(int64_t s) { m_server_salt = s; }

private:
    int64_t next_msg_id();
    int32_t next_seq_no(bool content_related);

    Transport&    m_transport;
    AuthKey       m_auth_key;
    int64_t       m_session_id;
    int64_t       m_server_salt;
    std::atomic<int32_t> m_seq_no{ 0 };
};

} // namespace MTProto
