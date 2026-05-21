// this file is part of AlexaInc / QuotlyNative — TL Serialization
// developer hansaka@alexainc

#pragma once
#include <vector>
#include <cstdint>
#include <string>

namespace MTProto {

using Bytes = std::vector<uint8_t>;

// ── TL Writer ─────────────────────────────────────────────────────────────────

class TLWriter {
public:
    TLWriter() = default;

    void writeInt32(int32_t v);
    void writeInt64(int64_t v);
    void writeInt128(const uint8_t* v);   // writes 16 bytes
    void writeInt256(const uint8_t* v);   // writes 32 bytes
    void writeBytes(const Bytes& b);      // TL bytes: length-prefixed, padded to 4-byte alignment
    void writeString(const std::string& s);
    void writeBigEndian32(uint32_t v);    // used for p, q fields

    const Bytes& data() const { return m_data; }
    size_t size() const { return m_data.size(); }

private:
    Bytes m_data;
};

// ── TL Reader ─────────────────────────────────────────────────────────────────

class TLReader {
public:
    explicit TLReader(const Bytes& data, size_t offset = 0);

    int32_t  readInt32();
    int64_t  readInt64();
    Bytes    readInt128();              // reads 16 bytes
    Bytes    readInt256();              // reads 32 bytes
    Bytes    readBytes();              // TL bytes: length-prefixed
    std::string readString();

    Bytes    readRaw(size_t n);
    bool     atEnd() const;
    size_t   pos() const { return m_pos; }
    size_t   remaining() const;
    void     skip(size_t n);

private:
    const Bytes& m_data;
    size_t m_pos;
};

// ── TL Constructor IDs (MTProto handshake messages) ───────────────────────────

namespace TL {
    constexpr int32_t req_pq_multi          = (int32_t)0xbe7e8ef1u;
    constexpr int32_t resPQ                 = (int32_t)0x05162463u;
    constexpr int32_t p_q_inner_data_dc     = (int32_t)0xa9f55f95u;
    constexpr int32_t req_DH_params         = (int32_t)0xd712e4beu;
    constexpr int32_t server_DH_params_ok   = (int32_t)0xd0e8075cu;
    constexpr int32_t server_DH_params_fail = (int32_t)0x79cb045du;
    constexpr int32_t server_DH_inner_data  = (int32_t)0xb5890dbau;
    constexpr int32_t client_DH_inner_data  = (int32_t)0x6643b654u;
    constexpr int32_t set_client_DH_params  = (int32_t)0xf5045f1fu;
    constexpr int32_t dh_gen_ok             = (int32_t)0x3bcbf734u;
    constexpr int32_t dh_gen_retry          = (int32_t)0x46dc1fb9u;
    constexpr int32_t dh_gen_fail           = (int32_t)0xa69dae02u;
    constexpr int32_t vector                = (int32_t)0x1cb5c415u;
    constexpr int32_t gzip_packed           = (int32_t)0x3072cfa1u;
    constexpr int32_t msgs_ack              = (int32_t)0x62d6b459u;
    constexpr int32_t rpc_result            = (int32_t)0xf35c6d01u;
    constexpr int32_t rpc_error             = (int32_t)0x2144ca19u;
    constexpr int32_t new_session_created   = (int32_t)0x9ec20908u;
    constexpr int32_t bad_server_salt       = (int32_t)0xedab447bu;
    constexpr int32_t ping                  = (int32_t)0x7abe77ecu;
    constexpr int32_t ping_delay_disconnect = (int32_t)0xf3427b8cu;
    constexpr int32_t pong                  = (int32_t)0x347773c5u;
    constexpr int32_t msg_container        = (int32_t)0x73f1f8dcu;
} // namespace TL

} // namespace MTProto
