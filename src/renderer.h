// this file is part of AlexaInc / QuotlyNative — Cairo Renderer
// developer hansaka@alexainc

#pragma once
#include <string>
#include <vector>
#include <cairo.h>
#include <pango/pangocairo.h>

namespace Quote {

enum class CornerRounding {
    None,
    Small,
    Large,
    Tail
};

struct BubbleRounding {
    CornerRounding topLeft;
    CornerRounding topRight;
    CornerRounding bottomLeft;
    CornerRounding bottomRight;
};

struct RenderOptions {
    bool transparent = true;
    std::string backgroundColor = "#00000000";
    bool isOut = true;
    bool hasBubble = true;
    BubbleRounding rounding;
};

class Renderer {
public:
    static void renderQuote(const std::string& outputFile, const std::string& text, const RenderOptions& options);
    
private:
    static void drawBubble(cairo_t* cr, double x, double y, double width, double height, const RenderOptions& options);
};

} // namespace Quote
