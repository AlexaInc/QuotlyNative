// this file is part of AlexaInc / QuotlyNative — Cairo Renderer
// developer hansaka@alexainc

#pragma once
#include <string>
#include <vector>
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

struct MessageData {
    std::string text;                  // raw text
    std::string pangoMarkup;           // processed Pango markup from TextEngine
    std::string senderName;            // "First Last"
    int         senderId    = 0;       // used to pick name color
    bool        isOutgoing  = false;
};

struct RenderOptions {
    bool transparent     = true;
    std::string backgroundColor = "#00000000";
    bool hasBubble       = true;
    BubbleRounding rounding;
};

class Renderer {
public:
    // Render a single quote message to PNG file
    static void renderQuote(
        const std::string& outputFile,
        const MessageData& msg,
        const RenderOptions& options);

private:
    // Draw a rounded-rect bubble with optional tail
    static void drawBubble(cairo_t* cr, double x, double y,
                            double width, double height,
                            const RenderOptions& options);

    // Draw a circular avatar placeholder with initials
    static void drawAvatar(cairo_t* cr, double x, double y,
                            double size, const std::string& name,
                            int userId);

    // Measure Pango layout and return width, height
    static void measureLayout(PangoLayout* layout, int maxWidth,
                               int& outWidth, int& outHeight);
};

} // namespace Quote
