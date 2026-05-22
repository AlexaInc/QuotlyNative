// this file is part of AlexaInc / QuotlyNative — Crypto
// developer hansaka@alexainc
//
// FIXES (2026-05-22, revised v4):
//   1. rsa_encrypt: REVERTED to MTProto 1.0 padding (SHA1+data+rand→255→RSA).
//      Pyrogram, Telethon, MadelineProto and TDLib all use this format for
//      production DCs — Telegram accepts it. The RSA_PAD form described in
//      the spec is required for some flows but NOT for plain p_q_inner_data_dc;
//      using RSA_PAD here causes the server to return transport-error -404.
//   2. make_tagged_nonce no longer XORs a constant tag into the nonce
//      (kept from earlier revision; it was a harmless no-op but obscured
//      real debugging).
//   3. pollard_rho replaced with Brent's variant + OpenSSL RNG seeding
//      (kept from earlier revision).

#include "crypto.h"
#include <openssl/sha.h>
#include <openssl/aes.h>
#include <openssl/rand.h>
#include <openssl/bn.h>
#include <openssl/evp.h>
#include <stdexcept>
#include <cstring>
#include <algorithm>
#include <cmath>

namespace MTProto {
namespace Crypto {

Bytes sha1(const Bytes& data) {
    Bytes out(20); SHA1(data.data(), data.size(), out.data()); return out;
}
Bytes sha256(const Bytes& data) {
    Bytes out(32); SHA256(data.data(), data.size(), out.data()); return out;
}

static void evp_aes_block(const uint8_t* key, const uint8_t* in, uint8_t* out, bool encrypt) {
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    int outl = 0;
    if (encrypt) {
        EVP_EncryptInit_ex(ctx, EVP_aes_256_ecb(), nullptr, key, nullptr);
        EVP_CIPHER_CTX_set_padding(ctx, 0);
        EVP_EncryptUpdate(ctx, out, &outl, in, 16);
    } else {
        EVP_DecryptInit_ex(ctx, EVP_aes_256_ecb(), nullptr, key, nullptr);
        EVP_CIPHER_CTX_set_padding(ctx, 0);
        EVP_DecryptUpdate(ctx, out, &outl, in, 16);
    }
    EVP_CIPHER_CTX_free(ctx);
}

Bytes aes_ige_encrypt(const Bytes& data, const Bytes& key, const Bytes& iv) {
    if (key.size() != 32 || iv.size() != 32)
        throw std::invalid_argument("AES-IGE: key/iv must be 32 bytes");
    if (data.size() % 16 != 0)
        throw std::invalid_argument("AES-IGE: data must be multiple of 16");
    Bytes out(data.size());
    // tgcrypto-compatible IV split (matches Pyrogram, TDLib, MadelineProto):
    //   iv1 = iv[0..16]  XOR'd into plaintext BEFORE AES (the "ciphertext" role)
    //   iv2 = iv[16..32] XOR'd into AES output    (the "plaintext"  role)
    // The previous code had this swapped, producing a self-consistent ciphertext
    // that the real Telegram server could not decrypt.
    uint8_t iv1[16], iv2[16];
    std::memcpy(iv1, iv.data(),      16);
    std::memcpy(iv2, iv.data() + 16, 16);

    for (size_t i = 0; i < data.size(); i += 16) {
        uint8_t chunk[16], tmp[16], block_out[16];
        std::memcpy(chunk, data.data() + i, 16);
        for (int j = 0; j < 16; ++j) tmp[j] = chunk[j] ^ iv1[j];
        evp_aes_block(key.data(), tmp, block_out, true);
        for (int j = 0; j < 16; ++j) block_out[j] ^= iv2[j];
        std::memcpy(out.data() + i, block_out, 16);
        std::memcpy(iv1, block_out, 16);  // next iv1 = current ciphertext
        std::memcpy(iv2, chunk,     16);  // next iv2 = current plaintext
    }
    return out;
}

Bytes aes_ige_decrypt(const Bytes& data, const Bytes& key, const Bytes& iv) {
    if (key.size() != 32 || iv.size() != 32)
        throw std::invalid_argument("AES-IGE: key/iv must be 32 bytes");
    if (data.size() % 16 != 0)
        throw std::invalid_argument("AES-IGE: data must be multiple of 16");
    Bytes out(data.size());
    // For decrypt, tgcrypto swaps the initial assignment:
    //   iv2 = iv[0..16],  iv1 = iv[16..32]
    uint8_t iv1[16], iv2[16];
    std::memcpy(iv2, iv.data(),      16);
    std::memcpy(iv1, iv.data() + 16, 16);

    for (size_t i = 0; i < data.size(); i += 16) {
        uint8_t chunk[16], tmp[16], block_out[16];
        std::memcpy(chunk, data.data() + i, 16);
        for (int j = 0; j < 16; ++j) tmp[j] = chunk[j] ^ iv1[j];
        evp_aes_block(key.data(), tmp, block_out, false);
        for (int j = 0; j < 16; ++j) block_out[j] ^= iv2[j];
        std::memcpy(out.data() + i, block_out, 16);
        std::memcpy(iv1, block_out, 16);  // next iv1 = current decrypted plaintext
        std::memcpy(iv2, chunk,     16);  // next iv2 = current ciphertext
    }
    return out;
}

// ── RSA: MTProto 1.0 padding (the form actually used by production DCs) ──────
// Wire format:
//   data_with_hash = SHA1(data) + data + random_padding_to_255_bytes
//   ciphertext     = pow(data_with_hash, e, n) as 256-byte big-endian
//
// This is what Pyrogram, Telethon, MadelineProto, gramjs and TDLib all use
// for p_q_inner_data_dc encryption in req_DH_params. The RSA_PAD/OAEP+
// variant described in section 4.1 of core.telegram.org/mtproto/auth_key
// is required for temporary key creation (p_q_inner_data_temp_dc) but
// production servers reject it for regular handshakes with transport
// error -404.
Bytes rsa_encrypt(const Bytes& data, const Bytes& n, const Bytes& e) {
    if (data.size() > 235)
        throw std::runtime_error("RSA: inner data > 235 bytes (max 255 - 20 SHA1)");

    Bytes hash = sha1(data);

    Bytes padded;
    padded.reserve(255);
    padded.insert(padded.end(), hash.begin(), hash.end());     // 20 bytes
    padded.insert(padded.end(), data.begin(), data.end());     // up to 235

    size_t pad_size = 255 - padded.size();
    if (pad_size) {
        Bytes pad = random_bytes(pad_size);
        padded.insert(padded.end(), pad.begin(), pad.end());
    }
    if (padded.size() != 255)
        throw std::runtime_error("RSA: padded size != 255");

    BIGNUM* bn_msg = BN_bin2bn(padded.data(), 255, nullptr);
    BIGNUM* bn_n   = BN_bin2bn(n.data(), static_cast<int>(n.size()), nullptr);
    BIGNUM* bn_e   = BN_bin2bn(e.data(), static_cast<int>(e.size()), nullptr);
    BIGNUM* bn_res = BN_new();
    BN_CTX* ctx    = BN_CTX_new();

    BN_mod_exp(bn_res, bn_msg, bn_e, bn_n, ctx);

    Bytes result(256, 0);
    int len = BN_num_bytes(bn_res);
    BN_bn2bin(bn_res, result.data() + 256 - len);

    BN_free(bn_msg); BN_free(bn_n); BN_free(bn_e); BN_free(bn_res);
    BN_CTX_free(ctx);
    return result;
}

Bytes mod_exp(const Bytes& base, const Bytes& exp, const Bytes& mod) {
    BIGNUM* bn_base = BN_bin2bn(base.data(), base.size(), nullptr);
    BIGNUM* bn_exp  = BN_bin2bn(exp.data(),  exp.size(),  nullptr);
    BIGNUM* bn_mod  = BN_bin2bn(mod.data(),  mod.size(),  nullptr);
    BIGNUM* bn_res  = BN_new();
    BN_CTX* ctx     = BN_CTX_new();
    BN_mod_exp(bn_res, bn_base, bn_exp, bn_mod, ctx);
    size_t mod_size = mod.size();
    Bytes result(mod_size, 0);
    int len = BN_num_bytes(bn_res);
    BN_bn2bin(bn_res, result.data() + mod_size - len);
    BN_free(bn_base); BN_free(bn_exp); BN_free(bn_mod); BN_free(bn_res);
    BN_CTX_free(ctx);
    return result;
}

// ── Brent's rho ──────────────────────────────────────────────────────────────
static uint64_t gcd_u64(uint64_t a, uint64_t b) {
    while (b) { a %= b; std::swap(a, b); } return a;
}
static uint64_t secure_rand_u64() {
    uint64_t v;
    if (RAND_bytes(reinterpret_cast<unsigned char*>(&v), sizeof(v)) != 1)
        throw std::runtime_error("RAND_bytes failed");
    return v;
}
static uint64_t brent_rho(uint64_t n) {
    if (n % 2 == 0) return 2;
    if (n <= 3) return n;
    while (true) {
        uint64_t y = secure_rand_u64() % (n - 1) + 1;
        uint64_t c = secure_rand_u64() % (n - 1) + 1;
        uint64_t m = secure_rand_u64() % (n - 1) + 1;
        uint64_t g = 1, r = 1, q = 1, x = 0, ys = 0;
        while (g == 1) {
            x = y;
            for (uint64_t i = 0; i < r; ++i)
                y = ((__uint128_t)y * y + c) % n;
            uint64_t k = 0;
            while (k < r && g == 1) {
                ys = y;
                uint64_t limit = std::min<uint64_t>(m, r - k);
                for (uint64_t i = 0; i < limit; ++i) {
                    y = ((__uint128_t)y * y + c) % n;
                    uint64_t diff = (x > y) ? (x - y) : (y - x);
                    q = (__uint128_t)q * diff % n;
                }
                g = gcd_u64(q, n);
                k += m;
            }
            r *= 2;
        }
        if (g == n) {
            do {
                ys = ((__uint128_t)ys * ys + c) % n;
                uint64_t diff = (x > ys) ? (x - ys) : (ys - x);
                g = gcd_u64(diff, n);
            } while (g == 1);
        }
        if (g != n) return g;
    }
}

std::pair<uint32_t, uint32_t> factorize_pq(const Bytes& pq_bytes) {
    uint64_t pq = 0;
    for (uint8_t b : pq_bytes) pq = (pq << 8) | b;
    if (pq < 2) throw std::runtime_error("factorize_pq: pq < 2");
    uint64_t p = 0;
    for (int tries = 0; tries < 64; ++tries) {
        uint64_t f = brent_rho(pq);
        if (f > 1 && f < pq && pq % f == 0) { p = f; break; }
    }
    if (p == 0) throw std::runtime_error("factorize_pq: failed");
    uint64_t q = pq / p;
    if (p > q) std::swap(p, q);
    return { static_cast<uint32_t>(p), static_cast<uint32_t>(q) };
}

Bytes tl_encode_bytes_correct(const Bytes& data) {
    Bytes out;
    size_t len = data.size();
    if (len < 254) out.push_back(static_cast<uint8_t>(len));
    else {
        out.push_back(0xfe);
        out.push_back(len & 0xff);
        out.push_back((len >> 8) & 0xff);
        out.push_back((len >> 16) & 0xff);
    }
    out.insert(out.end(), data.begin(), data.end());
    while (out.size() % 4 != 0) out.push_back(0);
    return out;
}

int64_t rsa_fingerprint(const Bytes& n, const Bytes& e) {
    Bytes combined;
    auto nb = tl_encode_bytes_correct(n);
    auto eb = tl_encode_bytes_correct(e);
    combined.insert(combined.end(), nb.begin(), nb.end());
    combined.insert(combined.end(), eb.begin(), eb.end());
    Bytes h = sha1(combined);
    int64_t fp = 0;
    for (int i = 0; i < 8; ++i) fp |= (int64_t)h[12 + i] << (8 * i);
    return fp;
}

Bytes random_bytes(size_t count) {
    Bytes out(count);
    if (RAND_bytes(out.data(), static_cast<int>(count)) != 1)
        throw std::runtime_error("RAND_bytes failed");
    return out;
}

Bytes xor_bytes(const Bytes& a, const Bytes& b) {
    Bytes out(a.size());
    for (size_t i = 0; i < a.size(); ++i) out[i] = a[i] ^ b[i];
    return out;
}

// Telegram production RSA key #2 (fingerprint 0x0BC35F3509F7B7A5)
// Key #1 (0xC3B42B026CE86B21) is still advertised by the server
// but in practice production DCs reject handshakes encrypted with it.
// Pyrogram, Telethon, MadelineProto all prefer key #2 today.
static const uint8_t TG_RSA_N_BYTES[] = {
    0xae, 0xec, 0x36, 0xc8, 0xff, 0xc1, 0x09, 0xcb, 0x09, 0x96, 0x24, 0x68,
    0x5b, 0x97, 0x81, 0x54, 0x15, 0x65, 0x7b, 0xd7, 0x6d, 0x8c, 0x9c, 0x3e,
    0x39, 0x81, 0x03, 0xd7, 0xad, 0x16, 0xc9, 0xbb, 0xa6, 0xf5, 0x25, 0xed,
    0x04, 0x12, 0xd7, 0xae, 0x2c, 0x2d, 0xe2, 0xb4, 0x4e, 0x77, 0xd7, 0x2c,
    0xbf, 0x4b, 0x74, 0x38, 0x70, 0x9a, 0x4e, 0x64, 0x6a, 0x05, 0xc4, 0x34,
    0x27, 0xc7, 0xf1, 0x84, 0xde, 0xbf, 0x72, 0x94, 0x75, 0x19, 0x68, 0x0e,
    0x65, 0x15, 0x00, 0x89, 0x0c, 0x68, 0x32, 0x79, 0x6d, 0xd1, 0x1f, 0x77,
    0x2c, 0x25, 0xff, 0x8f, 0x57, 0x67, 0x55, 0xaf, 0xe0, 0x55, 0xb0, 0xa3,
    0x75, 0x2c, 0x69, 0x6e, 0xb7, 0xd8, 0xda, 0x0d, 0x8b, 0xe1, 0xfa, 0xf3,
    0x8c, 0x9b, 0xdd, 0x97, 0xce, 0x0a, 0x77, 0xd3, 0x91, 0x62, 0x30, 0xc4,
    0x03, 0x21, 0x67, 0x10, 0x0e, 0xdd, 0x0f, 0x9e, 0x7a, 0x3a, 0x9b, 0x60,
    0x2d, 0x04, 0x36, 0x7b, 0x68, 0x95, 0x36, 0xaf, 0x0d, 0x64, 0xb6, 0x13,
    0xcc, 0xba, 0x79, 0x62, 0x93, 0x9d, 0x3b, 0x57, 0x68, 0x2b, 0xeb, 0x6d,
    0xae, 0x5b, 0x60, 0x81, 0x30, 0xb2, 0xe5, 0x2a, 0xca, 0x78, 0xba, 0x02,
    0x3c, 0xf6, 0xce, 0x80, 0x6b, 0x1d, 0xc4, 0x9c, 0x72, 0xcf, 0x92, 0x8a,
    0x71, 0x99, 0xd2, 0x2e, 0x3d, 0x7a, 0xc8, 0x4e, 0x47, 0xbc, 0x94, 0x27,
    0xd0, 0x23, 0x69, 0x45, 0xd1, 0x0d, 0xbd, 0x15, 0x17, 0x7b, 0xab, 0x41,
    0x3f, 0xbf, 0x0e, 0xdf, 0xda, 0x09, 0xf0, 0x14, 0xc7, 0xa7, 0xda, 0x08,
    0x8d, 0xde, 0x97, 0x59, 0x70, 0x2c, 0xa7, 0x60, 0xaf, 0x2b, 0x8e, 0x4e,
    0x97, 0xcc, 0x05, 0x5c, 0x61, 0x7b, 0xd7, 0x4c, 0x3d, 0x97, 0x00, 0x86,
    0x35, 0xb9, 0x8d, 0xc4, 0xd6, 0x21, 0xb4, 0x89, 0x1d, 0xa9, 0xfb, 0x04,
    0x73, 0x04, 0x79, 0x27
};
static const uint8_t TG_RSA_E_BYTES[] = { 0x01, 0x00, 0x01 };

const Bytes& tg_rsa_n() {
    static const Bytes n(TG_RSA_N_BYTES, TG_RSA_N_BYTES + sizeof(TG_RSA_N_BYTES));
    return n;
}
const Bytes& tg_rsa_e() {
    static const Bytes e(TG_RSA_E_BYTES, TG_RSA_E_BYTES + sizeof(TG_RSA_E_BYTES));
    return e;
}
int64_t tg_rsa_fingerprint() {
    static const int64_t fp = rsa_fingerprint(tg_rsa_n(), tg_rsa_e());
    return fp;
}

const Bytes& alexainc_identity_tag() {
    static const Bytes tag = []() -> Bytes {
        const std::string seed = std::string("AlexaInc") + '\0' + "QuotlyNative";
        Bytes src(seed.begin(), seed.end());
        return sha256(src);
    }();
    return tag;
}
Bytes make_tagged_nonce() { return random_bytes(32); }

} // namespace Crypto
} // namespace MTProto
