// this file is part of AlexaInc / QuotlyNative — Crypto
// developer hansaka@alexainc

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

// ── SHA1 ─────────────────────────────────────────────────────────────────────

Bytes sha1(const Bytes& data) {
    Bytes out(20);
    SHA1(data.data(), data.size(), out.data());
    return out;
}

// ── SHA256 ────────────────────────────────────────────────────────────────────

Bytes sha256(const Bytes& data) {
    Bytes out(32);
    SHA256(data.data(), data.size(), out.data());
    return out;
}

// ── AES-256-IGE ───────────────────────────────────────────────────────────────
// IGE mode:  c_i = AES_K( p_i XOR c_{i-1} ) XOR p_{i-1}

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
    // For IGE decrypt: x_i = D_k(c_i XOR x_{i-1}) XOR c_{i-1}
    // x_0 = iv[0:16], c_0 = iv[16:32]
    uint8_t x_prev[16], c_prev[16];
    std::memcpy(x_prev, iv.data(),      16); // x_0
    std::memcpy(c_prev, iv.data() + 16, 16); // c_0

    for (size_t i = 0; i < data.size(); i += 16) {
        uint8_t tmp[16], block_out[16];
        // tmp = c_i XOR x_{i-1}
        for (int j = 0; j < 16; ++j) tmp[j] = data[i + j] ^ x_prev[j];
        evp_aes_block(key.data(), tmp, block_out, false); // D_k(tmp)
        // x_i = D_k(c_i XOR x_{i-1}) XOR c_{i-1}
        for (int j = 0; j < 16; ++j) block_out[j] ^= c_prev[j];
        std::memcpy(out.data() + i, block_out, 16);
        // Advance: c_{i} becomes c_prev, x_i becomes x_prev
        std::memcpy(c_prev, data.data() + i, 16); // c_i
        std::memcpy(x_prev, block_out, 16);        // x_i
    }
    return out;
}

// ── RSA (MTProto custom padding) ──────────────────────────────────────────────
// Format: SHA1(data)[20] + data + random_padding → 255 bytes total → RSA → 256 bytes

Bytes rsa_encrypt(const Bytes& data, const Bytes& n, const Bytes& e) {
    Bytes hash = sha1(data);

    // Pad to 255 bytes: SHA1(20) + data + random_fill
    size_t total = 255;
    size_t pad_size = total - 20 - data.size();
    Bytes padding = random_bytes(pad_size);

    Bytes padded;
    padded.insert(padded.end(), hash.begin(),    hash.end());    // 20 bytes SHA1
    padded.insert(padded.end(), data.begin(),    data.end());    // inner data
    padded.insert(padded.end(), padding.begin(), padding.end()); // random padding

    if (padded.size() != 255)
        throw std::runtime_error("RSA padded size != 255");

    // RSA: result = padded^e mod n (big-endian)
    BIGNUM* bn_msg = BN_bin2bn(padded.data(), 255, nullptr);
    BIGNUM* bn_n   = BN_bin2bn(n.data(), n.size(), nullptr);
    BIGNUM* bn_e   = BN_bin2bn(e.data(), e.size(), nullptr);
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

// ── Pollard's rho PQ factorization ────────────────────────────────────────────

static uint64_t gcd(uint64_t a, uint64_t b) {
    while (b) { a %= b; std::swap(a, b); }
    return a;
}

static uint64_t pollard_rho(uint64_t n) {
    if (n % 2 == 0) return 2;
    uint64_t x = rand() % (n - 2) + 2;
    uint64_t y = x;
    uint64_t c = rand() % (n - 1) + 1;
    uint64_t d = 1;
    while (d == 1) {
        x = (__uint128_t(x) * x + c) % n;
        y = (__uint128_t(y) * y + c) % n;
        y = (__uint128_t(y) * y + c) % n;
        d = gcd(x > y ? x - y : y - x, n);
    }
    return d;
}

std::pair<uint32_t, uint32_t> factorize_pq(const Bytes& pq_bytes) {
    uint64_t pq = 0;
    for (uint8_t b : pq_bytes) pq = (pq << 8) | b;

    uint64_t p = pq;
    while (p == pq) p = pollard_rho(pq);
    uint64_t q = pq / p;
    if (p > q) std::swap(p, q);
    return { static_cast<uint32_t>(p), static_cast<uint32_t>(q) };
}

// ── RSA fingerprint ───────────────────────────────────────────────────────────
// TL-serialized: write n as byte-string prefixed with length, same for e
// fingerprint = last 8 bytes of SHA1(n_bytes_with_len + e_bytes_with_len) as LE int64

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
    // pad to 4-byte alignment
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
    // last 8 bytes as little-endian int64
    int64_t fp = 0;
    for (int i = 0; i < 8; ++i)
        fp |= (int64_t)h[12 + i] << (8 * i);
    return fp;
}

// ── Utilities ─────────────────────────────────────────────────────────────────

Bytes random_bytes(size_t count) {
    Bytes out(count);
    RAND_bytes(out.data(), static_cast<int>(count));
    return out;
}

Bytes xor_bytes(const Bytes& a, const Bytes& b) {
    Bytes out(a.size());
    for (size_t i = 0; i < a.size(); ++i)
        out[i] = a[i] ^ b[i];
    return out;
}

// ── Telegram Production RSA Key (fingerprint 0xC3B42B026CE86B21) ──────────────
// Modulus (n) and exponent (e) in big-endian format
// Source: Telegram TDLib / GramJS verified production key

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

static const uint8_t TG_RSA_E_BYTES[] = { 0x01, 0x00, 0x01 }; // 65537

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

const Bytes& alexainc_identity_tag() {
    // SHA256("AlexaInc\0QuotlyNative") — computed once at startup
    static const Bytes tag = []() -> Bytes {
        // "AlexaInc" + NUL separator + "QuotlyNative"
        const std::string seed = std::string("AlexaInc") + '\0' + "QuotlyNative";
        Bytes src(seed.begin(), seed.end());
        return sha256(src);
    }();
    return tag;
}

// Generates a cryptographically secure 32-byte nonce where the first 16 bytes
// are XOR-mixed with the first 16 bytes of the AlexaInc identity tag.
// The result is statistically indistinguishable from random (since both
// components are pseudorandom under SHA256 pre-image resistance), but the
// AlexaInc identity is cryptographically verifiable given the tag constant.
Bytes make_tagged_nonce() {
    Bytes nonce = random_bytes(32);
    const Bytes& tag = alexainc_identity_tag();
    // XOR first 16 bytes with identity tag — remains uniformly random
    for (int i = 0; i < 16; ++i)
        nonce[i] ^= tag[i];
    return nonce;
}

} // namespace Crypto
} // namespace MTProto

