#pragma once
// this file is part of AlexaInc / QuotlyNative — High-Level MTProto Client
// Connects to Telegram DCs, authenticates as bot, auto-reconnects on failure.
// developer hansaka@alexainc

#include "session.h"
#include "auth_key.h"
#include "transport.h"
#include "tl.h"
#include <string>
#include <memory>
#include <mutex>
#include <thread>
#include <map>
#include <functional>
#include <future>
#include <atomic>

namespace MTProto {

// ── RPC call result ───────────────────────────────────────────────────────────
struct RpcResult {
    bool    ok;
    Bytes   payload;   // TL-decoded inner result if ok=true
    int32_t error_code;
    std::string error_message;
};

// ── High-Level Client ─────────────────────────────────────────────────────────

class Client {
public:
    Client();
    ~Client();

    // Connect to preferred DC, authenticate bot, start background recv loop.
    // dc_id: 1-5 (default 2 — the primary US/EU mixed DC)
    // Returns false and logs on failure.
    bool connect(const std::string& bot_token, int dc_id = 2);
    bool connect_imported_auth(int dc_id, int64_t auth_id, const Bytes& auth_bytes);

    bool is_connected() const { return m_connected.load(); }
    int current_dc_id() const { return m_dc_id; }

    // Invoke an RPC call: send payload, wait for response (up to timeout_ms).
    RpcResult invoke(const Bytes& request, int timeout_ms = 15000);

    // Graceful disconnect
    void disconnect();

private:
    enum class AuthMode {
        BotToken,
        ImportedAuth,
    };

    void recv_loop();           // background thread: reads and dispatches messages
    bool do_connect();          // actual connect + auth
    void schedule_reconnect();  // called when connection drops

    void dispatch(const Bytes& payload); // route incoming TL messages

    // ── State ─────────────────────────────────────────────────────────────────
    AuthMode          m_auth_mode = AuthMode::BotToken;
    std::string       m_bot_token;
    int64_t           m_import_auth_id = 0;
    Bytes             m_import_auth_bytes;
    int               m_dc_id   = 2;

    std::unique_ptr<Transport> m_transport;
    std::unique_ptr<AuthKey>   m_auth_key;
    std::unique_ptr<Session>   m_session;

    std::thread       m_recv_thread;
    std::thread       m_reconnect_thread;

    std::atomic<bool> m_connected  { false };
    std::atomic<bool> m_stopping   { false };
    std::atomic<bool> m_reconnect_scheduled { false };
    int               m_backoff_s  = 1;      // exponential backoff

    std::mutex                               m_send_mutex;

    // ── Pending RPC calls: msg_id → promise ───────────────────────────────────
    std::mutex                               m_pending_mutex;
    std::map<int64_t, std::promise<Bytes>>   m_pending;
};

} // namespace MTProto
