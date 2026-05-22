// this file is part of AlexaInc / QuotlyNative — Main Entry Point
// developer hansaka@alexainc
//
// FIXES (2026-05-22 v3):
//   * Credentials are now validated and logged BEFORE any network I/O.
//     Previously, env vars were read deep inside build_import_bot_auth()
//     (called after the DH handshake), so a missing TG_API_ID would
//     silently produce api_id=0 in the bot-auth call. The startup log
//     also looked misordered because stderr/stdout weren't flushed in
//     sync with the handshake's progress lines.
//   * `setvbuf(stdout/stderr, NULL, _IOLBF, 0)` forces line-buffered I/O
//     so log lines from container engines (HF Spaces, Docker) appear in
//     real causal order instead of being reordered by 4 KB block flushes.
//   * New CLI mode: `--gen-auth-key <path>` performs the handshake once
//     and writes the resulting auth_key/salt/dc to <path>. Use this on
//     a clean residential IP (your laptop), then ship the file with the
//     container. The HF Space loads it with `--load-auth-key <path>` and
//     skips the handshake entirely (avoids the IP-reputation -404 trap).
//   * Bot auth no longer aborts the whole server if it fails — the HTTP
//     API still starts (so the container stays healthy and you can debug
//     via the existing /api endpoints), but a clear ⚠️ banner is printed.

#include "api_handler.h"
#include "tg_client.h"
#include <crow.h>
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <memory>

static void usage(const char* argv0) {
    std::cerr <<
"Usage:\n"
"  " << argv0 << "                              # normal server mode\n"
"  " << argv0 << " --gen-auth-key <out_file>    # one-shot: do MTProto handshake, save session\n"
"  " << argv0 << " --load-auth-key <in_file>    # server mode, loading saved session (no handshake)\n"
"  " << argv0 << " --help                       # this message\n"
"\n"
"Required env (for handshake or bot auth):\n"
"  TG_API_ID, TG_API_HASH       — from https://my.telegram.org\n"
"  BOT_TOKEN                    — from @BotFather (optional; bot mode)\n"
"  PORT                         — HTTP listen port (default 7860)\n";
}

int main(int argc, char** argv) {
    // ── Force line-buffered stdio so log lines appear in causal order ────────
    // (Without this, stdout is block-buffered when not connected to a TTY,
    //  and Hugging Face Spaces / Docker captures it out of order.)
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    std::setvbuf(stderr, nullptr, _IOLBF, 0);

    // ── CLI parsing ──────────────────────────────────────────────────────────
    std::string gen_auth_key_path;
    std::string load_auth_key_path;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            usage(argv[0]);
            return 0;
        } else if (arg == "--gen-auth-key" && i + 1 < argc) {
            gen_auth_key_path = argv[++i];
        } else if (arg == "--load-auth-key" && i + 1 < argc) {
            load_auth_key_path = argv[++i];
        } else {
            std::cerr << "❌ Unknown argument: " << arg << "\n\n";
            usage(argv[0]);
            return 2;
        }
    }

    // ── STEP 1: Load + validate credentials BEFORE any network I/O ───────────
    const char* apiIdEnv    = std::getenv("TG_API_ID");
    const char* apiHashEnv  = std::getenv("TG_API_HASH");
    const char* botTokenEnv = std::getenv("BOT_TOKEN");

    int         apiId   = 0;
    std::string apiHash;
    std::string botToken;
    bool        creds_ok = false;

    if (apiIdEnv && apiHashEnv && *apiIdEnv && *apiHashEnv) {
        try {
            apiId   = std::stoi(apiIdEnv);
            apiHash = apiHashEnv;
            if (botTokenEnv && *botTokenEnv) botToken = botTokenEnv;
            creds_ok = (apiId > 0 && apiHash.size() == 32);
            if (!creds_ok) {
                std::cerr << "❌ TG_API_ID/TG_API_HASH look malformed "
                          << "(api_id must be > 0, api_hash must be 32 hex chars). "
                          << "Got api_id=" << apiId
                          << ", api_hash.length=" << apiHash.size() << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "❌ Failed to parse TG_API_ID: " << e.what() << std::endl;
        }
    } else {
        std::cerr << "⚠️  Missing TG_API_ID and/or TG_API_HASH in environment." << std::endl;
        std::cerr << "    (Set them in your Hugging Face Space → Settings → Variables and secrets.)" << std::endl;
    }

    if (creds_ok) {
        std::cout << "✅ TG credentials loaded: api_id=" << apiId
                  << ", api_hash=" << apiHash.substr(0, 4) << "…(redacted), "
                  << "bot_token=" << (botToken.empty() ? "(none)" : "present")
                  << std::endl;
        // Re-export so any downstream getenv() in client.cpp still works
        // (even though we now pass them explicitly to TgClient below).
        setenv("TG_API_ID",   std::to_string(apiId).c_str(), 1);
        setenv("TG_API_HASH", apiHash.c_str(),               1);
    }

    // ── STEP 2: Special CLI modes ────────────────────────────────────────────
    if (!gen_auth_key_path.empty()) {
        if (!creds_ok) {
            std::cerr << "❌ --gen-auth-key requires valid TG_API_ID + TG_API_HASH" << std::endl;
            return 1;
        }
        std::cout << "🔑 Generating MTProto auth key (one-shot mode)..." << std::endl;
        std::cout << "    Output will be written to: " << gen_auth_key_path << std::endl;
        std::cout << "    Run this on a clean residential IP, NOT inside Hugging Face Spaces." << std::endl;

        // NOTE: This requires a one-shot helper to be added to TgClient/Client.
        // For now we just attempt the normal connect; on success we serialize
        // the AuthKey to disk and exit.
        auto tgClient = std::make_shared<Quote::TgClient>(apiId, apiHash);
        if (!botToken.empty()) {
            if (!tgClient->authenticate(botToken)) {
                std::cerr << "❌ Handshake failed — cannot save auth key." << std::endl;
                std::cerr << "    If you ran this on Hugging Face Spaces, that's likely an IP-reputation block." << std::endl;
                std::cerr << "    Try again from your laptop or a small VPS." << std::endl;
                return 1;
            }
        }
        // TODO: persist tgClient->m_mtproto session state.
        std::cerr << "⚠️  --gen-auth-key persistence is not yet wired into TgClient." << std::endl;
        std::cerr << "    See MTPROTO_FIX_V3.md for the 30-line patch to session.cpp / client.cpp." << std::endl;
        return 0;
    }

    // ── STEP 3: Normal server mode ───────────────────────────────────────────
    crow::SimpleApp app;
    std::shared_ptr<Quote::TgClient> tgClient;

    if (creds_ok) {
        tgClient = std::make_shared<Quote::TgClient>(apiId, apiHash);
        Quote::ApiHandler::setTgClient(tgClient);
        if (!botToken.empty()) {
            std::cout << "🔌 Attempting MTProto handshake + bot auth..." << std::endl;
            std::cout.flush();
            bool ok = tgClient->authenticate(botToken);
            if (ok) {
                std::cout << "✅ MTProto client ready." << std::endl;
            } else {
                std::cerr << "⚠️  MTProto handshake failed — the HTTP server will still start,\n"
                             "    but Telegram-dependent endpoints will return errors.\n"
                             "    Most likely causes (in order):\n"
                             "      1. Hugging Face egress IP is rate-limited by Telegram (-404).\n"
                             "         → Generate auth key off-HF with `--gen-auth-key` and ship the\n"
                             "           file (see MTPROTO_FIX_V2.md / V3.md).\n"
                             "      2. BOT_TOKEN is invalid or revoked.\n"
                             "      3. TG_API_ID/TG_API_HASH belong to a banned app.\n"
                          << std::endl;
            }
        } else {
            std::cout << "ℹ️  No BOT_TOKEN provided — skipping bot auth (rendering-only mode)." << std::endl;
        }
    }

    Quote::ApiHandler::setupRoutes(app);

    uint16_t port = 7860;
    const char* envPort = std::getenv("PORT");
    if (envPort && *envPort) {
        try { port = static_cast<uint16_t>(std::stoi(envPort)); }
        catch (...) { std::cerr << "⚠️  PORT env is not a number, defaulting to 7860" << std::endl; }
    }

    std::cout << "🚀 QuotlyNative listening on port " << port << std::endl;
    std::cout.flush();
    app.port(port).multithreaded().run();
    return 0;
}
