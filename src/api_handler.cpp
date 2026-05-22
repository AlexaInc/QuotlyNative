// this file is part of AlexaInc / QuotlyNative — API Handler
// developer hansaka@alexainc

#include "api_handler.h"
#include "renderer.h"
#include "text_engine.h"
#include "tg_client.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <filesystem>
#include <map>
#include <fstream>
#include <vector>
#include <cstdlib>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace Quote {
static std::string g_apiLogs;
static std::mutex g_logMutex;

void apiLog(const std::string& msg) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    g_apiLogs += msg + "\n";
    if (g_apiLogs.size() > 50000) g_apiLogs = g_apiLogs.substr(25000); // Keep last 25KB
    std::cout << msg << std::endl;
}

// ── Base64 Decoding Helper ───────────────────────────────────────────────────
static std::string base64Decode(const std::string& in) {
    std::string out;
    std::vector<int> T(256, -1);
    for (int i = 0; i < 64; i++) T["ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"[i]] = i;

    int val = 0, valb = -8;
    for (unsigned char c : in) {
        if (T[c] == -1) continue;
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0) {
            out.push_back(char((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

static std::string saveBase64ToTemp(const std::string& b64Data, const std::string& prefix) {
    if (b64Data.empty()) return "";
    size_t comma = b64Data.find(',');
    std::string actualData = (comma != std::string::npos) ? b64Data.substr(comma + 1) : b64Data;
    std::string decoded = base64Decode(actualData);
    if (decoded.empty()) return "";

    std::string path = "/tmp/" + prefix + "_" + std::to_string(time(NULL)) + "_" + std::to_string(rand()) + ".png";
    std::ofstream ofs(path, std::ios::binary);
    ofs.write(decoded.data(), decoded.size());
    return path;
}

void ApiHandler::setupRoutes(crow::SimpleApp& app) {
    CROW_ROUTE(app, "/quote").methods(crow::HTTPMethod::POST)(handleQuoteRequest);
    CROW_ROUTE(app, "/api/generate").methods(crow::HTTPMethod::POST)(handleQuoteRequest); // JS API Alias
    CROW_ROUTE(app, "/debug/logs").methods(crow::HTTPMethod::GET)([](){
        std::lock_guard<std::mutex> lock(g_logMutex);
        return crow::response(g_apiLogs);
    });
}

crow::response ApiHandler::handleQuoteRequest(const crow::request& req) {
    try {
        auto body = nlohmann::json::parse(req.body);

        if (!body.contains("messages") || !body["messages"].is_array() || body["messages"].empty()) {
            return crow::response(400, "Invalid payload: 'messages' array required");
        }

        std::string outputPath = "/tmp/quote_" + std::to_string(time(NULL)) + "_" + std::to_string(rand()) + ".png";
        bool transparent = body.value("transparent", true);
        
        // Track temp files for cleanup
        std::vector<std::string> tempFiles;

        std::vector<MessageData> msgs;
        for (const auto& item : body["messages"]) {
            MessageData msg;
            
            // ── Field Aliasing: text / message ──────────────────────────────
            msg.text = item.value("text", item.value("message", ""));
            
            // ── User Identity & Grouping Key ─────────────────────────────────
            int64_t uid = 0;
            bool hasUid = false;
            if (item.contains("from") && item["from"].contains("id")) {
                uid = item["from"]["id"];
                hasUid = true;
            } else if (item.contains("id")) {
                uid = item.value("id", 0LL);
                hasUid = true;
            }

            int nci = item.value("nameColorId", 0);
            std::string fn = item.value("firstName", "");
            std::string ln = item.value("lastName", "");
            if (item.contains("from")) {
                fn = item["from"].value("first_name", fn);
                ln = item["from"].value("last_name", ln);
            }
            msg.senderName = fn;
            if (!ln.empty()) msg.senderName += " " + ln;
            msg.senderId = hasUid ? (int)uid : nci;

            if (hasUid) {
                msg.senderKey = "id:" + std::to_string(uid);
            } else {
                msg.senderKey = "name:" + fn + "|" + ln + "|color:" + std::to_string(nci);
            }

            // Entities
            auto entities = item.value("entities", nlohmann::json::array());
            msg.pangoMarkup = TextEngine::processEntities(msg.text, entities);

            // ── Field Aliasing: reply_to / replySender ──────────────────────
            if (item.contains("reply_to")) {
                const auto& r = item["reply_to"];
                msg.reply.hasReply = true;
                msg.reply.text = r.value("text", "");
                if (r.contains("from")) {
                    const auto& rf = r["from"];
                    msg.reply.senderName = rf.value("first_name", "");
                    msg.reply.senderId   = rf.value("id", 0);
                }
            } else if (item.contains("replySender")) {
                msg.reply.hasReply = true;
                msg.reply.senderName = item.value("replySender", "");
                msg.reply.text       = item.value("replyMessage", "");
                msg.reply.senderId   = item.value("replySenderColor", 0);
            }
            
            // ── Base64 Images ────────────────────────────────────────────────
            if (item.contains("avatarBase64")) {
                // (Note: in our current renderer logic, we draw avatar from senderId,
                // but we could extend drawAvatar to take a path instead)
            }
            if (item.contains("mediaBase64")) {
                msg.photoPath = saveBase64ToTemp(item.value("mediaBase64", ""), "media");
                if (!msg.photoPath.empty()) tempFiles.push_back(msg.photoPath);
            }

            // ── Premium Emojis & Status ─────────────────────────────────────
            msg.emojiStatusId = item.value("custom_emoji_id", item.value("customemojiid", 0ULL));
            if (item.contains("from") && item["from"].contains("emoji_status_custom_emoji_id")) {
                msg.emojiStatusId = item["from"]["emoji_status_custom_emoji_id"];
            }

            for (const auto& e : entities) {
                if (e.value("type", "") == "custom_emoji") {
                    CustomEmoji ce;
                    ce.offset = e.value("offset", 0);
                    ce.length = e.value("length", 0);
                    ce.documentId = e.value("custom_emoji_id", 0ULL);
                    msg.customEmojis.push_back(ce);
                }
            }

            msgs.push_back(msg);
        }

        // ── Pre-fetch Premium Emojis ─────────────────────────────────────────
        std::map<uint64_t, std::string> emojiMap;
        // Static TgClient initialized from env vars (reused across requests)
        static TgClient s_tgClient(
            std::atoi(std::getenv("TG_API_ID") ? std::getenv("TG_API_ID") : "0"),
            std::getenv("TG_API_HASH") ? std::getenv("TG_API_HASH") : ""
        );
        static bool s_tgConnected = [&](){
            const char* tok = std::getenv("BOT_TOKEN");
            return tok ? s_tgClient.authenticate(tok) : false;
        }();

        if (s_tgConnected) {
            apiLog("[QuoteAPI] handleQuoteRequest " + std::to_string(msgs.size()) + " messages");
            for (const auto& m : msgs) {
                if (m.emojiStatusId != 0 && emojiMap.find(m.emojiStatusId) == emojiMap.end()) {
                    apiLog("[QuoteAPI] Fetching status emoji: " + std::to_string(m.emojiStatusId));
                    std::string path = s_tgClient.fetchCustomEmoji(std::to_string(m.emojiStatusId));
                    if (!path.empty()) {
                        apiLog("[QuoteAPI] Success: " + path);
                        emojiMap[m.emojiStatusId] = path;
                    } else {
                        apiLog("[QuoteAPI] Failed to fetch status emoji");
                    }
                }
                for (const auto& ce : m.customEmojis) {
                    if (emojiMap.find(ce.documentId) == emojiMap.end()) {
                        apiLog("[QuoteAPI] Fetching inline emoji: " + std::to_string(ce.documentId));
                        std::string path = s_tgClient.fetchCustomEmoji(std::to_string(ce.documentId));
                        if (!path.empty()) {
                            apiLog("[QuoteAPI] Success: " + path);
                            emojiMap[ce.documentId] = path;
                        } else {
                            apiLog("[QuoteAPI] Failed to fetch inline emoji");
                        }
                    }
                }
            }
        } else {
            apiLog("[QuoteAPI] ⚠️ MTProto NOT Connected. Skipping emojis.");
        }

        RenderOptions options;
        options.transparent = transparent;
        options.hasBubble = true;

        apiLog("[QuoteAPI] Rendering quote with " + std::to_string(emojiMap.size()) + " cached emojis");
        Renderer::renderQuote(outputPath, msgs, options, emojiMap);

        std::ifstream file(outputPath, std::ios::binary);
        if (!file) return crow::response(500, "Failed to read generated quote");

        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        crow::response res(content);
        res.set_header("Content-Type", "image/png");

        // CLEANUP
        std::filesystem::remove(outputPath);
        for (const auto& f : tempFiles) std::filesystem::remove(f);

        return res;

    } catch (const std::exception& e) {
        return crow::response(500, std::string("Internal Error: ") + e.what());
    }
}

} // namespace Quote
