// this file is part of AlexaInc / QuotlyNative — TG Client
// developer hansaka@alexainc

#include "tg_client.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <sys/stat.h>

namespace Quote {

void apiLog(const std::string& msg);
static bool fileExists(const std::string& path);

namespace {
std::mutex g_failedEmojiMutex;
std::map<std::string, std::chrono::steady_clock::time_point> g_failedEmojiCache;
constexpr auto kFailedEmojiBackoff = std::chrono::minutes(2);

bool wasRecentFailure(const std::string& emojiId) {
    std::lock_guard<std::mutex> lock(g_failedEmojiMutex);
    auto it = g_failedEmojiCache.find(emojiId);
    if (it == g_failedEmojiCache.end()) return false;
    if (std::chrono::steady_clock::now() - it->second > kFailedEmojiBackoff) {
        g_failedEmojiCache.erase(it);
        return false;
    }
    return true;
}

void rememberFailure(const std::string& emojiId) {
    std::lock_guard<std::mutex> lock(g_failedEmojiMutex);
    g_failedEmojiCache[emojiId] = std::chrono::steady_clock::now();
}

void clearFailure(const std::string& emojiId) {
    std::lock_guard<std::mutex> lock(g_failedEmojiMutex);
    g_failedEmojiCache.erase(emojiId);
}
}

TgClient::TgClient(int apiId, const std::string& apiHash)
    : m_apiId(apiId), m_apiHash(apiHash) {
}

TgClient::~TgClient() {
    m_mtproto.disconnect();
}

bool TgClient::authenticate(const std::string& botToken) {
    if (m_mtproto.is_connected()) return true;
    if (m_apiId <= 0 || m_apiHash.empty() || botToken.empty()) {
        std::cerr << "TgClient: Missing credentials." << std::endl;
        return false;
    }
    return m_mtproto.connect(botToken, 2);
}

static std::string shellQuote(const std::string& s) {
    std::string out = "'";
    for (char c : s) out += (c == '\'') ? "'\\''" : std::string(1, c);
    out += "'";
    return out;
}

static std::string runCommandCapture(const std::string& cmd) {
    std::array<char, 4096> buffer{};
    std::string result;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "";
    while (fgets(buffer.data(), (int)buffer.size(), pipe) != nullptr) result += buffer.data();
    pclose(pipe);
    return result;
}

static std::string extFromPath(const std::string& path) {
    size_t q = path.find('?');
    std::string clean = q == std::string::npos ? path : path.substr(0, q);
    size_t dot = clean.find_last_of('.');
    if (dot == std::string::npos) return ".bin";
    return clean.substr(dot);
}

static std::string fetchCustomEmojiViaBotApi(const std::string& emojiId) {
    const char* tok = std::getenv("BOT_TOKEN");
    if (!tok || !*tok) return "";
    std::string token(tok);

    try {
        std::string ids = "[\"" + emojiId + "\"]";
        std::string url = "https://api.telegram.org/bot" + token + "/getCustomEmojiStickers";
        std::string cmd =
            "curl -sSf --connect-timeout 3 --max-time 8 -X POST --data-urlencode custom_emoji_ids=" +
            shellQuote(ids) + " " + shellQuote(url);
        std::string body = runCommandCapture(cmd);
        if (body.empty()) return "";

        auto j = nlohmann::json::parse(body);
        if (!j.value("ok", false) || !j.contains("result") || j["result"].empty()) return "";
        const auto& st = j["result"][0];

        std::string fileId;
        if (st.contains("thumbnail") && st["thumbnail"].contains("file_id")) fileId = st["thumbnail"].value("file_id", "");
        else if (st.contains("thumb") && st["thumb"].contains("file_id")) fileId = st["thumb"].value("file_id", "");
        else fileId = st.value("file_id", "");
        if (fileId.empty()) return "";

        std::string getFileUrl = "https://api.telegram.org/bot" + token + "/getFile?file_id=" + fileId;
        std::string fileBody = runCommandCapture("curl -sSf --connect-timeout 3 --max-time 8 " + shellQuote(getFileUrl));
        if (fileBody.empty()) return "";

        auto fj = nlohmann::json::parse(fileBody);
        if (!fj.value("ok", false) || !fj.contains("result")) return "";
        std::string filePath = fj["result"].value("file_path", "");
        if (filePath.empty()) return "";

        mkdir("emoji_cache", 0777);
        std::string out = std::string("emoji_cache") + "/emoji_" + emojiId + "_botapi" + extFromPath(filePath);
        if (fileExists(out)) return out;

        std::string downloadUrl = "https://api.telegram.org/file/bot" + token + "/" + filePath;
        int rc = system((
            "curl -L -sSf --connect-timeout 3 --max-time 12 -o " + shellQuote(out) + " " + shellQuote(downloadUrl)
        ).c_str());
        if (rc == 0 && fileExists(out)) {
            apiLog("[TgClient] Bot API emoji asset: " + out);
            return out;
        }
    } catch (const std::exception& e) {
        apiLog(std::string("[TgClient] Bot API fallback failed: ") + e.what());
    }

    return "";
}

static bool fileExists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && st.st_size > 0;
}

static std::string extensionFromBytes(const MTProto::Bytes& b, const std::string& mime = "") {
    if (b.size() >= 8 && b[0] == 0x89 && b[1] == 'P' && b[2] == 'N' && b[3] == 'G') return ".png";
    if (b.size() >= 12 && b[0] == 'R' && b[1] == 'I' && b[2] == 'F' && b[3] == 'F' && b[8] == 'W' && b[9] == 'E' && b[10] == 'B' && b[11] == 'P') return ".webp";
    if (b.size() >= 3 && b[0] == 0xFF && b[1] == 0xD8 && b[2] == 0xFF) return ".jpg";
    if (b.size() >= 4 && b[0] == 0x1A && b[1] == 0x45 && b[2] == 0xDF && b[3] == 0xA3) return ".webm";
    if (b.size() >= 2 && b[0] == 0x1F && b[1] == 0x8B) return ".tgs";
    if (mime == "image/png") return ".png";
    if (mime == "image/webp") return ".webp";
    if (mime == "image/jpeg") return ".jpg";
    if (mime == "video/webm") return ".webm";
    if (mime == "application/x-tgsticker") return ".tgs";
    return ".bin";
}

static std::string saveBytes(const std::string& base, const MTProto::Bytes& b, const std::string& mime = "") {
    if (b.empty()) return "";
    std::string path = base + extensionFromBytes(b, mime);
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return "";
    fwrite(b.data(), 1, b.size(), f);
    fclose(f);
    return path;
}

struct ThumbInfo {
    std::string bestType;
    int bestScore = -1;
    MTProto::Bytes cachedBytes;
};

static void considerThumb(ThumbInfo& info, const std::string& type, int w, int h, int size, const MTProto::Bytes& cached = {}) {
    int score = (w > 0 && h > 0) ? w * h : size;
    if (score > info.bestScore) {
        info.bestScore = score;
        info.bestType = type;
        if (!cached.empty()) info.cachedBytes = cached;
    }
}

static void readPhotoSizeVector(MTProto::TLReader& r, ThumbInfo& info) {
    int32_t vt = r.readInt32();
    if (vt != MTProto::TL::vector) return;

    int32_t count = r.readInt32();
    for (int i = 0; i < count; ++i) {
        int32_t tid = r.readInt32();
        if (tid == MTProto::TL::photoSize || tid == (int32_t)0x77bfb61bu || tid == (int32_t)0x77c01b79u) {
            std::string type = r.readString();
            int w = r.readInt32();
            int h = r.readInt32();
            int size = r.readInt32();
            considerThumb(info, type, w, h, size);
        } else if (tid == MTProto::TL::photoCachedSize || tid == (int32_t)0xe9a73486u) {
            std::string type = r.readString();
            int w = r.readInt32();
            int h = r.readInt32();
            MTProto::Bytes bytes = r.readBytes();
            considerThumb(info, type, w, h, (int)bytes.size(), bytes);
        } else if (tid == (int32_t)0xfa3efb95u) { // photoSizeProgressive
            std::string type = r.readString();
            int w = r.readInt32();
            int h = r.readInt32();
            int32_t vec = r.readInt32();
            int size = 0;
            if (vec == MTProto::TL::vector) {
                int32_t n = r.readInt32();
                for (int j = 0; j < n; ++j) size = std::max(size, r.readInt32());
            }
            considerThumb(info, type, w, h, size);
        } else if (tid == (int32_t)0xd8214d41u) { // photoPathSize
            r.readString();
            r.readBytes();
        } else if (tid == (int32_t)0xe0b0bc2eu) { // photoStrippedSize
            r.readString();
            r.readBytes();
        } else if (tid == (int32_t)0x0e17e23cu || tid == (int32_t)0x111e5e11u) { // photoSizeEmpty variants
            r.readString();
        } else {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "[TgClient] Unknown PhotoSize constructor: 0x%08x", (unsigned)tid);
            apiLog(buf);
            return;
        }
    }
}

static void skipVideoSizeVector(MTProto::TLReader& r) {
    int32_t vt = r.readInt32();
    if (vt != MTProto::TL::vector) return;
    int32_t count = r.readInt32();
    for (int i = 0; i < count; ++i) {
        int32_t tid = r.readInt32();
        if (tid == (int32_t)0xde33b094u) {
            int32_t flags = r.readInt32();
            r.readString();
            r.readInt32();
            r.readInt32();
            r.readInt32();
            if (flags & 1) r.skip(8);
        } else if (tid == (int32_t)0xf85c413cu || tid == (int32_t)0x0da082feu) {
            return;
        } else {
            return;
        }
    }
}

std::string TgClient::fetchCustomEmoji(const std::string& emojiId) {
    uint64_t eid = 0;
    try {
        eid = std::stoull(emojiId);
    } catch (...) {
        apiLog("[TgClient] Invalid emoji id: " + emojiId);
        return "";
    }

    std::string cacheDir = "emoji_cache";
    mkdir(cacheDir.c_str(), 0777);
    for (const char* ext : {".png", ".webp.png", ".jpg.png", ".tgs.png", ".webm.png", ".cached.png", ".webp", ".jpg", ".tgs", ".webm", ".cached", "_botapi.webp", "_botapi.png", "_botapi.jpg", "_botapi.tgs", "_botapi.webm"}) {
        std::string p = cacheDir + "/emoji_" + emojiId + ext;
        if (fileExists(p)) return p;
    }

    if (wasRecentFailure(emojiId)) {
        apiLog("[TgClient] Skipping recent failed emoji lookup: " + emojiId);
        return "";
    }

    auto fetchViaMtproto = [&]() -> std::string {
        if (!m_mtproto.is_connected()) return "";

        std::cout << "[TgClient] Fetching doc for emoji: " << eid << std::endl;

        MTProto::TLWriter w;
        w.writeInt32(MTProto::TL::messages_getCustomEmojiDocuments);
        w.writeInt32(MTProto::TL::vector);
        w.writeInt32(1);
        w.writeInt64((int64_t)eid);

        auto res = m_mtproto.invoke(w.data(), 8000);
        if (!res.ok) {
            apiLog("[TgClient] RPC failed for getCustomEmojiDocuments: " + std::to_string(res.error_code) + " " + res.error_message);
            return "";
        }

        MTProto::TLReader r(res.payload);
        int32_t vecType = r.readInt32();
        if (vecType != MTProto::TL::vector) {
            char buf[96];
            std::snprintf(buf, sizeof(buf), "[TgClient] Expected vector for getCustomEmojiDocuments result, got 0x%08x", (unsigned)vecType);
            apiLog(buf);
            return "";
        }

        int32_t count = r.readInt32();
        if (count <= 0) {
            apiLog("[TgClient] No documents returned for emojiId");
            return "";
        }

        int32_t docType = r.readInt32();
        if (docType != MTProto::TL::document && docType != (int32_t)0x8fdccffau) {
            char buf[96];
            std::snprintf(buf, sizeof(buf), "[TgClient] Expected document constructor, got 0x%08x", (unsigned)docType);
            apiLog(buf);
            return "";
        }

        int32_t flags = r.readInt32();
        int64_t doc_id = r.readInt64();
        int64_t doc_access_hash = r.readInt64();
        MTProto::Bytes doc_file_ref = r.readBytes();
        r.readInt32();
        std::string mime = r.readString();
        r.readInt64();

        apiLog("[TgClient] Found Document ID: " + std::to_string(doc_id) + " mime=" + mime);

        ThumbInfo thumb;
        if (flags & 1) readPhotoSizeVector(r, thumb);
        if (flags & 2) skipVideoSizeVector(r);
        if (!r.atEnd()) r.readInt32();

        if (!thumb.cachedBytes.empty()) {
            std::string cached = saveBytes(cacheDir + "/emoji_" + emojiId + "_cached", thumb.cachedBytes);
            if (!cached.empty()) {
                apiLog("[TgClient] Using cached thumb bytes: " + cached);
                return cached;
            }
        }

        auto download = [&](const std::string& requestedThumb) -> std::string {
            MTProto::TLWriter fw;
            fw.writeInt32(MTProto::TL::upload_getFile);
            fw.writeInt32(0);
            fw.writeInt32(MTProto::TL::inputDocumentFileLocation);
            fw.writeInt64(doc_id);
            fw.writeInt64(doc_access_hash);
            fw.writeBytes(doc_file_ref);
            fw.writeString(requestedThumb);
            fw.writeInt64(0);
            fw.writeInt32(1024 * 1024);

            auto fres = m_mtproto.invoke(fw.data(), 10000);
            if (!fres.ok) {
                apiLog("[TgClient] upload.getFile failed: " + std::to_string(fres.error_code) + " " + fres.error_message);
                return "";
            }

            MTProto::TLReader fr(fres.payload);
            int32_t ftype = fr.readInt32();
            if (ftype != MTProto::TL::upload_file) {
                char buf[96];
                std::snprintf(buf, sizeof(buf), "[TgClient] upload.getFile returned non-file result: 0x%08x", (unsigned)ftype);
                apiLog(buf);
                return "";
            }

            fr.readInt32();
            fr.readInt32();
            MTProto::Bytes b = fr.readBytes();
            apiLog("[TgClient] Downloaded " + std::to_string(b.size()) + " bytes" + (requestedThumb.empty() ? "" : " thumb=" + requestedThumb));
            return saveBytes(cacheDir + "/emoji_" + emojiId + (requestedThumb.empty() ? "" : "_thumb_" + requestedThumb), b, requestedThumb.empty() ? mime : "");
        };

        std::string path;
        if (!thumb.bestType.empty()) {
            apiLog("[TgClient] Downloading emoji thumbnail: " + thumb.bestType);
            path = download(thumb.bestType);
        }
        if (path.empty()) {
            apiLog("[TgClient] Downloading full emoji document");
            path = download("");
        }
        return path;
    };

    if (std::string mtprotoPath = fetchViaMtproto(); !mtprotoPath.empty()) {
        clearFailure(emojiId);
        return mtprotoPath;
    }

    if (std::string botPath = fetchCustomEmojiViaBotApi(emojiId); !botPath.empty()) {
        clearFailure(emojiId);
        return botPath;
    }

    rememberFailure(emojiId);
    return "";
}

std::string TgClient::fetchAvatar(const std::string& userId) {
    (void)userId;
    return "";
}

} // namespace Quote
