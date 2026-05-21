// this file is part of AlexaInc / QuotlyNative — Cairo Renderer
// developer hansaka@alexainc

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

void Renderer::measureLayout(PangoLayout* layout, int maxWidth, int& outWidth, int& outHeight) {
    pango_layout_set_width(layout, maxWidth * PANGO_SCALE);
    pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
    pango_layout_get_pixel_size(layout, &outWidth, &outHeight);
}

// ── drawAvatar ────────────────────────────────────────────────────────────────

void Renderer::drawAvatar(cairo_t* cr, double x, double y, double size, const std::string& name, int userId) {
    using namespace Style;
    double cx = x + size / 2.0, cy = y + size / 2.0, radius = size / 2.0;
    auto color = hexToRGBA(nameColor(userId));
    cairo_arc(cr, cx, cy, radius, 0, 2 * M_PI);
    cairo_set_source_rgba(cr, color.r, color.g, color.b, color.a);
    cairo_fill(cr);

    std::string initials;
    if (!name.empty()) {
        initials += toupper(name[0]);
        auto space = name.find(' ');
        if (space != std::string::npos && space + 1 < name.size()) initials += toupper(name[space + 1]);
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

// ── drawReply ────────────────────────────────────────────────────────────────

void Renderer::drawReply(cairo_t* cr, double x, double y, double width, const ReplyData& reply) {
    if (!reply.hasReply) return;
    using namespace Style;

    // Accent line
    auto color = hexToRGBA(nameColor(reply.senderId));
    cairo_set_source_rgba(cr, color.r, color.g, color.b, color.a);
    cairo_rectangle(cr, x, y, 2, 35);
    cairo_fill(cr);

    // Name
    PangoLayout* nl = pango_cairo_create_layout(cr);
    PangoFontDescription* nd = pango_font_description_from_string("Inter Bold 12");
    pango_layout_set_font_description(nl, nd);
    pango_layout_set_text(nl, reply.senderName.c_str(), -1);
    cairo_set_source_rgba(cr, color.r, color.g, color.b, color.a); // Reply name uses sender color
    cairo_move_to(cr, x + 8, y + 2);
    pango_cairo_show_layout(cr, nl);

    // Text
    PangoLayout* tl = pango_cairo_create_layout(cr);
    PangoFontDescription* td = pango_font_description_from_string("Inter 12");
    pango_layout_set_font_description(tl, td);
    pango_layout_set_width(tl, (width - 12) * PANGO_SCALE);
    pango_layout_set_ellipsize(tl, PANGO_ELLIPSIZE_END);
    pango_layout_set_text(tl, reply.text.c_str(), -1);
    cairo_set_source_rgba(cr, 0.8, 0.8, 0.8, 0.8);
    cairo_move_to(cr, x + 8, y + 18);
    pango_cairo_show_layout(cr, tl);

    pango_font_description_free(nd); pango_font_description_free(td);
    g_object_unref(nl); g_object_unref(tl);
}

// ── drawBubble ────────────────────────────────────────────────────────────────

void Renderer::drawBubble(cairo_t* cr, double x, double y, double width, double height, const RenderOptions& options) {
    if (!options.hasBubble) return;
    using namespace Style;
    auto getRadius = [](CornerRounding type) -> double {
        if (type == CornerRounding::Large) return kBubbleRadiusLarge;
        if (type == CornerRounding::Small) return kBubbleRadiusSmall;
        return 0;
    };
    double rtl = getRadius(options.rounding.topLeft), rtr = getRadius(options.rounding.topRight);
    double rbl = getRadius(options.rounding.bottomLeft), rbr = getRadius(options.rounding.bottomRight);
    bool tailLeft  = options.rounding.bottomLeft  == CornerRounding::Tail;
    bool tailRight = options.rounding.bottomRight == CornerRounding::Tail;
    if (tailLeft) rbl = 0; if (tailRight) rbr = 0;
    auto bg = hexToRGBA(options.isOut ? kColorOutBg : kColorInBg);
    cairo_new_path(cr);
    if (rtl > 0) cairo_arc(cr, x + rtl, y + rtl, rtl, M_PI, 3 * M_PI / 2); else cairo_move_to(cr, x, y);
    if (rtr > 0) cairo_arc(cr, x + width - rtr, y + rtr, rtr, 3 * M_PI / 2, 2 * M_PI); else cairo_line_to(cr, x + width, y);
    if (tailRight) {
        cairo_line_to(cr, x + width, y + height - kTailHeight);
        cairo_curve_to(cr, x + width, y + height - kTailHeight*0.4, x + width + kTailWidth*0.6, y + height - kTailHeight*0.1, x + width + kTailWidth, y + height);
        cairo_line_to(cr, x + width - kBubbleRadiusSmall, y + height);
    } else if (rbr > 0) cairo_arc(cr, x + width - rbr, y + height - rbr, rbr, 0, M_PI / 2); else cairo_line_to(cr, x + width, y + height);
    if (tailLeft) {
        cairo_line_to(cr, x + kBubbleRadiusSmall, y + height);
        cairo_line_to(cr, x - kTailWidth, y + height);
        cairo_curve_to(cr, x - kTailWidth*0.6, y + height - kTailHeight*0.1, x, y + height - kTailHeight*0.4, x, y + height - kTailHeight);
    } else if (rbl > 0) cairo_arc(cr, x + rbl, y + height - rbl, rbl, M_PI / 2, M_PI); else cairo_line_to(cr, x, y + height);
    cairo_close_path(cr);
    cairo_set_source_rgba(cr, bg.r, bg.g, bg.b, bg.a);
    cairo_fill(cr);
}

// ── renderQuote ───────────────────────────────────────────────────────────────

void Renderer::renderQuote(const std::string& outputFile, const std::vector<MessageData>& messages, const RenderOptions& options) {
    if (messages.empty()) return;
    using namespace Style;

    cairo_surface_t* measure_surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_t* measure_cr = cairo_create(measure_surf);
    int maxTextWidth = (int)(kMsgMaxWidth - kPadLeft - kPadRight);

    struct MsgSize { double h; double textH; double nameH; double replyH; double photoH; double bubbleW; };
    std::vector<MsgSize> sizes;
    double totalH = 0;
    double maxW = kMsgMinWidth;

    for (const auto& msg : messages) {
        PangoLayout* tl = pango_cairo_create_layout(measure_cr);
        PangoFontDescription* td = pango_font_description_from_string("Inter 14");
        pango_layout_set_font_description(tl, td);
        if (!msg.pangoMarkup.empty()) pango_layout_set_markup(tl, msg.pangoMarkup.c_str(), -1);
        else pango_layout_set_text(tl, msg.text.c_str(), -1);
        int tw, th; measureLayout(tl, maxTextWidth, tw, th);

        double nameH = 0, nameW = 0;
        if (!msg.senderName.empty()) {
            PangoLayout* nl = pango_cairo_create_layout(measure_cr);
            PangoFontDescription* nd = pango_font_description_from_string("Inter Bold 13");
            pango_layout_set_font_description(nl, nd);
            pango_layout_set_text(nl, msg.senderName.c_str(), -1);
            int nw, nh; pango_layout_get_pixel_size(nl, &nw, &nh);
            nameH = nh + kNamePadTop + kNamePadBottom; nameW = nw;
            g_object_unref(nl); pango_font_description_free(nd);
        }

        double replyH = msg.reply.hasReply ? 40 : 0;
        double photoH = msg.photoPath.empty() ? 0 : 200; // Simplified photo height

        double msgW = std::max({(double)tw, nameW, (double)(msg.reply.hasReply ? 150 : 0)}) + kPadLeft + kPadRight;
        msgW = std::clamp(msgW, kMsgMinWidth, kMsgMaxWidth);
        maxW = std::max(maxW, msgW);

        double msgH = kPadTop + nameH + replyH + th + photoH + kPadBottom;
        sizes.push_back({msgH, (double)th, nameH, replyH, photoH, msgW});
        totalH += msgH + 4;
        g_object_unref(tl); pango_font_description_free(td);
    }

    double canvasW = kCanvasPad + kAvatarSize + kAvatarMarginRight + maxW + kTailWidth + kCanvasPad;
    double canvasH = kCanvasPad + std::max(totalH, kAvatarSize) + kCanvasPad;
    cairo_destroy(measure_cr); cairo_surface_destroy(measure_surf);

    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, (int)ceil(canvasW), (int)ceil(canvasH));
    cairo_t* cr = cairo_create(surface);
    if (options.transparent) { cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE); cairo_set_source_rgba(cr, 0, 0, 0, 0); cairo_paint(cr); }
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    drawAvatar(cr, kCanvasPad, canvasH - kCanvasPad - kAvatarSize, kAvatarSize, messages[0].senderName, messages[0].senderId);

    double curY = kCanvasPad;
    double bubbleX = kCanvasPad + kAvatarSize + kAvatarMarginRight;

    for (size_t i = 0; i < messages.size(); ++i) {
        const auto& msg = messages[i];
        const auto& sz = sizes[i];
        RenderOptions mOpts = options;
        mOpts.isOut = msg.isOutgoing;
        mOpts.rounding.topLeft = (i == 0) ? CornerRounding::Large : CornerRounding::Small;
        mOpts.rounding.topRight = (i == 0) ? CornerRounding::Large : CornerRounding::Small;
        mOpts.rounding.bottomLeft = (i == messages.size() - 1) ? CornerRounding::Tail : CornerRounding::Small;
        mOpts.rounding.bottomRight = CornerRounding::Large;

        drawBubble(cr, bubbleX, curY, maxW, sz.h, mOpts);

        double py = curY + kPadTop;
        if (!msg.senderName.empty()) {
             auto color = hexToRGBA(nameColor(msg.senderId));
             cairo_set_source_rgba(cr, color.r, color.g, color.b, color.a);
             PangoLayout* nl = pango_cairo_create_layout(cr);
             PangoFontDescription* nd = pango_font_description_from_string("Inter Bold 13");
             pango_layout_set_font_description(nl, nd);
             pango_layout_set_text(nl, msg.senderName.c_str(), -1);
             cairo_move_to(cr, bubbleX + kPadLeft, py + kNamePadTop);
             pango_cairo_show_layout(cr, nl);
             py += sz.nameH; g_object_unref(nl); pango_font_description_free(nd);
        }

        if (msg.reply.hasReply) {
            drawReply(cr, bubbleX + kPadLeft, py, maxW - kPadLeft - kPadRight, msg.reply);
            py += sz.replyH;
        }

        if (!msg.photoPath.empty()) {
            // Draw photo placeholder (actual loading needs more robust logic)
            cairo_set_source_rgba(cr, 0.2, 0.2, 0.2, 1);
            cairo_rectangle(cr, bubbleX + kPadLeft, py, maxW - kPadLeft - kPadRight, sz.photoH - 8);
            cairo_fill(cr);
            py += sz.photoH;
        }

        PangoLayout* tl = pango_cairo_create_layout(cr);
        PangoFontDescription* td = pango_font_description_from_string("Inter 14");
        pango_layout_set_font_description(tl, td);
        pango_layout_set_width(tl, (maxW - kPadLeft - kPadRight) * PANGO_SCALE);
        if (!msg.pangoMarkup.empty()) pango_layout_set_markup(tl, msg.pangoMarkup.c_str(), -1);
        else pango_layout_set_text(tl, msg.text.c_str(), -1);
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_move_to(cr, bubbleX + kPadLeft, py);
        pango_cairo_show_layout(cr, tl);

        py += sz.textH; g_object_unref(tl); pango_font_description_free(td);
        curY += sz.h + 4;
    }

    cairo_surface_write_to_png(surface, outputFile.c_str());
    cairo_destroy(cr); cairo_surface_destroy(surface);
}

} // namespace Quote
