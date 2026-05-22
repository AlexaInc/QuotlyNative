// this file is part of AlexaInc / QuotlyNative — Auth Key Generation
// developer hansaka@alexainc
//
// FIXES (2026-05-22):
//   * `nonce` is now a proper int128 (16 bytes); previously the code passed
//     a 32-byte buffer to writeInt128() which only reads the first 16 bytes.
//   * `new_nonce` is a proper int256 (32 bytes).
//   * Removed the AlexaInc XOR tagging from nonces (see crypto.cpp note).
//   * `build_unencrypted` no longer does the double-pass placeholder dance —
//     it just emits the header + payload directly.
//   * `server_DH_params_fail` is detected before we try to AES-decrypt.
//   * `dh_gen_retry` / `dh_gen_fail` are handled with up to 5 retries.
//   * `req_DH_params` now uses MTProto 2.0 RSA_PAD (via the new rsa_encrypt).
//   * client_DH_inner_data padding is now random bytes (not zeros).

#include "auth_key.h"
#include "tl.h"
#include "crypto.h"
#include <chrono>
#include <stdexcept>
#include <cstring>
#include <iostream>
#include <sstream>
#include <ctime>
#include <algorithm>

namespace MTProto {

// ── Unencrypted MTProto message frame ─────────────────────────────────────────
//   int64 auth_key_id = 0
//   int64 message_id
//   int32 message_data_length
//   bytes message_data
static Bytes build_unencrypted(int64_t msg_id, const Bytes& payload) {
    Bytes out;
    out.reserve(20 + payload.size());

    auto append64 = [&](int64_t v) {
        uint64_t u = static_cast<uint64_t>(v);
        for (int i = 0; i < 8; ++i) out.push_back((u >> (8*i)) & 0xff);
    };
    auto append32 = [&](int32_t v) {
        uint32_t u = static_cast<uint32_t>(v);
        for (int i = 0; i < 4; ++i) out.push_back((u >> (8*i)) & 0xff);
    };

    append64(0);                                        // auth_key_id
    append64(msg_id);                                   // message_id
    append32(static_cast<int32_t>(payload.size()));     // message_data_length
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

static int64_t make_msg_id() {
    using namespace std::chrono;
    auto now = system_clock::now().time_since_epoch();
    int64_t ns = duration_cast<nanoseconds>(now).count();
    int64_t sec  = ns / 1'000'000'000LL;
    int64_t frac = (ns % 1'000'000'000LL) * 4294967296LL / 1'000'000'000LL;
    return (sec << 32) | (frac & 0xfffffffc); // must be divisible by 4
}

// ── Build p_q_inner_data_dc TL object ─────────────────────────────────────────
static Bytes build_pq_inner(
    const Bytes& pq, uint32_t p, uint32_t q,
    const Bytes& nonce,          // 16 bytes
    const Bytes& server_nonce,   // 16 bytes
    const Bytes& new_nonce,      // 32 bytes
    int dc_id)
{
    auto u32_be = [](uint32_t v) -> Bytes {
        Bytes b(4);
        b[0] = (v >> 24) & 0xff;
        b[1] = (v >> 16) & 0xff;
        b[2] = (v >>  8) & 0xff;
        b[3] =  v        & 0xff;
        return b;
    };

    TLWriter w;
    w.writeInt32(TL::p_q_inner_data_dc);
    w.writeBytes(pq);
    w.writeBytes(u32_be(p));
    w.writeBytes(u32_be(q));
    w.writeInt128(nonce.data());
    w.writeInt128(server_nonce.data());
    w.writeInt256(new_nonce.data());
    w.writeInt32(dc_id);
    return w.data();
}

// ── Derive tmp_aes key/iv per MTProto spec ───────────────────────────────────
//   tmp_aes_key = SHA1(new_nonce + server_nonce)                            (20)
//               + substr(SHA1(server_nonce + new_nonce), 0, 12)             (12) → 32
//   tmp_aes_iv  = substr(SHA1(server_nonce + new_nonce), 12, 8)             ( 8)
//               + SHA1(new_nonce + new_nonce)                               (20)
//               + substr(new_nonce, 0, 4)                                   ( 4) → 32
static void derive_tmp_aes(
    const Bytes& server_nonce, const Bytes& new_nonce,
    Bytes& aes_key, Bytes& aes_iv)
{
    using namespace Crypto;
    if (server_nonce.size() != 16) throw std::runtime_error("derive_tmp_aes: server_nonce must be 16 bytes");
    if (new_nonce.size()    != 32) throw std::runtime_error("derive_tmp_aes: new_nonce must be 32 bytes");

    Bytes nn_sn; nn_sn.insert(nn_sn.end(), new_nonce.begin(),    new_nonce.end());
                 nn_sn.insert(nn_sn.end(), server_nonce.begin(), server_nonce.end());
    Bytes sn_nn; sn_nn.insert(sn_nn.end(), server_nonce.begin(), server_nonce.end());
                 sn_nn.insert(sn_nn.end(), new_nonce.begin(),    new_nonce.end());
    Bytes nn_nn; nn_nn.insert(nn_nn.end(), new_nonce.begin(),    new_nonce.end());
                 nn_nn.insert(nn_nn.end(), new_nonce.begin(),    new_nonce.end());

    Bytes h1 = sha1(nn_sn);
    Bytes h2 = sha1(sn_nn);
    Bytes h3 = sha1(nn_nn);

    aes_key.clear();
    aes_key.insert(aes_key.end(), h1.begin(),     h1.end());
    aes_key.insert(aes_key.end(), h2.begin(),     h2.begin() + 12);

    aes_iv.clear();
    aes_iv.insert(aes_iv.end(), h2.begin() + 12,   h2.begin() + 20);
    aes_iv.insert(aes_iv.end(), h3.begin(),        h3.end());
    aes_iv.insert(aes_iv.end(), new_nonce.begin(), new_nonce.begin() + 4);
}

// ── Parse unencrypted payload (skip 20-byte header) ───────────────────────────
static Bytes parse_unencrypted_payload(const Bytes& frame) {
    if (frame.size() < 20) throw std::runtime_error("Frame too short for unencrypted message");
    return Bytes(frame.begin() + 20, frame.end());
}

// ═════════════════════════════════════════════════════════════════════════════
// MAIN: generate_auth_key
// ═════════════════════════════════════════════════════════════════════════════

AuthKey generate_auth_key(Transport& transport, int dc_id) {
    using namespace Crypto;

    std::cout << "  [AuthKey] Step 1: req_pq_multi" << std::endl;

    // ── Step 1: req_pq_multi (nonce is int128 = 16 bytes) ────────────────────
    Bytes nonce = random_bytes(16);

    TLWriter req_pq;
    req_pq.writeInt32(TL::req_pq_multi);
    req_pq.writeInt128(nonce.data());

    transport.send(build_unencrypted(make_msg_id(), req_pq.data()));

    // ── Parse resPQ ──────────────────────────────────────────────────────────
    Bytes frame   = transport.recv();
    Bytes payload = parse_unencrypted_payload(frame);

    TLReader r_resPQ(payload);
    int32_t cid = r_resPQ.readInt32();
    if (cid != TL::resPQ) {
        std::ostringstream ss; ss << "Expected resPQ, got: 0x" << std::hex << (uint32_t)cid;
        throw std::runtime_error(ss.str());
    }

    Bytes echo_nonce = r_resPQ.readInt128();
    if (!std::equal(echo_nonce.begin(), echo_nonce.end(), nonce.begin()))
        throw std::runtime_error("resPQ: server echoed wrong nonce");

    Bytes server_nonce = r_resPQ.readInt128();    // 16 bytes
    Bytes pq           = r_resPQ.readBytes();

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

    auto [p, q] = factorize_pq(pq);
    std::cout << "  [AuthKey] p=" << p << " q=" << q << std::endl;

    // ── Step 2: req_DH_params (new_nonce is int256 = 32 bytes) ───────────────
    Bytes new_nonce = random_bytes(32);

    Bytes inner          = build_pq_inner(pq, p, q, nonce, server_nonce, new_nonce, dc_id);
    Bytes encrypted_data = rsa_encrypt(inner, tg_rsa_n(), tg_rsa_e());  // MTProto 2.0 RSA_PAD

    auto u32_be = [](uint32_t v) -> Bytes {
        Bytes b(4);
        b[0] = (v >> 24) & 0xff; b[1] = (v >> 16) & 0xff;
        b[2] = (v >>  8) & 0xff; b[3] =  v        & 0xff;
        return b;
    };

    TLWriter req_dh;
    req_dh.writeInt32(TL::req_DH_params);
    req_dh.writeInt128(nonce.data());
    req_dh.writeInt128(server_nonce.data());
    req_dh.writeBytes(u32_be(p));
    req_dh.writeBytes(u32_be(q));
    req_dh.writeInt64(server_fp);
    req_dh.writeBytes(encrypted_data);

    transport.send(build_unencrypted(make_msg_id(), req_dh.data()));
    std::cout << "  [AuthKey] Step 2: req_DH_params sent" << std::endl;

    // ── Parse server_DH_params ───────────────────────────────────────────────
    frame   = transport.recv();
    payload = parse_unencrypted_payload(frame);

    TLReader r_dh(payload);
    cid = r_dh.readInt32();
    if (cid == TL::server_DH_params_fail)
        throw std::runtime_error("server_DH_params_fail — RSA inner data rejected by server");
    if (cid != TL::server_DH_params_ok) {
        std::ostringstream ss;
        ss << "Expected server_DH_params_ok, got: 0x" << std::hex << (uint32_t)cid;
        throw std::runtime_error(ss.str());
    }

    r_dh.readInt128(); // nonce echo
    r_dh.readInt128(); // server_nonce echo
    Bytes encrypted_answer = r_dh.readBytes();

    // ── Decrypt server DH inner data ─────────────────────────────────────────
    Bytes aes_key, aes_iv;
    derive_tmp_aes(server_nonce, new_nonce, aes_key, aes_iv);
    Bytes decrypted = aes_ige_decrypt(encrypted_answer, aes_key, aes_iv);

    if (decrypted.size() < 24)
        throw std::runtime_error("Decrypted server DH answer too short");

    const uint8_t* d = decrypted.data();
    uint32_t got_cid = (uint32_t)d[20] | ((uint32_t)d[21] << 8)
                     | ((uint32_t)d[22] << 16) | ((uint32_t)d[23] << 24);
    if (got_cid != (uint32_t)TL::server_DH_inner_data) {
        std::ostringstream ss;
        ss << "server_DH_inner_data CID mismatch: expected 0x"
           << std::hex << (uint32_t)TL::server_DH_inner_data
           << ", got 0x" << got_cid;
        throw std::runtime_error(ss.str());
    }

    Bytes inner_part(decrypted.begin() + 20, decrypted.end());
    Bytes expected_sha1 = sha1(inner_part);
    if (!std::equal(expected_sha1.begin(), expected_sha1.end(), decrypted.begin()))
        throw std::runtime_error("server_DH_inner_data SHA1 mismatch");

    TLReader r_inner(decrypted, 20);
    r_inner.readInt32();          // cid (already checked)
    r_inner.readInt128();         // nonce
    r_inner.readInt128();         // server_nonce
    int32_t g       = r_inner.readInt32();
    Bytes   dh_prime = r_inner.readBytes();
    Bytes   g_a      = r_inner.readBytes();
    // r_inner.readInt32();       // server_time (TODO: feed to transport for sync)

    std::cout << "  [AuthKey] Step 3: DH params decrypted, g=" << g << std::endl;

    // ── DH retry loop (up to 5 attempts, per Telegram spec) ──────────────────
    for (int retry = 0; retry < 5; ++retry) {
        Bytes b_exp = random_bytes(256);

        Bytes g_bytes(256, 0);
        g_bytes[252] = (g >> 24) & 0xff;
        g_bytes[253] = (g >> 16) & 0xff;
        g_bytes[254] = (g >>  8) & 0xff;
        g_bytes[255] =  g        & 0xff;

        Bytes g_b          = mod_exp(g_bytes, b_exp, dh_prime);
        Bytes auth_key_raw = mod_exp(g_a,     b_exp, dh_prime);
        while (auth_key_raw.size() < 256) auth_key_raw.insert(auth_key_raw.begin(), 0);

        TLWriter cli_inner;
        cli_inner.writeInt32(TL::client_DH_inner_data);
        cli_inner.writeInt128(nonce.data());
        cli_inner.writeInt128(server_nonce.data());
        cli_inner.writeInt64(retry);  // retry_id (0 first time)
        cli_inner.writeBytes(g_b);

        Bytes inner_data = cli_inner.data();
        Bytes hash_inner = sha1(inner_data);

        Bytes padded_inner;
        padded_inner.insert(padded_inner.end(), hash_inner.begin(), hash_inner.end());
        padded_inner.insert(padded_inner.end(), inner_data.begin(), inner_data.end());
        size_t pad_needed = (16 - (padded_inner.size() % 16)) % 16;
        if (pad_needed) {
            Bytes pad = random_bytes(pad_needed);
            padded_inner.insert(padded_inner.end(), pad.begin(), pad.end());
        }

        Bytes enc_client = aes_ige_encrypt(padded_inner, aes_key, aes_iv);

        TLWriter set_cli;
        set_cli.writeInt32(TL::set_client_DH_params);
        set_cli.writeInt128(nonce.data());
        set_cli.writeInt128(server_nonce.data());
        set_cli.writeBytes(enc_client);

        transport.send(build_unencrypted(make_msg_id(), set_cli.data()));

        frame   = transport.recv();
        payload = parse_unencrypted_payload(frame);

        TLReader r_gen(payload);
        cid = r_gen.readInt32();

        if (cid == TL::dh_gen_ok) {
            std::cout << "  [AuthKey] ✅ DH handshake complete" << std::endl;

            Bytes ak_sha = sha1(auth_key_raw);
            int64_t key_id = 0;
            for (int i = 0; i < 8; ++i)
                key_id |= static_cast<int64_t>(ak_sha[12 + i]) << (8 * i);

            int64_t server_salt = 0;
            for (int i = 0; i < 8; ++i) {
                uint8_t byte = new_nonce[i] ^ server_nonce[i];
                server_salt |= static_cast<int64_t>(byte) << (8 * i);
            }
            return AuthKey{ auth_key_raw, key_id, server_salt };
        }

        if (cid == TL::dh_gen_retry) {
            std::cout << "  [AuthKey] dh_gen_retry — retrying (" << (retry + 1) << "/5)" << std::endl;
            continue;
        }
        if (cid == TL::dh_gen_fail)
            throw std::runtime_error("dh_gen_fail — server rejected DH parameters");

        std::ostringstream ss;
        ss << "Unexpected DH gen response cid: 0x" << std::hex << (uint32_t)cid;
        throw std::runtime_error(ss.str());
    }

    throw std::runtime_error("DH handshake failed after 5 retries");
}

} // namespace MTProto
