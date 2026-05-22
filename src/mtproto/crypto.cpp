// this file is part of AlexaInc / QuotlyNative — Crypto
// developer hansaka@alexainc
//
// FIXES (2026-05-22):
//   1. rsa_encrypt now implements MTProto 2.0 RSA_PAD (required since 2019).
//   2. make_tagged_nonce no longer XORs a constant tag into the nonce.
//      The XOR was harmless to the math but added zero auditability and
//      complicated debugging. Nonces are now plain cryptographic randoms.
//   3. pollard_rho replaced with Brent's variant + OpenSSL RNG seeding.
//   4. factorize_pq retries on degenerate factorizations.

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

// ── SHA1 / SHA256 ─────────────────────────────────────────────────────────────

Bytes sha1(const Bytes& data) {
    Bytes out(20);
    SHA1(data.data(), data.size(), out.data());
    return out;
}

Bytes sha256(const Bytes& data) {
    Bytes out(32);
    SHA256(data.data(), data.size(), out.data());
    return out;
}

// ── AES-256-IGE ───────────────────────────────────────────────────────────────

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
        throw std::invalid_argument("AES-IGE: key must be 32 bytes, iv must be 32 bytes");
    if (data.size() % 16 != 0)
        throw std::invalid_argument("AES-IGE: data must be multiple of 16 bytes");

    Bytes out(data.size());
    uint8_t prev_cipher[16], prev_plain[16];
    std::memcpy(prev_plain,  iv.data(),      16);
    std::memcpy(prev_cipher, iv.data() + 16, 16);

    for (size_t i = 0; i < data.size(); i += 16) {
        uint8_t tmp[16], block_out[16];
        for (int j = 0; j < 16; ++j) tmp[j] = data[i + j] ^ prev_cipher[j];
        evp_aes_block(key.data(), tmp, block_out, true);
        for (int j = 0; j < 16; ++j) block_out[j] ^= prev_plain[j];
        std::memcpy(out.data() + i, block_out, 16);
        std::memcpy(prev_plain,  data.data() + i, 16);
        std::memcpy(prev_cipher, block_out, 16);
    }
    return out;
}

Bytes aes_ige_decrypt(const Bytes& data, const Bytes& key, const Bytes& iv) {
    if (key.size() != 32 || iv.size() != 32)
        throw std::invalid_argument("AES-IGE: key must be 32 bytes, iv must be 32 bytes");
    if (data.size() % 16 != 0)
        throw std::invalid_argument("AES-IGE: data must be multiple of 16 bytes");

    Bytes out(data.size());
    uint8_t x_prev[16], c_prev[16];
    std::memcpy(x_prev, iv.data(),      16);
    std::memcpy(c_prev, iv.data() + 16, 16);

    for (size_t i = 0; i < data.size(); i += 16) {
        uint8_t tmp[16], block_out[16];
        for (int j = 0; j < 16; ++j) tmp[j] = data[i + j] ^ x_prev[j];
        evp_aes_block(key.data(), tmp, block_out, false);
        for (int j = 0; j < 16; ++j) block_out[j] ^= c_prev[j];
        std::memcpy(out.data() + i, block_out, 16);
        std::memcpy(c_prev, data.data() + i, 16);
        std::memcpy(x_prev, block_out, 16);
    }
    return out;
}

// ── RSA: MTProto 2.0 RSA_PAD (required since 2019) ────────────────────────────
//
// Reference: https://core.telegram.org/mtproto/auth_key#41-rsa-paddata-server-public-key-mentioned-above-is-implemented-as-follows
//
//   data_with_padding   = data + random_padding_bytes   (192 bytes total)
//   data_pad_reversed   = reverse(data_with_padding)
//   loop:
//     temp_key          = random(32)
//     data_with_hash    = data_pad_reversed + SHA256(temp_key + data_with_padding)
//     aes_encrypted     = AES-256-IGE(data_with_hash, key=temp_key, iv=0)   // 224 bytes
//     temp_key_xor      = temp_key XOR SHA256(aes_encrypted)
//     key_aes_encrypted = temp_key_xor + aes_encrypted                      // 256 bytes
//   until int(key_aes_encrypted, big-endian) < n
//   encrypted_data      = RSA(key_aes_encrypted, n, e)                      // 256 bytes

Bytes rsa_encrypt(const Bytes& data, const Bytes& n, const Bytes& e) {
    if (data.size() > 192)
        throw std::runtime_error("RSA_PAD: inner data > 192 bytes");

    BIGNUM* bn_n = BN_bin2bn(n.data(), static_cast<int>(n.size()), nullptr);
    BIGNUM* bn_e = BN_bin2bn(e.data(), static_cast<int>(e.size()), nullptr);
    BN_CTX* ctx  = BN_CTX_new();

    Bytes result(256, 0);

    for (int attempt = 0; attempt < 32; ++attempt) {
        Bytes data_with_padding = data;
        if (data_with_padding.size() < 192) {
            Bytes pad = random_bytes(192 - data_with_padding.size());
            data_with_padding.insert(data_with_padding.end(), pad.begin(), pad.end());
        }

        Bytes data_pad_reversed(data_with_padding.rbegin(), data_with_padding.rend());

        Bytes temp_key = random_bytes(32);

        Bytes h_in; h_in.reserve(temp_key.size() + data_with_padding.size());
        h_in.insert(h_in.end(), temp_key.begin(),          temp_key.end());
        h_in.insert(h_in.end(), data_with_padding.begin(), data_with_padding.end());
        Bytes h = sha256(h_in);

        Bytes data_with_hash = data_pad_reversed;
        data_with_hash.insert(data_with_hash.end(), h.begin(), h.end());
        if (data_with_hash.size() != 224) {
            BN_free(bn_n); BN_free(bn_e); BN_CTX_free(ctx);
            throw std::runtime_error("RSA_PAD: data_with_hash size != 224");
        }

        Bytes zero_iv(32, 0);
        Bytes aes_encrypted = aes_ige_encrypt(data_with_hash, temp_key, zero_iv);

        Bytes hk = sha256(aes_encrypted);
        Bytes temp_key_xor(32);
        for (int i = 0; i < 32; ++i) temp_key_xor[i] = temp_key[i] ^ hk[i];

        Bytes key_aes_encrypted;
        key_aes_encrypted.reserve(256);
        key_aes_encrypted.insert(key_aes_encrypted.end(), temp_key_xor.begin(), temp_key_xor.end());
        key_aes_encrypted.insert(key_aes_encrypted.end(), aes_encrypted.begin(), aes_encrypted.end());

        BIGNUM* bn_msg = BN_bin2bn(key_aes_encrypted.data(), 256, nullptr);
        if (BN_cmp(bn_msg, bn_n) >= 0) {
            BN_free(bn_msg);
            continue;
        }

        BIGNUM* bn_res = BN_new();
        BN_mod_exp(bn_res, bn_msg, bn_e, bn_n, ctx);

        int len = BN_num_bytes(bn_res);
        std::fill(result.begin(), result.end(), 0);
        BN_bn2bin(bn_res, result.data() + 256 - len);

        BN_free(bn_msg);
        BN_free(bn_res);
        BN_free(bn_n); BN_free(bn_e); BN_CTX_free(ctx);
        return result;
    }

    BN_free(bn_n); BN_free(bn_e); BN_CTX_free(ctx);
    throw std::runtime_error("RSA_PAD: failed to find m < n after 32 attempts");
}

// ── DH modular exponentiation ─────────────────────────────────────────────────

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

// ── Pollard's rho (Brent's variant) ───────────────────────────────────────────

static uint64_t gcd_u64(uint64_t a, uint64_t b) {
    while (b) { a %= b; std::swap(a, b); }
    return a;
}

static uint64_t secure_rand_u64() {
    uint64_t v;
    if (RAND_bytes(reinterpret_cast<unsigned char*>(&v), sizeof(v)) != 1)
        throw std::runtime_error("RAND_bytes failed");
    return v;
}

static uint64_t brent_rho(uint64_t n) {
    if (n % 2 == 0) return 2;
    if (n <= 3)      return n;

    while (true) {
        uint64_t y = secure_rand_u64() % (n - 1) + 1;
        uint64_t c = secure_rand_u64() % (n - 1) + 1;
        uint64_t m = secure_rand_u64() % (n - 1) + 1;
        uint64_t g = 1, r = 1, q = 1;
        uint64_t x = 0, ys = 0;

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
    if (p == 0)
        throw std::runtime_error("factorize_pq: failed to factorize");

    uint64_t q = pq / p;
    if (p > q) std::swap(p, q);
    return { static_cast<uint32_t>(p), static_cast<uint32_t>(q) };
}

// ── RSA fingerprint ───────────────────────────────────────────────────────────

Bytes tl_encode_bytes_correct(const Bytes& data) {
    Bytes out;
    size_t len = data.size();
    if (len < 254) {
        out.push_back(static_cast<uint8_t>(len));
    } else {
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
    for (int i = 0; i < 8; ++i)
        fp |= (int64_t)h[12 + i] << (8 * i);
    return fp;
}

// ── Utilities ─────────────────────────────────────────────────────────────────

Bytes random_bytes(size_t count) {
    Bytes out(count);
    if (RAND_bytes(out.data(), static_cast<int>(count)) != 1)
        throw std::runtime_error("RAND_bytes failed");
    return out;
}

Bytes xor_bytes(const Bytes& a, const Bytes& b) {
    Bytes out(a.size());
    for (size_t i = 0; i < a.size(); ++i)
        out[i] = a[i] ^ b[i];
    return out;
}

// ── Telegram Production RSA Key (fingerprint 0xC3B42B026CE86B21) ──────────────

static const uint8_t TG_RSA_N_BYTES[] = {
    0xc1, 0x50, 0x02, 0x3e, 0x2f, 0x70, 0xdb, 0x79, 0x85, 0xde, 0xd0, 0x64,
    0x75, 0x9c, 0xfe, 0xcf, 0x0a, 0xf3, 0x28, 0xe6, 0x9a, 0x41, 0xda, 0xf4,
    0xd6, 0xf0, 0x1b, 0x53, 0x81, 0x35, 0xa6, 0xf9, 0x1f, 0x8f, 0x8b, 0x2a,
    0x0e, 0xc9, 0xba, 0x97, 0x20, 0xce, 0x35, 0x2e, 0xfc, 0xf6, 0xc5, 0x68,
    0x0f, 0xfc, 0x42, 0x4b, 0xd6, 0x34, 0x86, 0x49, 0x02, 0xde, 0x0b, 0x4b,
    0xd6, 0xd4, 0x9f, 0x4e, 0x58, 0x02, 0x30, 0xe3, 0xae, 0x97, 0xd9, 0x5c,
    0x8b, 0x19, 0x44, 0x2b, 0x3c, 0x0a, 0x10, 0xd8, 0xf5, 0x63, 0x3f, 0xec,
    0xed, 0xd6, 0x92, 0x6a, 0x7f, 0x6d, 0xab, 0x0d, 0xdb, 0x7d, 0x45, 0x7f,
    0x9e, 0xa8, 0x1b, 0x84, 0x65, 0xfc, 0xd6, 0xff, 0xfe, 0xed, 0x11, 0x40,
    0x11, 0xdf, 0x91, 0xc0, 0x59, 0xca, 0xed, 0xaf, 0x97, 0x62, 0x5f, 0x6c,
    0x96, 0xec, 0xc7, 0x47, 0x25, 0x55, 0x69, 0x34, 0xef, 0x78, 0x1d, 0x86,
    0x6b, 0x34, 0xf0, 0x11, 0xfc, 0xe4, 0xd8, 0x35, 0xa0, 0x90, 0x19, 0x6e,
    0x9a, 0x5f, 0x0e, 0x44, 0x49, 0xaf, 0x7e, 0xb6, 0x97, 0xdd, 0xb9, 0x07,
    0x64, 0x94, 0xca, 0x5f, 0x81, 0x10, 0x4a, 0x30, 0x5b, 0x6d, 0xd2, 0x76,
    0x65, 0x72, 0x2c, 0x46, 0xb6, 0x0e, 0x5d, 0xf6, 0x80, 0xfb, 0x16, 0xb2,
    0x10, 0x60, 0x7e, 0xf2, 0x17, 0x65, 0x2e, 0x60, 0x23, 0x6c, 0x25, 0x5f,
    0x6a, 0x28, 0x31, 0x5f, 0x40, 0x83, 0xa9, 0x67, 0x91, 0xd7, 0x21, 0x4b,
    0xf6, 0x4c, 0x1d, 0xf4, 0xfd, 0x0d, 0xb1, 0x94, 0x4f, 0xb2, 0x6a, 0x2a,
    0x57, 0x03, 0x1b, 0x32, 0xee, 0xe6, 0x4a, 0xd1, 0x5a, 0x8b, 0xa6, 0x88,
    0x85, 0xcd, 0xe7, 0x4a, 0x5b, 0xfc, 0x92, 0x0f, 0x6a, 0xbf, 0x59, 0xba,
    0x5c, 0x75, 0x50, 0x63, 0x73, 0xe7, 0x13, 0x0f, 0x90, 0x42, 0xda, 0x92,
    0x21, 0x79, 0x25, 0x1f
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

// ── AlexaInc Application Identity ────────────────────────────────────────────
// NOTE (2026-05-22 fix): Previously, make_tagged_nonce() XOR'd the first 16
// bytes of every nonce with SHA256("AlexaInc\0QuotlyNative"). This was a
// no-op for protocol correctness but obscured real handshake bugs. We now
// emit plain RAND_bytes nonces (32 bytes for new_nonce). The identity tag
// is retained as a public constant for any external attestation use.

const Bytes& alexainc_identity_tag() {
    static const Bytes tag = []() -> Bytes {
        const std::string seed = std::string("AlexaInc") + '\0' + "QuotlyNative";
        Bytes src(seed.begin(), seed.end());
        return sha256(src);
    }();
    return tag;
}

Bytes make_tagged_nonce() {
    return random_bytes(32);
}

} // namespace Crypto
} // namespace MTProto
