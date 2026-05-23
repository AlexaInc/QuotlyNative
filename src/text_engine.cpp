// this file is part of AlexaInc / QuotlyNative — Text Engine
// developer hansaka@alexainc

#include "text_engine.h"
#include <algorithm>
#include <cstdint>
#include <cctype>
#include <set>
#include <string>
#include <vector>

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

static std::string lowerAscii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return (char)std::tolower(c);
    });
    return s;
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
            cp = c;
            len = 1;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < text.size() &&
                   (static_cast<unsigned char>(text[i + 1]) & 0xC0) == 0x80) {
            cp = ((c & 0x1F) << 6) |
                 (static_cast<unsigned char>(text[i + 1]) & 0x3F);
            len = 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < text.size() &&
                   (static_cast<unsigned char>(text[i + 1]) & 0xC0) == 0x80 &&
                   (static_cast<unsigned char>(text[i + 2]) & 0xC0) == 0x80) {
            cp = ((c & 0x0F) << 12) |
                 ((static_cast<unsigned char>(text[i + 1]) & 0x3F) << 6) |
                 (static_cast<unsigned char>(text[i + 2]) & 0x3F);
            len = 3;
        } else if ((c & 0xF8) == 0xF0 && i + 3 < text.size() &&
                   (static_cast<unsigned char>(text[i + 1]) & 0xC0) == 0x80 &&
                   (static_cast<unsigned char>(text[i + 2]) & 0xC0) == 0x80 &&
                   (static_cast<unsigned char>(text[i + 3]) & 0xC0) == 0x80) {
            cp = ((c & 0x07) << 18) |
                 ((static_cast<unsigned char>(text[i + 1]) & 0x3F) << 12) |
                 ((static_cast<unsigned char>(text[i + 2]) & 0x3F) << 6) |
                 (static_cast<unsigned char>(text[i + 3]) & 0x3F);
            len = 4;
        } else {
            // Invalid UTF-8 byte: advance one byte so a bad payload can only
            // affect that byte/entity, never the whole message.
            cp = c;
            len = 1;
        }

        int cpUnits = (cp >= 0x10000) ? 2 : 1;
        if (units + cpUnits > utf16Offset) break; // offset inside surrogate pair
        units += cpUnits;
        i += len;
    }
    return static_cast<int>(i);
}

namespace {

enum StyleBits : uint32_t {
    StyleLink       = 1u << 0,
    StyleBold       = 1u << 1,
    StyleItalic     = 1u << 2,
    StyleUnderline  = 1u << 3,
    StyleStrike     = 1u << 4,
    StyleSpoiler    = 1u << 5,
    StyleCode       = 1u << 6,
    StyleBlockquote = 1u << 7,
};

struct StyleRange {
    int start = 0;
    int end = 0;
    uint32_t style = 0;
};

static uint32_t styleForTelegramEntity(const std::string& rawType) {
    std::string type = lowerAscii(rawType);

    // Some integrations pass MTProto-ish names instead of Bot API names.
    const std::string prefix = "messageentity";
    if (type.rfind(prefix, 0) == 0) type = type.substr(prefix.size());

    if (type == "bold") return StyleBold;
    if (type == "italic") return StyleItalic;
    if (type == "underline") return StyleUnderline;
    if (type == "strikethrough" || type == "strike" || type == "strikeout") return StyleStrike;
    if (type == "spoiler") return StyleSpoiler;
    if (type == "code" || type == "pre") return StyleCode;
    if (type == "blockquote" || type == "expandable_blockquote") return StyleBlockquote;

    // Telegram Desktop maps these to semantic clickable/link-like entity
    // types. We cannot attach click handlers in a PNG, but we can render the
    // same visual blue link color.
    if (type == "text_link" || type == "url" || type == "mention" ||
        type == "hashtag" || type == "cashtag" || type == "bot_command" ||
        type == "email" || type == "phone_number" || type == "phone" ||
        type == "text_mention" || type == "mention_name" || type == "bank_card") {
        return StyleLink;
    }

    // custom_emoji is intentionally not markup. The renderer handles it with
    // pango_attr_shape_new so it participates in layout as a real glyph.
    return 0;
}

static void openStyle(std::string& out, uint32_t bit) {
    switch (bit) {
        case StyleBlockquote: out += "<span foreground='#A8C4DB'>"; break;
        case StyleSpoiler:    out += "<span foreground='#888888'>"; break;
        case StyleLink:       out += "<span foreground='#62bcf9'>"; break;
        case StyleBold:       out += "<b>"; break;
        case StyleItalic:     out += "<i>"; break;
        case StyleUnderline:  out += "<u>"; break;
        case StyleStrike:     out += "<s>"; break;
        case StyleCode:       out += "<tt>"; break;
        default: break;
    }
}

static void closeStyle(std::string& out, uint32_t bit) {
    switch (bit) {
        case StyleBlockquote:
        case StyleSpoiler:
        case StyleLink:       out += "</span>"; break;
        case StyleBold:       out += "</b>"; break;
        case StyleItalic:     out += "</i>"; break;
        case StyleUnderline:  out += "</u>"; break;
        case StyleStrike:     out += "</s>"; break;
        case StyleCode:       out += "</tt>"; break;
        default: break;
    }
}

static const uint32_t kStyleOrder[] = {
    StyleBlockquote,
    StyleSpoiler,
    StyleLink,
    StyleBold,
    StyleItalic,
    StyleUnderline,
    StyleStrike,
    StyleCode,
};

static std::string applyStylesToSegment(const std::string& segment, uint32_t activeStyles) {
    if (segment.empty()) return "";

    std::string out;
    for (uint32_t bit : kStyleOrder) {
        if (activeStyles & bit) openStyle(out, bit);
    }

    out += xmlEscape(segment);

    for (auto it = std::rbegin(kStyleOrder); it != std::rend(kStyleOrder); ++it) {
        if (activeStyles & *it) closeStyle(out, *it);
    }
    return out;
}

} // namespace

std::string TextEngine::processEntities(const std::string& text,
                                         const nlohmann::json& entities) {
    if (text.empty()) return "";

    std::vector<StyleRange> ranges;
    std::vector<int> boundaries;
    boundaries.reserve(entities.is_array() ? entities.size() * 2 + 2 : 2);
    boundaries.push_back(0);
    boundaries.push_back((int)text.size());

    if (entities.is_array() && !entities.empty()) {
        for (const auto& e : entities) {
            if (!e.is_object()) continue;

            int off16 = e.value("offset", 0);
            int len16 = e.value("length", 0);
            if (off16 < 0 || len16 <= 0) continue;

            const std::string type = e.value("type", "");
            const uint32_t style = styleForTelegramEntity(type);
            if (style == 0) continue; // unsupported/custom emoji: skip this entity only

            int start = utf16OffsetToUtf8ByteOffset(text, off16);
            int end = utf16OffsetToUtf8ByteOffset(text, off16 + len16);
            start = std::clamp(start, 0, (int)text.size());
            end = std::clamp(end, start, (int)text.size());

            // Bad/overflowing entity positions should not poison the whole
            // markup string. We simply ignore that entity and keep rendering
            // the rest of the message.
            if (end <= start) continue;

            ranges.push_back({start, end, style});
            boundaries.push_back(start);
            boundaries.push_back(end);
        }
    }

    std::sort(boundaries.begin(), boundaries.end());
    boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());

    // Build markup by splitting text at every entity boundary and opening a
    // fresh, correctly nested tag stack for each segment. This is deliberately
    // more robust than inserting raw open/close tags at offsets: Telegram can
    // send partially-overlapping entities, and raw insertion creates invalid
    // XML like <b>..<i>..</b>..</i>, causing Pango to reject the whole string.
    std::string result;
    result.reserve(text.size() + ranges.size() * 32);

    for (size_t i = 0; i + 1 < boundaries.size(); ++i) {
        const int a = boundaries[i];
        const int b = boundaries[i + 1];
        if (b <= a) continue;

        uint32_t active = 0;
        for (const auto& r : ranges) {
            if (r.start <= a && b <= r.end) active |= r.style;
        }

        result += applyStylesToSegment(text.substr(a, b - a), active);
    }

    return result;
}

bool TextEngine::isOnlyEmoji(const std::string& text) {
    return false;
}

} // namespace Quote
