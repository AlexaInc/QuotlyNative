// this file is part of AlexaInc / QuotlyNative — Auth Key Generation
// developer hansaka@alexainc

#include "auth_key.h"
#include "tl.h"
#include "crypto.h"
#include <chrono>
#include <stdexcept>
#include <cstring>
#include <iostream>
#include <sstream>
#include <ctime>

namespace MTProto {

// ── Unencrypted message (used before auth key exists) ─────────────────────────
static Bytes build_unencrypted(int64_t msg_id, const Bytes& payload) {
    TLWriter w;
    w.writeInt64(0);           // auth_key_id = 0 (unencrypted)
    w.writeInt64(msg_id);      // message_id
    w.writeInt32(static_cast<int32_t>(payload.size()));
    for (auto b : payload) w.writeInt32(0); // placeholder — overwrite below
    // Rebuild properly
    Bytes out;
    auto append64 = [&](int64_t v) {
        for (int i = 0; i < 8; ++i) out.push_back((v >> (8*i)) & 0xff);
    };
    auto append32 = [&](int32_t v) {
        for (int i = 0; i < 4; ++i) out.push_back((v >> (8*i)) & 0xff);
    };
    append64(0);                            // auth_key_id
    append64(msg_id);                       // message_id
    append32(static_cast<int32_t>(payload.size()));
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

static int64_t make_msg_id() {
    using namespace std::chrono;
    auto now = system_clock::now().time_since_epoch();
    int64_t ns = duration_cast<nanoseconds>(now).count();
    // MTProto msg_id ≈ unix_seconds * 2^32 | nano_fraction
    int64_t sec = ns / 1'000'000'000LL;
    int64_t frac = (ns % 1'000'000'000LL) * 4294967296LL / 1'000'000'000LL;
    return (sec << 32) | (frac & 0xfffffffc); // must be divisible by 4
}

static Bytes parse_unencrypted_payload(const Bytes& frame) {
    // Skip auth_key_id(8) + msg_id(8) + msg_len(4) = 20 bytes header
    if (frame.size() < 20) throw std::runtime_error("Frame too short for unencrypted message");
    TLReader r(frame, 20);
    return r.readRaw(frame.size() - 20);
}

// ── Build p_q_inner_data_dc TL object ─────────────────────────────────────────
static Bytes build_pq_inner(
    const Bytes& pq, uint32_t p, uint32_t q,
    const Bytes& nonce, const Bytes& server_nonce,
    const Bytes& new_nonce, int dc_id)
{
    TLWriter w;
    w.writeInt32(TL::p_q_inner_data_dc);
    w.writeBytes(pq);
    w.writeBigEndian32(p);
    w.writeBigEndian32(q);
    w.writeInt128(nonce.data());
    w.writeInt128(server_nonce.data());
    w.writeInt256(new_nonce.data());
    w.writeInt32(dc_id);
    return w.data();
}

// ── Derive tmp_aes key/iv per MTProto spec ───────────────────────────────────
static void derive_tmp_aes(
    const Bytes& server_nonce, const Bytes& new_nonce,
    Bytes& aes_key, Bytes& aes_iv)
{
    using namespace Crypto;
    // MTProto 1.0 Key Derivation for initial Handshake
    // tmp_aes_key = SHA1(new_nonce + server_nonce) + substr(SHA1(server_nonce + new_nonce), 0, 12)
    // tmp_aes_iv = substr(SHA1(server_nonce + new_nonce), 12, 8) + SHA1(new_nonce + new_nonce) + substr(new_nonce, 0, 4)

    Bytes nn = new_nonce; if (nn.size() > 32) nn.resize(32);
    Bytes sn = server_nonce; if (sn.size() > 16) sn.resize(16);

    Bytes h1_input; h1_input.insert(h1_input.end(), nn.begin(), nn.end()); h1_input.insert(h1_input.end(), sn.begin(), sn.end());
    Bytes h1 = sha1(h1_input);

    Bytes h2_input; h2_input.insert(h2_input.end(), sn.begin(), sn.end()); h2_input.insert(h2_input.end(), nn.begin(), nn.end());
    Bytes h2 = sha1(h2_input);

    Bytes h3_input; h3_input.insert(h3_input.end(), nn.begin(), nn.end()); h3_input.insert(h3_input.end(), nn.begin(), nn.end());
    Bytes h3 = sha1(h3_input);

    aes_key.clear();
    aes_key.insert(aes_key.end(), h1.begin(), h1.end());
    aes_key.insert(aes_key.end(), h2.begin(), h2.begin() + 12);

    aes_iv.clear();
    aes_iv.insert(aes_iv.end(), h2.begin() + 12, h2.end());
    aes_iv.insert(aes_iv.end(), h3.begin(), h3.end());
    aes_iv.insert(aes_iv.end(), nn.begin(), nn.begin() + 4);
}

// ═════════════════════════════════════════════════════════════════════════════
// MAIN: generate_auth_key
// ═════════════════════════════════════════════════════════════════════════════

AuthKey generate_auth_key(Transport& transport, int dc_id) {
    using namespace Crypto;

    std::cout << "  [AuthKey] Step 1: req_pq_multi (AlexaInc tagged nonce)" << std::endl;

    // ── Step 1: req_pq_multi ─────────────────────────────────────────────────
    Bytes nonce = make_tagged_nonce(); // AlexaInc identity XOR-mixed in first 16 bytes

    TLWriter req_pq;
    req_pq.writeInt32(TL::req_pq_multi);
    req_pq.writeInt128(nonce.data());

    transport.send(build_unencrypted(make_msg_id(), req_pq.data()));

    // ── Parse resPQ ──────────────────────────────────────────────────────────
    Bytes frame = transport.recv();
    Bytes payload = parse_unencrypted_payload(frame);

    TLReader r_resPQ(payload);
    int32_t cid = r_resPQ.readInt32();
    if (cid != TL::resPQ)
        throw std::runtime_error("Expected resPQ, got: " + std::to_string(cid));

    r_resPQ.readInt128(); // skip the echoed client nonce
    Bytes server_nonce_r = r_resPQ.readInt128();  // server's nonce
    Bytes server_nonce(server_nonce_r.begin(), server_nonce_r.end());
    // (resPQ nonce echo fits nonce we sent)
    Bytes pq = r_resPQ.readBytes();

    // Read server fingerprints vector
    int32_t vec_cid = r_resPQ.readInt32();
    if (vec_cid != TL::vector) throw std::runtime_error("Expected vector in resPQ");
    int32_t vec_len = r_resPQ.readInt32();
    int64_t server_fp = 0;
    for (int i = 0; i < vec_len; ++i) {
        int64_t fp = r_resPQ.readInt64();
        if (fp == tg_rsa_fingerprint()) server_fp = fp;
    }
    if (server_fp == 0)
        throw std::runtime_error("Server did not offer a known RSA fingerprint");

    std::cout << "  [AuthKey] Got PQ, factorizing..." << std::endl;

    // ── Factorize pq ─────────────────────────────────────────────────────────
    auto [p, q] = factorize_pq(pq);
    std::cout << "  [AuthKey] p=" << p << " q=" << q << std::endl;

    // ── Step 2: req_DH_params ────────────────────────────────────────────────
    Bytes new_nonce = make_tagged_nonce(); // second AlexaInc-tagged nonce

    Bytes inner = build_pq_inner(pq, p, q, nonce, server_nonce, new_nonce, dc_id);
    Bytes encrypted_data = rsa_encrypt(inner, tg_rsa_n(), tg_rsa_e());

    TLWriter req_dh;
    req_dh.writeInt32(TL::req_DH_params);
    req_dh.writeInt128(nonce.data());
    req_dh.writeInt128(server_nonce.data());
    req_dh.writeBigEndian32(p);
    req_dh.writeBigEndian32(q);
    req_dh.writeInt64(server_fp);
    req_dh.writeBytes(encrypted_data);

    transport.send(build_unencrypted(make_msg_id(), req_dh.data()));

    std::cout << "  [AuthKey] Step 2: req_DH_params sent" << std::endl;

    // ── Parse server_DH_params_ok ────────────────────────────────────────────
    frame = transport.recv();
    payload = parse_unencrypted_payload(frame);

    TLReader r_dh(payload);
    cid = r_dh.readInt32();
    if (cid == TL::server_DH_params_fail)
        throw std::runtime_error("server_DH_params_fail — bad RSA inner data or fingerprint");
    if (cid != TL::server_DH_params_ok)
        throw std::runtime_error("Expected server_DH_params_ok, got: " + std::to_string(cid));

    r_dh.readInt128(); // nonce echo
    r_dh.readInt128(); // server_nonce echo
    Bytes encrypted_answer = r_dh.readBytes();

    // ── Decrypt server DH inner data ─────────────────────────────────────────
    Bytes aes_key, aes_iv;
    derive_tmp_aes(server_nonce, new_nonce, aes_key, aes_iv);
    Bytes decrypted = aes_ige_decrypt(encrypted_answer, aes_key, aes_iv);

    // DEBUG: Hex dump to find the mismatch cause
    {
        auto hex = [](const Bytes& b){
            std::ostringstream ss;
            for(size_t i=0; i<std::min(b.size(),(size_t)32); ++i) ss << std::hex << (int)b[i] << " ";
            return ss.str();
        };
        std::cerr << "  [AuthKey] SN: " << hex(server_nonce) << std::endl;
        std::cerr << "  [AuthKey] NN: " << hex(new_nonce) << std::endl;
        std::cerr << "  [AuthKey] Dechead: " << hex(decrypted) << std::endl;
    }

    if (decrypted.size() < 24)
        throw std::runtime_error("Decrypted server DH answer too short");
    Bytes inner_data_bytes(decrypted.begin() + 20, decrypted.end());
    Bytes expected_sha1 = Crypto::sha1(inner_data_bytes);
    bool sha1_ok = std::equal(expected_sha1.begin(), expected_sha1.end(), decrypted.begin());
    if (!sha1_ok)
        std::cerr << "  [AuthKey] ⚠️ SHA1 mismatch in server_DH_inner_data!" << std::endl;

    TLReader r_inner(decrypted, 20);
    cid = r_inner.readInt32();
    if (cid != TL::server_DH_inner_data) {
        std::ostringstream ss; ss << "Expected 0x" << std::hex << TL::server_DH_inner_data << ", got 0x" << (uint32_t)cid;
        throw std::runtime_error(ss.str());
    }

    r_inner.readInt128(); // nonce
    r_inner.readInt128(); // server_nonce
    int32_t g = r_inner.readInt32();
    Bytes dh_prime = r_inner.readBytes();  // 256 bytes big-endian
    Bytes g_a       = r_inner.readBytes(); // server's g^a mod p

    std::cout << "  [AuthKey] Step 3: DH params decrypted, g=" << g << std::endl;

    // ── Generate client b, g_b ────────────────────────────────────────────────
    Bytes b = random_bytes(256);       // 2048-bit random exponent

    // g as 256-byte big-endian
    Bytes g_bytes(256, 0);
    g_bytes[255] = g & 0xff;
    g_bytes[254] = (g >> 8) & 0xff;
    g_bytes[253] = (g >> 16) & 0xff;
    g_bytes[252] = (g >> 24) & 0xff;

    Bytes g_b = mod_exp(g_bytes, b, dh_prime);     // g^b mod dh_prime
    Bytes auth_key_raw = mod_exp(g_a, b, dh_prime); // g^(a*b) mod dh_prime

    // ── Build client_DH_inner_data ───────────────────────────────────────────
    TLWriter cli_inner;
    cli_inner.writeInt32(TL::client_DH_inner_data);
    cli_inner.writeInt128(nonce.data());
    cli_inner.writeInt128(server_nonce.data());
    cli_inner.writeInt64(0); // retry_id = 0 (first attempt)
    cli_inner.writeBytes(g_b);

    Bytes inner_data = cli_inner.data();
    Bytes hash_inner = sha1(inner_data);

    // Plaintext = SHA1(inner_data) + inner_data + padding to 16-byte boundary
    Bytes padded_inner;
    padded_inner.insert(padded_inner.end(), hash_inner.begin(), hash_inner.end());
    padded_inner.insert(padded_inner.end(), inner_data.begin(), inner_data.end());
    while (padded_inner.size() % 16 != 0) padded_inner.push_back(0);

    Bytes enc_client = aes_ige_encrypt(padded_inner, aes_key, aes_iv);

    // ── Step 3: set_client_DH_params ─────────────────────────────────────────
    TLWriter set_cli;
    set_cli.writeInt32(TL::set_client_DH_params);
    set_cli.writeInt128(nonce.data());
    set_cli.writeInt128(server_nonce.data());
    set_cli.writeBytes(enc_client);

    transport.send(build_unencrypted(make_msg_id(), set_cli.data()));

    // ── Parse dh_gen_ok ──────────────────────────────────────────────────────
    frame = transport.recv();
    payload = parse_unencrypted_payload(frame);

    TLReader r_gen(payload);
    cid = r_gen.readInt32();
    if (cid != TL::dh_gen_ok)
        throw std::runtime_error("DH handshake failed, response cid: " + std::to_string(cid));

    std::cout << "  [AuthKey] ✅ DH handshake complete — auth key generated" << std::endl;

    // ── Compute auth_key_id and server_salt ───────────────────────────────────
    // Pad auth_key to 256 bytes
    while (auth_key_raw.size() < 256) auth_key_raw.insert(auth_key_raw.begin(), 0);

    Bytes ak_sha = sha1(auth_key_raw);
    int64_t key_id = 0;
    for (int i = 0; i < 8; ++i)
        key_id |= static_cast<int64_t>(ak_sha[12 + i]) << (8 * i); // last 8 bytes of SHA1

    // server_salt = XOR of first 8 bytes of new_nonce and server_nonce
    int64_t server_salt = 0;
    for (int i = 0; i < 8; ++i) {
        uint8_t byte = new_nonce[i] ^ server_nonce[i];
        server_salt |= static_cast<int64_t>(byte) << (8 * i);
    }

    return AuthKey{ auth_key_raw, key_id, server_salt };
}

} // namespace MTProto
