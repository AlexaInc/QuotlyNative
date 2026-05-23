#include "client.h"
#include "auth_key.h"
#include "tl.h"
#include "crypto.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <zlib.h>

namespace MTProto {

// ── TL: auth.importBotAuthorization ──────────────────────────────────────────
// auth.importBotAuthorization#8a1a22b0 flags:# api_id:int api_hash:string bot_auth_token:string = auth.Authorization

static Bytes build_import_bot_auth(const std::string& bot_token) {
    const char* api_id_env   = std::getenv("TG_API_ID");
    const char* api_hash_env = std::getenv("TG_API_HASH");
    int32_t api_id = api_id_env ? std::stoi(api_id_env) : 0;
    std::string api_hash = api_hash_env ? api_hash_env : "";

    TLWriter w;
    w.writeInt32((int32_t)0x67a3ff2cu); // auth.importBotAuthorization
    w.writeInt32(0);                    // flags = 0
    w.writeInt32(api_id);
    w.writeString(api_hash);
    w.writeString(bot_token);
    return w.data();
}

static Bytes build_import_authorization(int64_t auth_id, const Bytes& auth_bytes) {
    TLWriter w;
    w.writeInt32(TL::auth_importAuthorization);
    w.writeInt64(auth_id);
    w.writeBytes(auth_bytes);
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
    init.writeString("QuotlyNative");
    init.writeString("Linux");
    init.writeString("1.0");
    init.writeString("en");
    init.writeString("");
    init.writeString("en");
    Bytes initData = init.data();
    initData.insert(initData.end(), query.begin(), query.end());

    TLWriter layer;
    layer.writeInt32((int32_t)0xda9b0d0du); // invokeWithLayer
    layer.writeInt32(214);
    Bytes out = layer.data();
    out.insert(out.end(), initData.begin(), initData.end());
    return out;
}

// ── TL: ping_delay_disconnect ─────────────────────────────────────────────────
static Bytes build_ping_delay(int64_t ping_id, int32_t disconnect_delay = 75) {
    TLWriter w;
    w.writeInt32(TL::ping_delay_disconnect);
    w.writeInt64(ping_id);
    w.writeInt32(disconnect_delay);
    return w.data();
}

static int parse_migrate_dc(const std::string& msg) {
    const std::string needle = "_MIGRATE_";
    size_t pos = msg.find(needle);
    if (pos == std::string::npos) return 0;
    pos += needle.size();

    int dc = 0;
    bool any = false;
    while (pos < msg.size() && std::isdigit(static_cast<unsigned char>(msg[pos]))) {
        any = true;
        dc = dc * 10 + (msg[pos] - '0');
        ++pos;
    }
    return any && dc >= 1 && dc <= 5 ? dc : 0;
}

static Bytes gunzip_bytes(const Bytes& packed) {
    if (packed.empty()) return {};

    z_stream zs{};
    zs.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(packed.data()));
    zs.avail_in = static_cast<uInt>(packed.size());

    if (inflateInit2(&zs, 15 + 32) != Z_OK) {
        throw std::runtime_error("inflateInit2 failed for gzip_packed payload");
    }

    Bytes out;
    std::array<uint8_t, 8192> chunk{};
    int rc = Z_OK;
    while (rc == Z_OK) {
        zs.next_out = reinterpret_cast<Bytef*>(chunk.data());
        zs.avail_out = static_cast<uInt>(chunk.size());
        rc = inflate(&zs, Z_NO_FLUSH);
        if (rc != Z_OK && rc != Z_STREAM_END) {
            inflateEnd(&zs);
            throw std::runtime_error("inflate failed for gzip_packed payload");
        }
        size_t produced = chunk.size() - zs.avail_out;
        out.insert(out.end(), chunk.begin(), chunk.begin() + produced);
    }

    inflateEnd(&zs);
    return out;
}

static Bytes normalize_rpc_payload(Bytes payload) {
    while (payload.size() >= 4) {
        TLReader r(payload);
        int32_t cid = r.readInt32();
        if (cid != TL::gzip_packed) break;
        payload = gunzip_bytes(r.readBytes());
    }
    return payload;
}

static void join_thread_if_needed(std::thread& t) {
    if (!t.joinable()) return;
    if (t.get_id() == std::this_thread::get_id()) t.detach();
    else t.join();
}

Client::Client() = default;

Client::~Client() {
    disconnect();
}

bool Client::connect(const std::string& bot_token, int dc_id) {
    m_auth_mode = AuthMode::BotToken;
    m_bot_token = bot_token;
    m_import_auth_id = 0;
    m_import_auth_bytes.clear();
    m_dc_id = dc_id;
    m_stopping = false;
    m_backoff_s = 1;
    m_reconnect_scheduled = false;
    return do_connect();
}

bool Client::connect_imported_auth(int dc_id, int64_t auth_id, const Bytes& auth_bytes) {
    m_auth_mode = AuthMode::ImportedAuth;
    m_bot_token.clear();
    m_import_auth_id = auth_id;
    m_import_auth_bytes = auth_bytes;
    m_dc_id = dc_id;
    m_stopping = false;
    m_backoff_s = 1;
    m_reconnect_scheduled = false;
    return do_connect();
}

bool Client::do_connect() {
    std::array<bool, 6> tried{};
    const bool fixedDc = (m_auth_mode == AuthMode::ImportedAuth);
    int attempt_dc = (m_dc_id >= 1 && m_dc_id <= 5) ? m_dc_id : 2;

    auto next_untried = [&](int after_dc) -> int {
        if (fixedDc) return 0;
        for (int step = 1; step <= 5; ++step) {
            int dc = ((after_dc - 1 + step) % 5) + 1;
            if (!tried[dc]) return dc;
        }
        return 0;
    };

    while (!m_stopping && attempt_dc >= 1 && attempt_dc <= 5 && !tried[attempt_dc]) {
        tried[attempt_dc] = true;
        try {
            std::cout << "  [MTProto] Connecting to DC" << attempt_dc << "..." << std::endl;

            m_transport = std::make_unique<Transport>();
            m_transport->connect(attempt_dc);

            m_auth_key = std::make_unique<AuthKey>(generate_auth_key(*m_transport, attempt_dc));
            m_session = std::make_unique<Session>(*m_transport, *m_auth_key);

            std::cout << "  [MTProto] Authenticating..." << std::endl;
            Bytes auth_req = (m_auth_mode == AuthMode::ImportedAuth)
                ? build_import_authorization(m_import_auth_id, m_import_auth_bytes)
                : build_import_bot_auth(m_bot_token);
            int64_t msg_id = m_session->send(wrap_init_connection(auth_req));

            bool auth_ok = false;
            auto inspect_auth_payload = [&](const Bytes& payload, auto&& self) -> bool {
                if (payload.size() < 4) return false;
                TLReader r(payload);
                int32_t cid = r.readInt32();
                if (cid == TL::rpc_result) {
                    int64_t req = r.readInt64();
                    if (req != msg_id) return false;
                    Bytes inner(payload.begin() + 12, payload.end());
                    inner = normalize_rpc_payload(std::move(inner));
                    TLReader inner_reader(inner);
                    int32_t inner_cid = inner_reader.readInt32();
                    if (inner_cid == TL::rpc_error) {
                        int32_t err_code = inner_reader.readInt32();
                        std::string err_msg = inner_reader.readString();
                        throw std::runtime_error(std::string(
                            (m_auth_mode == AuthMode::ImportedAuth) ? "Imported auth error " : "Bot auth error ")
                            + std::to_string(err_code) + ": " + err_msg);
                    }
                    return true;
                }
                if (cid == TL::new_session_created) {
                    int64_t first_msg = r.readInt64();
                    r.readInt64();
                    int64_t server_salt = r.readInt64();
                    if (m_session) m_session->set_server_salt(server_salt);
                    try {
                        std::lock_guard<std::mutex> send_lock(m_send_mutex);
                        m_session->send(Session::make_ack(first_msg), false);
                    } catch (...) {}
                    return false;
                }
                if (cid == TL::bad_server_salt) {
                    r.readInt64();
                    r.readInt32();
                    r.readInt32();
                    int64_t new_salt = r.readInt64();
                    if (m_session) m_session->set_server_salt(new_salt);
                    return false;
                }
                if (cid == TL::msg_container) {
                    int32_t n = r.readInt32();
                    for (int i = 0; i < n; ++i) {
                        r.readInt64();
                        r.readInt32();
                        int32_t bytes = r.readInt32();
                        Bytes inner = r.readRaw(bytes);
                        if (self(inner, self)) return true;
                    }
                }
                return false;
            };

            int timeouts = 0;
            while (!auth_ok && timeouts < 6) {
                try {
                    Bytes resp = m_session->recv();
                    if (resp.empty()) throw std::runtime_error("Empty auth response");
                    auth_ok = inspect_auth_payload(resp, inspect_auth_payload);
                } catch (const TransportTimeoutError&) {
                    ++timeouts;
                }
            }
            if (!auth_ok) throw std::runtime_error("Timed out waiting for bot auth result");

            m_connected = true;
            m_backoff_s = 1;
            m_reconnect_scheduled = false;
            m_dc_id = attempt_dc;
            std::cout << "  [MTProto] ✅ Connected to DC" << m_dc_id;
            if (m_auth_mode == AuthMode::ImportedAuth) {
                std::cout << " with imported authorization";
            } else {
                std::cout << " as bot (AlexaInc/QuotlyNative)";
            }
            std::cout << std::endl;

            join_thread_if_needed(m_recv_thread);
            m_recv_thread = std::thread([this] { recv_loop(); });
            return true;

        } catch (const std::exception& e) {
            const std::string err = e.what();
            std::cerr << "  [MTProto] ❌ Connection failed on DC" << attempt_dc << ": " << err << std::endl;
            if (m_transport) m_transport->close();
            m_transport = nullptr;
            m_auth_key = nullptr;
            m_session = nullptr;
            m_connected = false;

            int migrate_dc = parse_migrate_dc(err);
            if (!fixedDc && migrate_dc >= 1 && migrate_dc <= 5 && !tried[migrate_dc]) {
                attempt_dc = migrate_dc;
                continue;
            }
        }

        attempt_dc = next_untried(attempt_dc);
    }

    std::cerr << "  [MTProto] ❌ All DCs failed to connect." << std::endl;
    m_connected = false;
    return false;
}

void Client::recv_loop() {
    int64_t ping_id = 1;
    auto last_ping = std::chrono::steady_clock::now();
    constexpr auto ping_interval = std::chrono::seconds(20);

    while (!m_stopping && m_connected) {
        auto now = std::chrono::steady_clock::now();
        if (now - last_ping >= ping_interval) {
            try {
                std::lock_guard<std::mutex> send_lock(m_send_mutex);
                if (m_session) m_session->send(build_ping_delay(ping_id++), false);
            } catch (...) {}
            last_ping = now;
        }

        Bytes payload;
        try {
            payload = m_session->recv();
        } catch (const TransportTimeoutError&) {
            continue;
        } catch (const std::exception& e) {
            if (!m_stopping) {
                std::cerr << "  [MTProto] Recv error: " << e.what() << std::endl;
                m_connected = false;
                schedule_reconnect();
            }
            return;
        }

        if (payload.empty()) {
            if (!m_stopping) {
                std::cerr << "  [MTProto] Connection closed by peer." << std::endl;
                m_connected = false;
                schedule_reconnect();
            }
            return;
        }

        dispatch(payload);
    }
}

void Client::dispatch(const Bytes& payload) {
    if (payload.size() < 4) return;

    TLReader r(payload);
    int32_t cid = r.readInt32();

    if (cid == TL::rpc_result) {
        int64_t req_id = r.readInt64();
        Bytes inner(payload.begin() + 12, payload.end());
        inner = normalize_rpc_payload(std::move(inner));

        std::lock_guard<std::mutex> lk(m_pending_mutex);
        auto it = m_pending.find(req_id);
        if (it != m_pending.end()) {
            it->second.set_value(inner);
            m_pending.erase(it);
        }
    } else if (cid == TL::bad_server_salt) {
        r.readInt64();
        r.readInt32();
        r.readInt32();
        int64_t new_salt = r.readInt64();
        if (m_session) m_session->set_server_salt(new_salt);
    } else if (cid == TL::new_session_created) {
        int64_t first_msg = r.readInt64();
        r.readInt64();
        int64_t server_salt = r.readInt64();
        if (m_session) m_session->set_server_salt(server_salt);
        try {
            std::lock_guard<std::mutex> send_lock(m_send_mutex);
            if (m_session) m_session->send(Session::make_ack(first_msg), false);
        } catch (...) {}
    } else if (cid == TL::pong) {
        // keepalive pong — nothing to do
    } else if (cid == TL::msg_container) {
        int32_t count = r.readInt32();
        for (int i = 0; i < count; ++i) {
            r.readInt64();
            r.readInt32();
            int32_t bytes = r.readInt32();
            Bytes inner = r.readRaw(bytes);
            dispatch(inner);
        }
    }
}

RpcResult Client::invoke(const Bytes& request, int timeout_ms) {
    if (!m_connected || !m_session) {
        return { false, {}, -1, "Not connected" };
    }

    std::promise<Bytes> promise;
    auto future = promise.get_future();
    int64_t msg_id = 0;

    try {
        std::lock_guard<std::mutex> pending_lock(m_pending_mutex);
        std::lock_guard<std::mutex> send_lock(m_send_mutex);
        if (!m_connected || !m_session) {
            return { false, {}, -1, "Not connected" };
        }
        Bytes wrapped = wrap_init_connection(request);
        msg_id = m_session->send(wrapped);
        m_pending[msg_id] = std::move(promise);
    } catch (const std::exception& e) {
        if (msg_id != 0) {
            std::lock_guard<std::mutex> lk(m_pending_mutex);
            m_pending.erase(msg_id);
        }
        if (!m_stopping) schedule_reconnect();
        return { false, {}, -1, e.what() };
    }

    auto status = future.wait_for(std::chrono::milliseconds(timeout_ms));
    if (status != std::future_status::ready) {
        std::lock_guard<std::mutex> lk(m_pending_mutex);
        m_pending.erase(msg_id);
        return { false, {}, -408, "Request timeout" };
    }

    Bytes result;
    try {
        result = future.get();
    } catch (const std::exception& e) {
        return { false, {}, -1, e.what() };
    }

    if (result.size() >= 4) {
        TLReader r(result);
        int32_t cid = r.readInt32();
        if (cid == TL::rpc_error) {
            int32_t code = r.readInt32();
            std::string msg = r.readString();
            return { false, {}, code, msg };
        }
    }

    return { true, result, 0, "" };
}

void Client::schedule_reconnect() {
    if (m_stopping) return;

    bool expected = false;
    if (!m_reconnect_scheduled.compare_exchange_strong(expected, true)) {
        return;
    }

    {
        std::lock_guard<std::mutex> lk(m_pending_mutex);
        for (auto& [id, p] : m_pending) {
            try {
                p.set_exception(std::make_exception_ptr(std::runtime_error("Disconnected")));
            } catch (...) {}
        }
        m_pending.clear();
    }

    int backoff = m_backoff_s;
    m_backoff_s = std::min(m_backoff_s * 2, 60);

    join_thread_if_needed(m_reconnect_thread);
    m_reconnect_thread = std::thread([this, backoff] {
        std::cout << "  [MTProto] Reconnecting in " << backoff << "s..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(backoff));

        bool ok = false;
        if (!m_stopping) {
            ok = do_connect();
        }

        m_reconnect_scheduled = false;
        if (!ok && !m_stopping) {
            schedule_reconnect();
        }
    });
}

void Client::disconnect() {
    m_stopping = true;
    m_connected = false;
    m_reconnect_scheduled = false;

    {
        std::lock_guard<std::mutex> lk(m_pending_mutex);
        for (auto& [id, p] : m_pending) {
            try {
                p.set_exception(std::make_exception_ptr(std::runtime_error("Disconnected")));
            } catch (...) {}
        }
        m_pending.clear();
    }

    if (m_transport) m_transport->close();

    join_thread_if_needed(m_recv_thread);
    join_thread_if_needed(m_reconnect_thread);

    m_session = nullptr;
    m_auth_key = nullptr;
    m_transport = nullptr;

    std::cout << "  [MTProto] Disconnected." << std::endl;
}

} // namespace MTProto
