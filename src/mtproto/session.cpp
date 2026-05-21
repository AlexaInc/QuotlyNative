// this file is part of AlexaInc / QuotlyNative — MTProto Session
// developer hansaka@alexainc

#include "session.h"
#include "crypto.h"
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <iostream>

namespace MTProto {

using namespace Crypto;

Session::Session(Transport& transport, const AuthKey& auth_key)
    : m_transport(transport)
    , m_auth_key(auth_key)
    , m_server_salt(auth_key.server_salt)
{
    // Random 64-bit session id
    Bytes rnd = random_bytes(8);
    m_session_id = 0;
    for (int i = 0; i < 8; ++i)
        m_session_id |= (int64_t)rnd[i] << (8 * i);
}

// ── msg_id: Unix time × 2^32, +4 per call, must be divisible by 4 ────────────
int64_t Session::next_msg_id() {
    using namespace std::chrono;
    auto ns = system_clock::now().time_since_epoch();
    int64_t sec  = duration_cast<seconds>(ns).count();
    int64_t frac = (duration_cast<nanoseconds>(ns).count() % 1'000'000'000LL)
                    * 4294967296LL / 1'000'000'000LL;
    int64_t msg_id = (sec << 32) | (frac & 0xFFFFFFFC);
    return msg_id;
}

int32_t Session::next_seq_no(bool content_related) {
    if (content_related) {
        int32_t s = m_seq_no.fetch_add(2);
        return s | 1; // odd for content-related
    }
    return m_seq_no.load(); // even for administrative
}

// ── MTProto message key (client→server) ──────────────────────────────────────
// msg_key = SHA256(auth_key[88:120] + plaintext)[8:24]  (middle 16 bytes)
static Bytes compute_msg_key(const Bytes& auth_key, const Bytes& plaintext) {
    Bytes buf(auth_key.begin() + 88, auth_key.begin() + 120);
    buf.insert(buf.end(), plaintext.begin(), plaintext.end());
    Bytes h = sha256(buf);
    return Bytes(h.begin() + 8, h.begin() + 24);
}

// Derive AES key+iv from msg_key and auth_key (client side, x=0)
static void derive_aes(const Bytes& auth_key, const Bytes& msg_key,
                        int x, Bytes& aes_key, Bytes& aes_iv) {
    Bytes sha_a_in, sha_b_in;
    sha_a_in.insert(sha_a_in.end(), msg_key.begin(), msg_key.end());
    sha_a_in.insert(sha_a_in.end(), auth_key.begin()+x,    auth_key.begin()+x+36);
    Bytes sha_a = sha256(sha_a_in);

    sha_b_in.insert(sha_b_in.end(), auth_key.begin()+x+40, auth_key.begin()+x+76);
    sha_b_in.insert(sha_b_in.end(), msg_key.begin(),       msg_key.end());
    Bytes sha_b = sha256(sha_b_in);

    aes_key.clear();
    aes_key.insert(aes_key.end(), sha_a.begin(),      sha_a.begin()+8);
    aes_key.insert(aes_key.end(), sha_b.begin()+8,    sha_b.begin()+24);
    aes_key.insert(aes_key.end(), sha_a.begin()+24,   sha_a.begin()+32);

    aes_iv.clear();
    aes_iv.insert(aes_iv.end(), sha_b.begin(),      sha_b.begin()+8);
    aes_iv.insert(aes_iv.end(), sha_a.begin()+8,    sha_a.begin()+24);
    aes_iv.insert(aes_iv.end(), sha_b.begin()+24,   sha_b.begin()+32);
}

// ── Encrypt and send ──────────────────────────────────────────────────────────
int64_t Session::send(const Bytes& payload, bool is_content_related) {
    int64_t msg_id  = next_msg_id();
    int32_t seq_no  = next_seq_no(is_content_related);

    // Plaintext inner = server_salt(8) + session_id(8) + msg_id(8) + seq_no(4)
    //                   + data_len(4) + payload + padding
    Bytes plain;
    auto wLE64 = [&](int64_t v) {
        for (int i = 0; i < 8; ++i) plain.push_back((v >> (8*i)) & 0xff);
    };
    auto wLE32 = [&](int32_t v) {
        for (int i = 0; i < 4; ++i) plain.push_back((v >> (8*i)) & 0xff);
    };

    wLE64(m_server_salt);
    wLE64(m_session_id);
    wLE64(msg_id);
    wLE32(seq_no);
    wLE32(static_cast<int32_t>(payload.size()));
    plain.insert(plain.end(), payload.begin(), payload.end());

    // Padding: 12 to 1024 random bytes, aligned to 16 bytes
    size_t pad_min = 12;
    size_t total = plain.size() + pad_min;
    size_t pad = pad_min + ((16 - total % 16) % 16);
    Bytes rnd = random_bytes(pad);
    plain.insert(plain.end(), rnd.begin(), rnd.end());

    // Compute msg_key and AES params
    Bytes msg_key = compute_msg_key(m_auth_key.key, plain);
    Bytes aes_key, aes_iv;
    derive_aes(m_auth_key.key, msg_key, 0, aes_key, aes_iv);

    Bytes encrypted = aes_ige_encrypt(plain, aes_key, aes_iv);

    // Frame: auth_key_id(8) + msg_key(16) + encrypted
    Bytes frame;
    for (int i = 0; i < 8; ++i)
        frame.push_back((m_auth_key.key_id >> (8*i)) & 0xff);
    frame.insert(frame.end(), msg_key.begin(), msg_key.end());
    frame.insert(frame.end(), encrypted.begin(), encrypted.end());

    m_transport.send(frame);
    return msg_id;
}

// ── Receive and decrypt ───────────────────────────────────────────────────────
Bytes Session::recv() {
    Bytes frame = m_transport.recv();
    if (frame.empty()) return {};

    if (frame.size() < 24) throw std::runtime_error("Session::recv: frame too short");

    // auth_key_id (8) + msg_key (16) + encrypted_data
    int64_t recv_key_id = 0;
    for (int i = 0; i < 8; ++i)
        recv_key_id |= (int64_t)frame[i] << (8*i);
    if (recv_key_id != m_auth_key.key_id)
        throw std::runtime_error("Session::recv: auth_key_id mismatch");

    Bytes msg_key(frame.begin() + 8, frame.begin() + 24);
    Bytes encrypted(frame.begin() + 24, frame.end());

    // Derive AES params for server→client (x=8)
    Bytes aes_key, aes_iv;
    derive_aes(m_auth_key.key, msg_key, 8, aes_key, aes_iv);

    Bytes plain = aes_ige_decrypt(encrypted, aes_key, aes_iv);

    // Parse inner: salt(8) + session_id(8) + msg_id(8) + seq_no(4) + data_len(4) + payload
    if (plain.size() < 32) throw std::runtime_error("Session::recv: decrypted too short");
    size_t pos = 16; // skip salt + session_id
    pos += 8;        // msg_id
    pos += 4;        // seq_no
    int32_t data_len = 0;
    for (int i = 0; i < 4; ++i)
        data_len |= (int32_t)plain[pos++] << (8*i);

    if (pos + data_len > plain.size())
        throw std::runtime_error("Session::recv: data_len out of bounds");

    return Bytes(plain.begin() + pos, plain.begin() + pos + data_len);
}

Bytes Session::make_ack(int64_t msg_id) {
    TLWriter w;
    w.writeInt32(TL::msgs_ack);
    w.writeInt32(TL::vector);
    w.writeInt32(1);
    w.writeInt64(msg_id);
    return w.data();
}

} // namespace MTProto
