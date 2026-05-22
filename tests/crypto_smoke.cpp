// Smoke test for crypto.cpp fixes.
// Compile:
//   g++ -std=c++17 -I. tests/crypto_smoke.cpp src/mtproto/crypto.cpp \
//       -lssl -lcrypto -o /tmp/crypto_smoke && /tmp/crypto_smoke

#include "../src/mtproto/crypto.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

using namespace MTProto;
using namespace MTProto::Crypto;

static void test_aes_ige_roundtrip() {
    Bytes key(32), iv(32);
    for (int i = 0; i < 32; ++i) { key[i] = (uint8_t)i; iv[i] = (uint8_t)(0x80 + i); }
    Bytes plain(64);
    for (int i = 0; i < 64; ++i) plain[i] = (uint8_t)(i * 3 + 1);
    Bytes c = aes_ige_encrypt(plain, key, iv);
    Bytes p = aes_ige_decrypt(c, key, iv);
    assert(p == plain);
    std::puts("[ok] AES-IGE round-trip");
}

static void test_rsa_pad_shape() {
    Bytes data192(192);
    for (int i = 0; i < 192; ++i) data192[i] = (uint8_t)(i * 7);
    Bytes ct = rsa_encrypt(data192, tg_rsa_n(), tg_rsa_e());
    assert(ct.size() == 256);

    Bytes data42(42);
    for (int i = 0; i < 42; ++i) data42[i] = (uint8_t)i;
    Bytes ct2 = rsa_encrypt(data42, tg_rsa_n(), tg_rsa_e());
    Bytes ct3 = rsa_encrypt(data42, tg_rsa_n(), tg_rsa_e());
    assert(ct2.size() == 256 && ct3.size() == 256);
    assert(ct2 != ct3);  // probabilistic

    bool threw = false;
    try { Bytes too_big(193); rsa_encrypt(too_big, tg_rsa_n(), tg_rsa_e()); }
    catch (const std::exception&) { threw = true; }
    assert(threw);
    std::puts("[ok] RSA_PAD shape & probabilism");
}

static void test_fingerprint() {
    int64_t fp = tg_rsa_fingerprint();
    if ((uint64_t)fp != 0xC3B42B026CE86B21ULL) {
        std::fprintf(stderr, "[FAIL] fingerprint=0x%016llx, expected 0xC3B42B026CE86B21\n",
                     (unsigned long long)fp);
        std::exit(1);
    }
    std::puts("[ok] Telegram RSA fingerprint = 0xC3B42B026CE86B21");
}

static void test_factorize() {
    // Two primes that multiply to a 61-bit semiprime
    uint64_t p_true = 1200948317ULL;
    uint64_t q_true = 1502392783ULL;
    uint64_t pq = p_true * q_true;
    Bytes pq_bytes;
    for (int i = 7; i >= 0; --i) pq_bytes.push_back((uint8_t)(pq >> (i * 8)));

    auto [p, q] = factorize_pq(pq_bytes);
    if ((uint64_t)p * q != pq) {
        std::fprintf(stderr, "[FAIL] factorize: %u * %u != %llu\n",
                     p, q, (unsigned long long)pq);
        std::exit(1);
    }
    std::printf("[ok] factorize_pq: %u * %u = %llu\n", p, q, (unsigned long long)pq);
}

static void test_nonce() {
    Bytes a = make_tagged_nonce();
    Bytes b = make_tagged_nonce();
    assert(a.size() == 32 && b.size() == 32);
    assert(a != b);
    std::puts("[ok] make_tagged_nonce returns 32 fresh random bytes");
}

int main() {
    test_aes_ige_roundtrip();
    test_rsa_pad_shape();
    test_fingerprint();
    test_factorize();
    test_nonce();
    std::puts("\nAll crypto smoke tests passed.");
    return 0;
}
