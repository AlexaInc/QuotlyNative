#include "client.h"
#include "auth_key.h"
#include "tl.h"
#include "crypto.h"
#include <iostream>
#include <chrono>
#include <stdexcept>
#include <cstring>

namespace MTProto {

// ── TL: auth.importBotAuthorization ──────────────────────────────────────────
// auth.importBotAuthorization#8a1a22b0 flags:# api_id:int api_hash:string bot_auth_token:string = auth.Authorization

static Bytes build_import_bot_auth(const std::string& bot_token) {
    // Read API_ID and API_HASH from environment
    const char* api_id_env   = std::getenv("TG_API_ID");
    const char* api_hash_env = std::getenv("TG_API_HASH");
    int32_t api_id   = api_id_env   ? std::stoi(api_id_env) : 0;
    std::string api_hash = api_hash_env ? api_hash_env : "";

    TLWriter w;
    w.writeInt32((int32_t)0x67a3ff2cu); // auth.importBotAuthorization
    w.writeInt32(0);                     // flags = 0
    w.writeInt32(api_id);
    w.writeString(api_hash);
    w.writeString(bot_token);
    return w.data();
}


// ── TL: invokeWithLayer + initConnection ─────────────────────────────────────
static Bytes wrap_init_connection(const Bytes& query) {
    const char* api_id_env = std::getenv("TG_API_ID");
    int32_t api_id = api_id_env ? std::stoi(api_id_env) : 0;

    TLWriter init;
    init.writeInt32((int32_t)0xc1cd5ea9u); // initConnection
    init.writeInt32(0);                    // flags
    init.writeInt32(api_id);
    init.writeString("QuotlyNative");      // device_model
    init.writeString("Linux");             // system_version
    init.writeString("1.0");               // app_version
    init.writeString("en");                // system_lang_code
    init.writeString("");                  // lang_pack
    init.writeString("en");                // lang_code
    Bytes initData = init.data();
    initData.insert(initData.end(), query.begin(), query.end());

    TLWriter layer;
    layer.writeInt32((int32_t)0xda9b0d0du); // invokeWithLayer
    layer.writeInt32(214);                  // current public API layer
    Bytes out = layer.data();
    out.insert(out.end(), initData.begin(), initData.end());
    return out;
}

// ── TL: ping_delay_disconnect ─────────────────────────────────────────────────
static Bytes build_ping_delay(int64_t ping_id, int32_t disconnect_delay = 75) {
    TLWriter w;
    w.writeInt32(TL::ping_delay_disconnect);
    w.writeInt64(ping_id);
    w.writeInt32(disconnect_delay); // server disconnects if no ping for this many seconds
    return w.data();
}

// ─────────────────────────────────────────────────────────────────────────────

Client::Client() = default;

Client::~Client() {
    disconnect();
}

bool Client::connect(const std::string& bot_token, int dc_id) {
    m_bot_token = bot_token;
    m_dc_id     = dc_id;
    m_stopping  = false;
    m_backoff_s = 1;

    return do_connect();
}

bool Client::do_connect() {
    int start_dc = m_dc_id;
    for (int attempts = 0; attempts < 5; ++attempts) {
        int attempt_dc = ((start_dc - 1 + attempts) % 5) + 1;
        try {
            std::cout << "  [MTProto] Connecting to DC" << attempt_dc << "..." << std::endl;

            // Create fresh transport + auth key
            m_transport = std::make_unique<Transport>();
            m_transport->connect(attempt_dc);

            m_auth_key = std::make_unique<AuthKey>(
                generate_auth_key(*m_transport, attempt_dc)
            );

            m_session = std::make_unique<Session>(*m_transport, *m_auth_key);

            // Authenticate as bot
            std::cout << "  [MTProto] Authenticating bot..." << std::endl;
            Bytes auth_req = build_import_bot_auth(m_bot_token);
            int64_t msg_id = m_session->send(wrap_init_connection(auth_req));

            // Wait specifically for the auth.importBotAuthorization rpc_result.
            // Telegram may send new_session_created or containers before the RPC result;
            // treating those as successful auth leaves the key unauthorised and later
            // API calls fail with CONNECTION_NOT_INITED/AUTH_KEY_UNREGISTERED.
            bool auth_ok = false;
            auto inspect_auth_payload = [&](const Bytes& payload, auto&& self) -> bool {
                if (payload.size() < 4) return false;
                TLReader r(payload);
                int32_t cid = r.readInt32();
                if (cid == TL::rpc_result) {
                    int64_t req = r.readInt64();
                    if (req != msg_id) return false;
                    int32_t inner_cid = r.readInt32();
                    if (inner_cid == TL::rpc_error) {
                        int32_t err_code = r.readInt32();
                        std::string err_msg = r.readString();
                        throw std::runtime_error("Bot auth error " + std::to_string(err_code) + ": " + err_msg);
                    }
                    return true;
                }
                if (cid == TL::new_session_created) {
                    int64_t first_msg = r.readInt64();
                    r.readInt64();
                    int64_t server_salt = r.readInt64();
                    if (m_session) m_session->set_server_salt(server_salt);
                    try { m_session->send(Session::make_ack(first_msg), false); } catch (...) {}
                    return false;
                }
                if (cid == TL::bad_server_salt) {
                    r.readInt64(); r.readInt32(); r.readInt32();
                    int64_t new_salt = r.readInt64();
                    if (m_session) m_session->set_server_salt(new_salt);
                    return false;
                }
                if (cid == TL::msg_container) {
                    int32_t n = r.readInt32();
                    for (int i = 0; i < n; ++i) {
                        r.readInt64(); r.readInt32();
                        int32_t bytes = r.readInt32();
                        Bytes inner = r.readRaw(bytes);
                        if (self(inner, self)) return true;
                    }
                }
                return false;
            };

            for (int i = 0; i < 10 && !auth_ok; ++i) {
                Bytes resp = m_session->recv();
                if (resp.empty()) throw std::runtime_error("Empty auth response");
                auth_ok = inspect_auth_payload(resp, inspect_auth_payload);
            }
            if (!auth_ok) throw std::runtime_error("Timed out waiting for bot auth result");

            m_connected  = true;
            m_backoff_s  = 1; // reset on success
            m_dc_id      = attempt_dc;
            std::cout << "  [MTProto] ✅ Connected to DC" << m_dc_id
                      << " as bot (AlexaInc/QuotlyNative)" << std::endl;

            // Start background receiver
            if (m_recv_thread.joinable()) m_recv_thread.join();
            m_recv_thread = std::thread([this] { recv_loop(); });

            return true;

        } catch (const std::exception& e) {
            std::cerr << "  [MTProto] ❌ Connection failed on DC" << attempt_dc << ": " << e.what() << std::endl;
            if (m_transport) { m_transport->close(); }
            m_transport = nullptr; m_auth_key = nullptr; m_session = nullptr;
        }
    }
    
    // If all DCs failed
    std::cerr << "  [MTProto] ❌ All DCs failed to connect." << std::endl;
    m_connected = false;
    return false;
}

// ── Background receiver loop ──────────────────────────────────────────────────
void Client::recv_loop() {
    int64_t ping_id = 1;
    auto    last_ping = std::chrono::steady_clock::now();

    while (!m_stopping && m_connected) {
        // Send periodic ping every 60 s
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - last_ping).count() >= 60) {
            try {
                m_session->send(build_ping_delay(ping_id++), false);
            } catch (...) {}
            last_ping = now;
        }

        Bytes payload;
        try {
            payload = m_session->recv();
        } catch (const std::exception& e) {
            if (!m_stopping) {
                std::cerr << "  [MTProto] Recv error: " << e.what() << std::endl;
                m_connected = false;
                schedule_reconnect();
            }
            return;
        }

        if (payload.empty()) {
            // Graceful disconnect
            if (!m_stopping) {
                m_connected = false;
                schedule_reconnect();
            }
            return;
        }

        dispatch(payload);
    }
}

// ── Dispatch incoming messages ────────────────────────────────────────────────
void Client::dispatch(const Bytes& payload) {
    if (payload.size() < 4) return;

    TLReader r(payload);
    int32_t cid = r.readInt32();

    if (cid == TL::rpc_result) {
        int64_t req_id = r.readInt64();
        Bytes inner(payload.begin() + 12, payload.end());
        std::lock_guard<std::mutex> lk(m_pending_mutex);
        auto it = m_pending.find(req_id);
        if (it != m_pending.end()) {
            it->second.set_value(inner);
            m_pending.erase(it);
        }
    } else if (cid == TL::bad_server_salt) {
        // Update server salt and retry is handled by invoker re-try
        r.readInt64(); // bad_msg_id
        r.readInt32(); // bad_msg_seqno
        r.readInt32(); // error_code
        int64_t new_salt = r.readInt64();
        if (m_session) m_session->set_server_salt(new_salt);
    } else if (cid == TL::new_session_created) {
        int64_t first_msg = r.readInt64();
        r.readInt64(); // unique_id
        int64_t server_salt = r.readInt64();
        if (m_session) m_session->set_server_salt(server_salt);
        // Ack the new_session_created
        Bytes ack = Session::make_ack(first_msg);
        try { m_session->send(ack, false); } catch (...) {}
    } else if (cid == TL::pong) {
        // keepalive pong — nothing to do
    } else if (cid == TL::msg_container) {
        // Message container: iterate inner messages
        int32_t count = r.readInt32();
        for (int i = 0; i < count; ++i) {
            r.readInt64(); // msg_id
            r.readInt32(); // seqno
            int32_t bytes = r.readInt32();
            Bytes inner = r.readRaw(bytes);
            dispatch(inner);
        }
    }
    // Other updates ignored for now (bot doesn't need them for emoji/avatar fetch)
}

// ── invoke ────────────────────────────────────────────────────────────────────
RpcResult Client::invoke(const Bytes& request, int timeout_ms) {
    if (!m_connected || !m_session)
        return { false, {}, -1, "Not connected" };

    std::promise<Bytes> promise;
    auto future = promise.get_future();
    int64_t msg_id;

    {
        std::lock_guard<std::mutex> lk(m_pending_mutex);
        Bytes wrapped = wrap_init_connection(request);
        msg_id = m_session->send(wrapped);
        m_pending[msg_id] = std::move(promise);
    }

    auto status = future.wait_for(std::chrono::milliseconds(timeout_ms));

    if (status != std::future_status::ready) {
        std::lock_guard<std::mutex> lk(m_pending_mutex);
        m_pending.erase(msg_id);
        return { false, {}, -408, "Request timeout" };
    }

    Bytes result = future.get();

    // Check for rpc_error in result
    if (result.size() >= 4) {
        TLReader r(result);
        int32_t cid = r.readInt32();
        if (cid == TL::rpc_error) {
            int32_t code = r.readInt32();
            std::string msg = r.readString();
            return { false, {}, code, msg };
        }
        // Re-include the constructor ID in the returned payload
        return { true, result, 0, "" };
    }

    return { true, result, 0, "" };
}

// ── Auto-reconnect ────────────────────────────────────────────────────────────
void Client::schedule_reconnect() {
    if (m_stopping) return;

    // Fail all pending promises
    {
        std::lock_guard<std::mutex> lk(m_pending_mutex);
        for (auto& [id, p] : m_pending) {
            try { p.set_exception(std::make_exception_ptr(
                std::runtime_error("Disconnected"))); } catch (...) {}
        }
        m_pending.clear();
    }

    int backoff = m_backoff_s;
    // Exponential backoff, cap at 60 s
    m_backoff_s = std::min(m_backoff_s * 2, 60);

    if (m_reconnect_thread.joinable()) m_reconnect_thread.detach();
    m_reconnect_thread = std::thread([this, backoff] {
        std::cout << "  [MTProto] Reconnecting in " << backoff << "s..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(backoff));
        if (!m_stopping) {
            if (!do_connect()) {
                schedule_reconnect();
            }
        }
    });
}

void Client::disconnect() {
    m_stopping  = true;
    m_connected = false;

    if (m_transport) m_transport->close();

    if (m_recv_thread.joinable())      m_recv_thread.join();
    if (m_reconnect_thread.joinable()) m_reconnect_thread.join();

    m_session   = nullptr;
    m_auth_key  = nullptr;
    m_transport = nullptr;

    std::cout << "  [MTProto] Disconnected." << std::endl;
}

} // namespace MTProto
