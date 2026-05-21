// this file is part of AlexaInc / QuotlyNative — API Handler
// developer hansaka@alexainc

#include "api_handler.h"
#include "renderer.h"
#include "text_engine.h"
#include <filesystem>
#include <iostream>
#include <fstream>
#include <vector>

namespace Quote {

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
            
            // ── Field Aliasing: from / firstName ────────────────────────────
            if (item.contains("from")) {
                const auto& from = item["from"];
                msg.senderName = from.value("first_name", "");
                std::string ln = from.value("last_name", "");
                if (!ln.empty()) msg.senderName += " " + ln;
                msg.senderId = from.value("id", 0);
            } else if (item.contains("firstName")) {
                msg.senderName = item.value("firstName", "");
                std::string ln = item.value("lastName", "");
                if (!ln.empty()) msg.senderName += " " + ln;
                msg.senderId = item.value("nameColorId", 0);
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

            // ── Media Type ───────────────────────────────────────────────────
            std::string mt = item.value("mediaType", "");
            if (mt == "sticker") {
                msg.mediaType = MediaType::Sticker;
            } else if (mt == "photo" || mt == "image" || mt == "video") {
                msg.mediaType = MediaType::Photo;
            } else if (!msg.photoPath.empty()) {
                // Auto-detect: if media exists but no type, default to photo
                msg.mediaType = MediaType::Photo;
            }

            msgs.push_back(msg);
        }

        RenderOptions options;
        options.transparent = transparent;
        options.hasBubble = true;

        Renderer::renderQuote(outputPath, msgs, options);

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
