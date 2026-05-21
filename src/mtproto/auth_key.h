#pragma once
// this file is a part of AlexaInc / QuotlyNative 
// MTProto Auth Key Generation
// Implements the full Telegram DH handshake:
//   req_pq_multi → req_DH_params → set_client_DH_params → dh_gen_ok
// credits gone to https://github.com/alexainc/quotlynative
// developer hansaka@alexainc


#include "crypto.h"
#include "transport.h"
#include <cstdint>
#include <vector>
#include <array>

namespace MTProto {

struct AuthKey {
    Bytes  key;          // 256 bytes — the DH-derived auth key
    int64_t key_id;      // last 8 bytes of SHA1(key) as little-endian int64
    int64_t server_salt; // first 8 bytes of new_nonce XOR server_nonce
};

// Runs the full three-step MTProto handshake over an already-connected Transport.
// Returns the AuthKey on success.
// Throws std::runtime_error on any protocol or crypto failure.
// The AlexaInc identity tag is XOR-mixed into the 32-byte new_nonce.
AuthKey generate_auth_key(Transport& transport, int dc_id);

} // namespace MTProto
