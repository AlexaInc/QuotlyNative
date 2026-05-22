// this file is part of AlexaInc / QuotlyNative — Text Engine
// developer hansaka@alexainc

#include "text_engine.h"
#include <algorithm>

namespace Quote {

// ── XML / Pango escape ────────────────────────────────────────────────────────
static std::string xmlEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        switch (c) {
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default:   out += c;        break;
        }
    }
    return out;
}

std::string TextEngine::processEntities(const std::string& text,
                                         const nlohmann::json& entities) {
    struct Tag { int pos; std::string markup; };
    std::vector<Tag> tags;

    if (!entities.empty()) {
        auto sorted = entities.get<std::vector<nlohmann::json>>();
        std::sort(sorted.begin(), sorted.end(), [](const nlohmann::json& a, const nlohmann::json& b){
            return a["offset"].get<int>() < b["offset"].get<int>();
        });

        for (const auto& e : sorted) {
            int off = e.value("offset", 0);
            int len = e.value("length", 0);
            if (len <= 0) continue;
            std::string type = e.value("type", "");

            std::string open, close;
            if      (type == "bold")          { open = "<b>";          close = "</b>"; }
            else if (type == "italic")        { open = "<i>";          close = "</i>"; }
            else if (type == "underline")     { open = "<u>";          close = "</u>"; }
            else if (type == "strikethrough") { open = "<s>";          close = "</s>"; }
            else if (type == "code")          { open = "<tt>";         close = "</tt>"; }
            else if (type == "pre")           { open = "<tt>";         close = "</tt>"; }
            else if (type == "spoiler")       { open = "<span foreground='#888888'>"; close = "</span>"; }
            else if (type == "text_link" || type == "url" || type == "mention") {
                open  = "<span foreground='#62bcf9'>";
                close = "</span>";
            }
            else if (type == "custom_emoji") {
                uint64_t eid = e.value("custom_emoji_id", 0ULL);
                // We use alpha='0' to hide the placeholder and face='EmojiPlaceholder' for detection
                open = "<span face='EmojiPlaceholder' alpha='0'>"; 
                close = "</span>";
            }
            else { continue; }

            tags.push_back({off,       open});
            tags.push_back({off + len, close});
        }
    }

    std::stable_sort(tags.begin(), tags.end(), [](const Tag& a, const Tag& b){
        return a.pos < b.pos;
    });

    std::string result;
    int cursor = 0;
    size_t tagIdx = 0;

    while (cursor < (int)text.size() || tagIdx < tags.size()) {
        while (tagIdx < tags.size() && tags[tagIdx].pos == cursor)
            result += tags[tagIdx++].markup;
        if (cursor >= (int)text.size()) break;
        int nextTagPos = tagIdx < tags.size() ? tags[tagIdx].pos : (int)text.size();
        result += xmlEscape(text.substr(cursor, nextTagPos - cursor));
        cursor = nextTagPos;
    }
    return result;
}

bool TextEngine::isOnlyEmoji(const std::string& text) {
    return false;
}

} // namespace Quote
