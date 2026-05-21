// this file is part of AlexaInc / QuotlyNative — Cairo Renderer
// developer hansaka@alexainc
// Bubble geometry derived from tdesktop/Telegram/SourceFiles/ui/chat/message_bubble.cpp

#include "renderer.h"
#include "style_constants.h"
#include <cmath>
#include <iostream>
#include <algorithm>

namespace Quote {

// ── Helpers ───────────────────────────────────────────────────────────────────

struct RGBA { double r, g, b, a; };

static RGBA hexToRGBA(const std::string& hex) {
    if (hex.size() < 7) return {0, 0, 0, 1};
    int r, g, b;
    sscanf(hex.c_str() + 1, "%02x%02x%02x", &r, &g, &b);
    double a = 1.0;
    if (hex.size() >= 9) {
        int ai;
        sscanf(hex.c_str() + 7, "%02x", &ai);
        a = ai / 255.0;
    }
    return {r / 255.0, g / 255.0, b / 255.0, a};
}

static const std::string& nameColor(int userId) {
    using namespace Style;
    return kNameColors[std::abs(userId) % kNameColors.size()];
}

// ── measureLayout ─────────────────────────────────────────────────────────────

void Renderer::measureLayout(PangoLayout* layout, int maxWidth,
                              int& outWidth, int& outHeight) {
    pango_layout_set_width(layout, maxWidth * PANGO_SCALE);
    pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
    pango_layout_get_pixel_size(layout, &outWidth, &outHeight);
}

// ── drawAvatar ────────────────────────────────────────────────────────────────
// Draws a colored circle with 1-2 letter initials, color derived from userId

void Renderer::drawAvatar(cairo_t* cr, double x, double y, double size,
                           const std::string& name, int userId) {
    using namespace Style;

    // Circle
    double cx = x + size / 2.0;
    double cy = y + size / 2.0;
    double radius = size / 2.0;

    auto color = hexToRGBA(nameColor(userId));
    cairo_arc(cr, cx, cy, radius, 0, 2 * M_PI);
    cairo_set_source_rgba(cr, color.r, color.g, color.b, color.a);
    cairo_fill(cr);

    // Initials (first letter of first and last name)
    std::string initials;
    if (!name.empty()) {
        initials += toupper(name[0]);
        auto space = name.find(' ');
        if (space != std::string::npos && space + 1 < name.size())
            initials += toupper(name[space + 1]);
    }

    if (!initials.empty()) {
        PangoLayout* layout = pango_cairo_create_layout(cr);
        PangoFontDescription* desc = pango_font_description_from_string("Inter 15");
        pango_font_description_set_weight(desc, PANGO_WEIGHT_MEDIUM);
        pango_layout_set_font_description(layout, desc);
        pango_layout_set_text(layout, initials.c_str(), -1);

        int tw, th;
        pango_layout_get_pixel_size(layout, &tw, &th);
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_move_to(cr, cx - tw / 2.0, cy - th / 2.0);
        pango_cairo_show_layout(cr, layout);

        pango_font_description_free(desc);
        g_object_unref(layout);
    }
}

// ── drawBubble ────────────────────────────────────────────────────────────────
// Rounded rect with optional tail, matching tdesktop PaintBubbleGeneric logic

void Renderer::drawBubble(cairo_t* cr, double x, double y,
                           double width, double height,
                           const RenderOptions& options) {
    if (!options.hasBubble) return;
    using namespace Style;

    auto getRadius = [](CornerRounding type) -> double {
        if (type == CornerRounding::Large) return kBubbleRadiusLarge;
        if (type == CornerRounding::Small) return kBubbleRadiusSmall;
        return 0;
    };

    double rtl = getRadius(options.rounding.topLeft);
    double rtr = getRadius(options.rounding.topRight);
    double rbl = getRadius(options.rounding.bottomLeft);
    double rbr = getRadius(options.rounding.bottomRight);

    bool tailLeft  = options.rounding.bottomLeft  == CornerRounding::Tail;
    bool tailRight = options.rounding.bottomRight == CornerRounding::Tail;

    if (tailLeft)  rbl = 0;
    if (tailRight) rbr = 0;

    auto bg = hexToRGBA(options.isOut ? kColorOutBg : kColorInBg);

    cairo_new_path(cr);

    // Top-left
    if (rtl > 0) cairo_arc(cr, x + rtl, y + rtl, rtl, M_PI, 3 * M_PI / 2);
    else cairo_move_to(cr, x, y);

    // Top-right
    if (rtr > 0) cairo_arc(cr, x + width - rtr, y + rtr, rtr, 3 * M_PI / 2, 2 * M_PI);
    else cairo_line_to(cr, x + width, y);

    // Bottom-right
    if (tailRight) {
        cairo_line_to(cr, x + width, y + height - kTailHeight);
        // Bezier tail curve
        cairo_curve_to(cr,
            x + width, y + height - kTailHeight * 0.4,
            x + width + kTailWidth * 0.6, y + height - kTailHeight * 0.1,
            x + width + kTailWidth, y + height);
        cairo_line_to(cr, x + width - kBubbleRadiusSmall, y + height);
    } else if (rbr > 0) {
        cairo_arc(cr, x + width - rbr, y + height - rbr, rbr, 0, M_PI / 2);
    } else {
        cairo_line_to(cr, x + width, y + height);
    }

    // Bottom-left
    if (tailLeft) {
        cairo_line_to(cr, x + kBubbleRadiusSmall, y + height);
        cairo_line_to(cr, x - kTailWidth, y + height);
        cairo_curve_to(cr,
            x - kTailWidth * 0.6, y + height - kTailHeight * 0.1,
            x, y + height - kTailHeight * 0.4,
            x, y + height - kTailHeight);
    } else if (rbl > 0) {
        cairo_arc(cr, x + rbl, y + height - rbl, rbl, M_PI / 2, M_PI);
    } else {
        cairo_line_to(cr, x, y + height);
    }

    cairo_close_path(cr);

    // Fill background
    cairo_set_source_rgba(cr, bg.r, bg.g, bg.b, bg.a);
    cairo_fill(cr);
}

// ═════════════════════════════════════════════════════════════════════════════
// renderQuote — full pipeline: measure text → calculate bubble → draw all
// ═════════════════════════════════════════════════════════════════════════════

void Renderer::renderQuote(
    const std::string& outputFile,
    const MessageData& msg,
    const RenderOptions& options)
{
    using namespace Style;

    // ── Step 1: Create a scratch surface to measure text ─────────────────────
    cairo_surface_t* measure_surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_t* measure_cr = cairo_create(measure_surf);

    // Body text layout
    PangoLayout* textLayout = pango_cairo_create_layout(measure_cr);
    PangoFontDescription* textDesc = pango_font_description_from_string("Inter");
    pango_font_description_set_size(textDesc, (int)(kTextFontSize * PANGO_SCALE));
    pango_layout_set_font_description(textLayout, textDesc);

    // Max text width = bubble content area
    int maxTextWidth = (int)(kMsgMaxWidth - kPadLeft - kPadRight);

    if (!msg.pangoMarkup.empty()) {
        pango_layout_set_markup(textLayout, msg.pangoMarkup.c_str(), -1);
    } else {
        pango_layout_set_text(textLayout, msg.text.c_str(), -1);
    }

    int textW, textH;
    measureLayout(textLayout, maxTextWidth, textW, textH);

    // Name layout
    PangoLayout* nameLayout = pango_cairo_create_layout(measure_cr);
    PangoFontDescription* nameDesc = pango_font_description_from_string("Inter");
    pango_font_description_set_size(nameDesc, (int)(kNameFontSize * PANGO_SCALE));
    pango_font_description_set_weight(nameDesc, PANGO_WEIGHT_SEMIBOLD);
    pango_layout_set_font_description(nameLayout, nameDesc);

    int nameW = 0, nameH = 0;
    bool hasName = !msg.senderName.empty();
    if (hasName) {
        pango_layout_set_text(nameLayout, msg.senderName.c_str(), -1);
        pango_layout_get_pixel_size(nameLayout, &nameW, &nameH);
    }

    // ── Step 2: Calculate bubble geometry ────────────────────────────────────
    double contentW = std::max((double)textW, (double)nameW);
    double bubbleW = contentW + kPadLeft + kPadRight;
    bubbleW = std::max(bubbleW, kMsgMinWidth);
    bubbleW = std::min(bubbleW, kMsgMaxWidth);

    double bubbleH = kPadTop;
    if (hasName) bubbleH += kNamePadTop + nameH + kNamePadBottom;
    bubbleH += textH;
    bubbleH += kPadBottom;

    // ── Step 3: Calculate canvas size ────────────────────────────────────────
    double avatarArea = kAvatarSize + kAvatarMarginRight;
    double tailExtra  = kTailWidth;

    double canvasW = kCanvasPad + avatarArea + bubbleW + tailExtra + kCanvasPad;
    double canvasH = kCanvasPad + std::max(bubbleH, kAvatarSize) + kCanvasPad;

    // Destroy measurement surface
    g_object_unref(textLayout);
    g_object_unref(nameLayout);
    pango_font_description_free(textDesc);
    pango_font_description_free(nameDesc);
    cairo_destroy(measure_cr);
    cairo_surface_destroy(measure_surf);

    // ── Step 4: Create final surface ─────────────────────────────────────────
    int surfW = (int)std::ceil(canvasW);
    int surfH = (int)std::ceil(canvasH);

    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, surfW, surfH);
    cairo_t* cr = cairo_create(surface);

    // Transparent background
    if (options.transparent) {
        cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
        cairo_set_source_rgba(cr, 0, 0, 0, 0);
        cairo_paint(cr);
    }
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    // Positions
    double avatarX = kCanvasPad;
    double avatarY = kCanvasPad + std::max(0.0, bubbleH - kAvatarSize); // bottom-aligned
    double bubbleX = kCanvasPad + avatarArea;
    double bubbleY = kCanvasPad;

    // ── Step 5: Draw avatar ──────────────────────────────────────────────────
    drawAvatar(cr, avatarX, avatarY, kAvatarSize, msg.senderName, msg.senderId);

    // ── Step 6: Draw bubble ──────────────────────────────────────────────────
    RenderOptions bubbleOpts = options;
    bubbleOpts.isOut = msg.isOutgoing;
    drawBubble(cr, bubbleX, bubbleY, bubbleW, bubbleH, bubbleOpts);

    // ── Step 7: Draw sender name ─────────────────────────────────────────────
    double cursorY = bubbleY + kPadTop;

    if (hasName) {
        cursorY += kNamePadTop;

        PangoLayout* nl = pango_cairo_create_layout(cr);
        PangoFontDescription* nd = pango_font_description_from_string("Inter");
        pango_font_description_set_size(nd, (int)(kNameFontSize * PANGO_SCALE));
        pango_font_description_set_weight(nd, PANGO_WEIGHT_SEMIBOLD);
        pango_layout_set_font_description(nl, nd);
        pango_layout_set_text(nl, msg.senderName.c_str(), -1);

        auto nc = hexToRGBA(nameColor(msg.senderId));
        cairo_set_source_rgba(cr, nc.r, nc.g, nc.b, nc.a);
        cairo_move_to(cr, bubbleX + kPadLeft, cursorY);
        pango_cairo_show_layout(cr, nl);

        int nw, nh;
        pango_layout_get_pixel_size(nl, &nw, &nh);
        cursorY += nh + kNamePadBottom;

        pango_font_description_free(nd);
        g_object_unref(nl);
    }

    // ── Step 8: Draw message text ────────────────────────────────────────────
    {
        PangoLayout* tl = pango_cairo_create_layout(cr);
        PangoFontDescription* td = pango_font_description_from_string("Inter");
        pango_font_description_set_size(td, (int)(kTextFontSize * PANGO_SCALE));
        pango_layout_set_font_description(tl, td);
        pango_layout_set_width(tl, (int)(bubbleW - kPadLeft - kPadRight) * PANGO_SCALE);
        pango_layout_set_wrap(tl, PANGO_WRAP_WORD_CHAR);

        if (!msg.pangoMarkup.empty()) {
            pango_layout_set_markup(tl, msg.pangoMarkup.c_str(), -1);
        } else {
            pango_layout_set_text(tl, msg.text.c_str(), -1);
        }

        auto tc = hexToRGBA(msg.isOutgoing ? kColorOutText : kColorInText);
        cairo_set_source_rgba(cr, tc.r, tc.g, tc.b, tc.a);
        cairo_move_to(cr, bubbleX + kPadLeft, cursorY);
        pango_cairo_show_layout(cr, tl);

        pango_font_description_free(td);
        g_object_unref(tl);
    }

    // ── Step 9: Write PNG ────────────────────────────────────────────────────
    cairo_surface_write_to_png(surface, outputFile.c_str());
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
}

} // namespace Quote
