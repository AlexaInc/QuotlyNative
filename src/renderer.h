// this file is part of AlexaInc / QuotlyNative — Cairo Renderer
// developer hansaka@alexainc

#pragma once
#include <string>
#include <vector>
#include <map>
#include <cairo.h>
#include <pango/pangocairo.h>
#include <nlohmann/json.hpp>

namespace Quote {

enum class CornerRounding {
    None,
    Small,
    Large,
    Tail
};

struct BubbleRounding {
    CornerRounding topLeft    = CornerRounding::Large;
    CornerRounding topRight   = CornerRounding::Large;
    CornerRounding bottomLeft = CornerRounding::Tail;
    CornerRounding bottomRight = CornerRounding::Large;
};

struct ReplyData {
    std::string text;
    std::string senderName;
    int senderId = 0;
    bool hasReply = false;
};

enum class MediaType { None, Photo, Sticker };

struct CustomEmoji {
    int offset;
    int length;
    uint64_t documentId;
};

struct MessageData {
    std::string text;                  // raw text
    std::string pangoMarkup;           // processed Pango markup from TextEngine
    std::string senderName;            // "First Last"
    int         senderId    = 0;       // used to pick name color
    std::string senderKey;             // unique identifier for grouping
    bool        isOutgoing  = false;
    ReplyData   reply;
    std::string photoPath;             // path to downloaded photo (if any)
    MediaType   mediaType   = MediaType::None;
    uint64_t    emojiStatusId = 0;     // custom emoji status ID
    std::vector<CustomEmoji> customEmojis;
};

struct RenderOptions {
    bool transparent     = true;
    std::string backgroundColor = "#00000000";
    bool isOut           = false;
    bool hasBubble       = true;
    BubbleRounding rounding;
};

class Renderer {
public:
    // Render a group of quote messages to PNG file
    static void renderQuote(
        const std::string& outputFile,
        const std::vector<MessageData>& messages,
        const RenderOptions& options,
        const std::map<uint64_t, std::string>& emojiMap = {});

private:
    static void drawBubble(cairo_t* cr, double x, double y,
                            double width, double height,
                            const RenderOptions& options);

    static void drawAvatar(cairo_t* cr, double x, double y,
                            double size, const std::string& name,
                            int userId);

    static void drawReply(cairo_t* cr, double x, double y,
                           double width, const ReplyData& reply);

    static void measureLayout(PangoLayout* layout, int maxWidth,
                               int& outWidth, int& outHeight);
};

} // namespace Quote
