// this file is part of AlexaInc / QuotlyNative — Text Engine
// developer hansaka@alexainc

#include "text_engine.h"
#include <algorithm>
#include <cstdint>

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
            case '"': out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default:   out += c;        break;
        }
    }
    return out;
}

// Telegram message entity offsets are UTF-16 code units, while std::string and
// Pango layout indices are UTF-8 byte offsets. Convert and clamp safely.
static int utf16OffsetToUtf8ByteOffset(const std::string& text, int utf16Offset) {
    if (utf16Offset <= 0) return 0;

    int units = 0;
    size_t i = 0;
    while (i < text.size() && units < utf16Offset) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        uint32_t cp = 0;
        size_t len = 1;

        if ((c & 0x80) == 0) {
            cp = c; len = 1;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < text.size()) {
            cp = ((c & 0x1F) << 6) | (static_cast<unsigned char>(text[i + 1]) & 0x3F);
            len = 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < text.size()) {
            cp = ((c & 0x0F) << 12) |
                 ((static_cast<unsigned char>(text[i + 1]) & 0x3F) << 6) |
                 (static_cast<unsigned char>(text[i + 2]) & 0x3F);
            len = 3;
        } else if ((c & 0xF8) == 0xF0 && i + 3 < text.size()) {
            cp = ((c & 0x07) << 18) |
                 ((static_cast<unsigned char>(text[i + 1]) & 0x3F) << 12) |
                 ((static_cast<unsigned char>(text[i + 2]) & 0x3F) << 6) |
                 (static_cast<unsigned char>(text[i + 3]) & 0x3F);
            len = 4;
        } else {
            cp = c; len = 1;
        }

        int cpUnits = (cp >= 0x10000) ? 2 : 1;
        if (units + cpUnits > utf16Offset) break; // offset inside surrogate pair
        units += cpUnits;
        i += len;
    }
    return static_cast<int>(i);
}

std::string TextEngine::processEntities(const std::string& text,
                                         const nlohmann::json& entities) {
    struct Tag { int pos; int priority; std::string markup; };
    std::vector<Tag> tags;

    if (entities.is_array() && !entities.empty()) {
        for (const auto& e : entities) {
            int off16 = e.value("offset", 0);
            int len16 = e.value("length", 0);
            if (len16 <= 0) continue;
            std::string type = e.value("type", "");

            int off = utf16OffsetToUtf8ByteOffset(text, off16);
            int end = utf16OffsetToUtf8ByteOffset(text, off16 + len16);
            off = std::clamp(off, 0, (int)text.size());
            end = std::clamp(end, off, (int)text.size());
            if (end <= off) continue;

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
                // Hide the one-character placeholder supplied by Telegram. The
                // renderer overlays the downloaded custom-emoji image at this
                // exact Pango index using MessageData::customEmojis.
                open = "<span alpha='1' foreground='#FFFFFF'>";
                close = "</span>";
            }
            else { continue; }

            // Close tags before opening tags at the same byte to keep markup valid.
            tags.push_back({off, 1, open});
            tags.push_back({end, 0, close});
        }
    }

    std::stable_sort(tags.begin(), tags.end(), [](const Tag& a, const Tag& b){
        if (a.pos != b.pos) return a.pos < b.pos;
        return a.priority < b.priority;
    });

    std::string result;
    int cursor = 0;
    size_t tagIdx = 0;

    while (cursor < (int)text.size() || tagIdx < tags.size()) {
        while (tagIdx < tags.size() && tags[tagIdx].pos == cursor)
            result += tags[tagIdx++].markup;
        if (cursor >= (int)text.size()) break;
        int nextTagPos = tagIdx < tags.size() ? tags[tagIdx].pos : (int)text.size();
        nextTagPos = std::clamp(nextTagPos, cursor, (int)text.size());
        result += xmlEscape(text.substr(cursor, nextTagPos - cursor));
        cursor = nextTagPos;
    }
    return result;
}

bool TextEngine::isOnlyEmoji(const std::string& text) {
    return false;
}

} // namespace Quote
