// this file is part of AlexaInc / QuotlyNative — API Handler
// developer hansaka@alexainc

#include "api_handler.h"
#include "renderer.h"
#include "text_engine.h"
#include "tg_client.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <iostream>
#include <filesystem>
#include <map>
#include <mutex>
#include <fstream>
#include <vector>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <cmath>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace Quote {
static std::string g_apiLogs;
static std::mutex g_logMutex;
static std::shared_ptr<TgClient> g_tgClient;

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


static std::string detectDataUriExtension(const std::string& in) {
    if (in.rfind("data:", 0) != 0) return ".bin";
    size_t semi = in.find(';');
    if (semi == std::string::npos) return ".bin";
    std::string mime = in.substr(5, semi - 5);
    if (mime == "image/png") return ".png";
    if (mime == "image/jpeg" || mime == "image/jpg") return ".jpg";
    if (mime == "image/webp") return ".webp";
    if (mime == "image/gif") return ".gif";
    if (mime == "video/webm") return ".webm";
    if (mime == "application/x-tgsticker") return ".tgs";
    return ".bin";
}

static MediaType parseMediaType(const nlohmann::json& item) {
    std::string mediaType = item.value("mediaType", item.value("media_type", ""));
    std::transform(mediaType.begin(), mediaType.end(), mediaType.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    if (mediaType == "photo" || mediaType == "image") return MediaType::Photo;
    if (mediaType == "sticker") return MediaType::Sticker;
    return MediaType::None;
}

static uint64_t parseUInt64Json(const nlohmann::json& obj, const std::string& key, uint64_t fallback = 0) {
    if (!obj.contains(key) || obj[key].is_null()) return fallback;
    const auto& v = obj[key];
    try {
        if (v.is_number_unsigned()) return v.get<uint64_t>();
        if (v.is_number_integer()) {
            auto n = v.get<int64_t>();
            return n > 0 ? static_cast<uint64_t>(n) : fallback;
        }
        if (v.is_string()) {
            std::string str = v.get<std::string>();
            if (str.empty()) return fallback;
            return static_cast<uint64_t>(std::stoull(str));
        }
    } catch (...) {}
    return fallback;
}

// Telegram entity offsets are UTF-16 code units; Pango wants UTF-8 byte indices.
static int utf16OffsetToUtf8ByteOffset(const std::string& text, int utf16Offset) {
    if (utf16Offset <= 0) return 0;
    int units = 0;
    size_t i = 0;
    while (i < text.size() && units < utf16Offset) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        uint32_t cp = 0;
        size_t len = 1;
        if ((c & 0x80) == 0) { cp = c; len = 1; }
        else if ((c & 0xE0) == 0xC0 && i + 1 < text.size()) {
            cp = ((c & 0x1F) << 6) | (static_cast<unsigned char>(text[i+1]) & 0x3F); len = 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < text.size()) {
            cp = ((c & 0x0F) << 12) | ((static_cast<unsigned char>(text[i+1]) & 0x3F) << 6) | (static_cast<unsigned char>(text[i+2]) & 0x3F); len = 3;
        } else if ((c & 0xF8) == 0xF0 && i + 3 < text.size()) {
            cp = ((c & 0x07) << 18) | ((static_cast<unsigned char>(text[i+1]) & 0x3F) << 12) | ((static_cast<unsigned char>(text[i+2]) & 0x3F) << 6) | (static_cast<unsigned char>(text[i+3]) & 0x3F); len = 4;
        } else { cp = c; len = 1; }
        int cpUnits = (cp >= 0x10000) ? 2 : 1;
        if (units + cpUnits > utf16Offset) break;
        units += cpUnits;
        i += len;
    }
    return static_cast<int>(i);
}

static std::string saveBase64ToTemp(const std::string& b64Data, const std::string& prefix) {
    if (b64Data.empty()) return "";
    size_t comma = b64Data.find(',');
    std::string actualData = (comma != std::string::npos) ? b64Data.substr(comma + 1) : b64Data;
    std::string decoded = base64Decode(actualData);
    if (decoded.empty()) return "";

    std::string ext = detectDataUriExtension(b64Data);
    std::string path = "/tmp/" + prefix + "_" + std::to_string(time(NULL)) + "_" + std::to_string(rand()) + ext;
    std::ofstream ofs(path, std::ios::binary);
    ofs.write(decoded.data(), decoded.size());
    return path;
}


struct PngSize { int w = 0; int h = 0; };

static PngSize readPngSize(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return {};
    uint8_t sig[24]{};
    if (fread(sig, 1, sizeof(sig), f) != sizeof(sig)) { fclose(f); return {}; }
    fclose(f);
    if (!(sig[0] == 0x89 && sig[1] == 'P' && sig[2] == 'N' && sig[3] == 'G')) return {};
    int w = (sig[16] << 24) | (sig[17] << 16) | (sig[18] << 8) | sig[19];
    int h = (sig[20] << 24) | (sig[21] << 16) | (sig[22] << 8) | sig[23];
    return {w, h};
}

static std::string shellQuoteApi(const std::string& s) {
    std::string out = "'";
    for (char c : s) out += (c == '\'') ? "'\\''" : std::string(1, c);
    out += "'";
    return out;
}

static bool runCwebpSticker(const std::string& inputPng,
                            const std::string& outputWebp,
                            int targetW,
                            int targetH,
                            const std::string& qualityArgs) {
    std::string cmd = "cwebp -quiet " + qualityArgs + " -m 6 -metadata none";
    if (targetW > 0 && targetH > 0) {
        cmd += " -resize " + std::to_string(targetW) + " " + std::to_string(targetH);
    }
    cmd += " " + shellQuoteApi(inputPng) + " -o " + shellQuoteApi(outputWebp) + " >/dev/null 2>&1";
    int rc = system(cmd.c_str());
    return rc == 0 && std::filesystem::exists(outputWebp) && std::filesystem::file_size(outputWebp) > 0;
}

static std::string makeTelegramStickerWebp(const std::string& inputPng, double requestedMaxSide) {
    // Telegram static stickers must fit inside a 512px box.  Keep the original
    // renderer untouched, then make a safe WebP copy with high-quality cwebp
    // resizing.  This avoids Telegram doing a rough conversion/resample later.
    const int maxSide = (int)std::clamp(requestedMaxSide, 128.0, 512.0);
    PngSize size = readPngSize(inputPng);

    int targetW = 0;
    int targetH = 0;
    if (size.w > 0 && size.h > 0) {
        if (size.w >= size.h) {
            targetW = maxSide;
            targetH = std::max(1, (int)std::lround((double)size.h * targetW / size.w));
        } else {
            targetH = maxSide;
            targetW = std::max(1, (int)std::lround((double)size.w * targetH / size.h));
        }
    }

    std::string webpPath = inputPng + ".sticker.webp";
    if (runCwebpSticker(inputPng, webpPath, targetW, targetH, "-lossless -q 100") &&
        std::filesystem::file_size(webpPath) <= 512 * 1024) {
        return webpPath;
    }

    // If lossless exceeds Telegram's 512KB sticker limit (usually because of a
    // photo inside the quote), retry lossy.  Text-only quotes normally stay on
    // the first lossless path.
    for (int q : {95, 90, 85, 80, 75, 65, 55, 45}) {
        std::filesystem::remove(webpPath);
        if (runCwebpSticker(inputPng, webpPath, targetW, targetH, "-q " + std::to_string(q)) &&
            std::filesystem::file_size(webpPath) <= 512 * 1024) {
            return webpPath;
        }
    }

    std::filesystem::remove(webpPath);
    return "";
}

void ApiHandler::setTgClient(const std::shared_ptr<TgClient>& tgClient) {
    g_tgClient = tgClient;
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
            if (item.contains("reply_to") || item.contains("reply_to_message")) {
                const auto& r = item.contains("reply_to") ? item["reply_to"] : item["reply_to_message"];
                msg.reply.hasReply = true;
                msg.reply.text = r.value("text", "");
                if (r.contains("from")) {
                    const auto& rf = r["from"];
                    msg.reply.senderName = rf.value("first_name", "");
                    msg.reply.senderId   = rf.value("id", 0);
                }
                auto rentities = r.value("entities", nlohmann::json::array());
                msg.reply.pangoMarkup = TextEngine::processEntities(msg.reply.text, rentities);
                if (rentities.is_array()) {
                    for (const auto& e : rentities) {
                        if (e.value("type", "") == "custom_emoji") {
                            CustomEmoji ce;
                            int off16 = e.value("offset", 0);
                            int len16 = e.value("length", 0);
                            ce.offset = utf16OffsetToUtf8ByteOffset(msg.reply.text, off16);
                            ce.length = utf16OffsetToUtf8ByteOffset(msg.reply.text, off16 + len16) - ce.offset;
                            ce.documentId = parseUInt64Json(e, "custom_emoji_id", parseUInt64Json(e, "document_id", 0));
                            if (ce.documentId != 0) msg.reply.customEmojis.push_back(ce);
                        }
                    }
                }
            } else if (item.contains("replySender")) {
                msg.reply.hasReply = true;
                msg.reply.senderName = item.value("replySender", "");
                msg.reply.text       = item.value("replyMessage", "");
                msg.reply.senderId   = item.value("replySenderColor", 0);
            }
            
            // ── Base64 Images ────────────────────────────────────────────────
            if (item.contains("avatarBase64")) {
                msg.avatarPath = saveBase64ToTemp(item.value("avatarBase64", ""), "avatar");
                if (!msg.avatarPath.empty()) {
                    tempFiles.push_back(msg.avatarPath);
                    apiLog("[QuoteAPI] Decoded avatarBase64 -> " + msg.avatarPath);
                }
            }
            if (item.contains("mediaBase64")) {
                msg.photoPath = saveBase64ToTemp(item.value("mediaBase64", ""), "media");
                msg.mediaType = parseMediaType(item);
                if (msg.mediaType == MediaType::None && !msg.photoPath.empty()) {
                    msg.mediaType = MediaType::Photo;
                }
                if (!msg.photoPath.empty()) tempFiles.push_back(msg.photoPath);
            }

            // ── Premium Emojis & Status ─────────────────────────────────────
            msg.emojiStatusId = parseUInt64Json(item, "custom_emoji_id",
                                parseUInt64Json(item, "customemojiid",
                                parseUInt64Json(item, "emoji_status_custom_emoji_id", 0)));
            if (item.contains("from") && item["from"].is_object()) {
                msg.emojiStatusId = parseUInt64Json(item["from"], "emoji_status_custom_emoji_id", msg.emojiStatusId);
            }

            if (entities.is_array()) {
                for (const auto& e : entities) {
                    if (e.value("type", "") == "custom_emoji") {
                        CustomEmoji ce;
                        int off16 = e.value("offset", 0);
                        int len16 = e.value("length", 0);
                        ce.offset = utf16OffsetToUtf8ByteOffset(msg.text, off16);
                        ce.length = utf16OffsetToUtf8ByteOffset(msg.text, off16 + len16) - ce.offset;
                        ce.documentId = parseUInt64Json(e, "custom_emoji_id", parseUInt64Json(e, "document_id", 0));
                        if (ce.documentId != 0) msg.customEmojis.push_back(ce);
                    }
                }
            }

            msgs.push_back(msg);
        }

        // ── Pre-fetch Premium Emojis ─────────────────────────────────────────
        std::map<uint64_t, std::string> emojiMap;

        if (g_tgClient) {
            apiLog("[QuoteAPI] handleQuoteRequest " + std::to_string(msgs.size()) + " messages");
            for (const auto& m : msgs) {
                if (m.emojiStatusId != 0 && emojiMap.find(m.emojiStatusId) == emojiMap.end()) {
                    apiLog("[QuoteAPI] Fetching status emoji: " + std::to_string(m.emojiStatusId));
                    std::string path = g_tgClient->fetchCustomEmoji(std::to_string(m.emojiStatusId));
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
                        std::string path = g_tgClient->fetchCustomEmoji(std::to_string(ce.documentId));
                        if (!path.empty()) {
                            apiLog("[QuoteAPI] Success: " + path);
                            emojiMap[ce.documentId] = path;
                        } else {
                            apiLog("[QuoteAPI] Failed to fetch inline emoji");
                        }
                    }
                }
                for (const auto& ce : m.reply.customEmojis) {
                    if (emojiMap.find(ce.documentId) == emojiMap.end()) {
                        apiLog("[QuoteAPI] Fetching reply inline emoji: " + std::to_string(ce.documentId));
                        std::string path = g_tgClient->fetchCustomEmoji(std::to_string(ce.documentId));
                        if (!path.empty()) {
                            emojiMap[ce.documentId] = path;
                        }
                    }
                }
            }
        } else {
            apiLog("[QuoteAPI] ⚠️ Telegram client not configured. Premium emoji fetching disabled.");
        }

        RenderOptions options;
        options.transparent = transparent;
        options.hasBubble = true;

        apiLog("[QuoteAPI] Rendering quote with " + std::to_string(emojiMap.size()) + " cached emojis");
        Renderer::renderQuote(outputPath, msgs, options, emojiMap);

        // Keep the original renderer exactly as-is.  Only after the PNG is
        // created, optionally produce a Telegram static-sticker-safe WebP
        // (<=512px on the longest side and <=512KB).
        std::string responsePath = outputPath;
        std::string contentType = "image/png";
        std::string format = body.value("format", body.value("outputFormat", std::string("png")));
        std::transform(format.begin(), format.end(), format.begin(),
                       [](unsigned char c) { return (char)std::tolower(c); });
        bool wantStickerWebp = body.value("webp", false) ||
                               body.value("telegramSticker", false) ||
                               body.value("sticker", false) ||
                               body.value("stickerMode", false) ||
                               format == "webp" || format == "sticker";
        if (wantStickerWebp) {
            std::string webpPath = makeTelegramStickerWebp(outputPath, body.value("stickerMaxSide", 512.0));
            if (!webpPath.empty()) {
                responsePath = webpPath;
                contentType = "image/webp";
                tempFiles.push_back(webpPath);
                apiLog("[QuoteAPI] Telegram sticker-safe WebP created: " + webpPath);
            } else {
                apiLog("[QuoteAPI] cwebp sticker conversion failed; falling back to PNG");
            }
        }

        std::ifstream file(responsePath, std::ios::binary);
        if (!file) return crow::response(500, "Failed to read generated quote");

        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        crow::response res(content);
        res.set_header("Content-Type", contentType);

        // CLEANUP
        std::filesystem::remove(outputPath);
        for (const auto& f : tempFiles) std::filesystem::remove(f);

        return res;

    } catch (const std::exception& e) {
        return crow::response(500, std::string("Internal Error: ") + e.what());
    }
}

} // namespace Quote
