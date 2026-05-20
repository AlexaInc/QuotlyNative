#include "renderer.h"
#include "style_constants.h"
#include <cmath>
#include <iostream>

namespace Quote {

struct Color {
    double r, g, b, a;
};

Color hexToRGBA(const std::string& hex) {
    if (hex.length() < 7) return {0, 0, 0, 1};
    int r, g, b;
    sscanf(hex.c_str() + 1, "%02x%02x%02x", &r, &g, &b);
    return {r / 255.0, g / 255.0, b / 255.0, 1.0};
}

void Renderer::drawBubble(cairo_t* cr, double x, double y, double width, double height, const RenderOptions& options) {
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
    
    Color bgObj = hexToRGBA(options.isOut ? kColorOutBg : kColorInBg);
    
    cairo_new_path(cr);
    
    // Top Left
    if (rtl > 0) cairo_arc(cr, x + rtl, y + rtl, rtl, M_PI, 3 * M_PI / 2);
    else cairo_move_to(cr, x, y);
    
    // Top Right
    if (rtr > 0) cairo_arc(cr, x + width - rtr, y + rtr, rtr, 3 * M_PI / 2, 2 * M_PI);
    else cairo_line_to(cr, x + width, y);
    
    // Bottom Right
    if (options.rounding.bottomRight == CornerRounding::Tail) {
        cairo_line_to(cr, x + width, y + height - 10);
        cairo_line_to(cr, x + width + 8, y + height);
        cairo_line_to(cr, x + width - 10, y + height);
    } else if (rbr > 0) {
        cairo_arc(cr, x + width - rbr, y + height - rbr, rbr, 0, M_PI / 2);
    } else {
        cairo_line_to(cr, x + width, y + height);
    }
    
    // Bottom Left
    if (options.rounding.bottomLeft == CornerRounding::Tail) {
        cairo_line_to(cr, x + 10, y + height);
        cairo_line_to(cr, x - 8, y + height);
        cairo_line_to(cr, x, y + height - 10);
    } else if (rbl > 0) {
        cairo_arc(cr, x + rbl, y + height - rbl, rbl, M_PI / 2, M_PI);
    } else {
        cairo_line_to(cr, x, y + height);
    }
    
    cairo_close_path(cr);
    
    cairo_set_source_rgba(cr, bgObj.r, bgObj.g, bgObj.b, bgObj.a);
    cairo_fill_preserve(cr);
    
    // Shadow
    cairo_set_source_rgba(cr, 0, 0, 0, 0.2);
    cairo_set_line_width(cr, 0.5);
    cairo_stroke(cr);
}

void Renderer::renderQuote(const std::string& outputFile, const std::string& text, const RenderOptions& options) {
    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 800, 400);
    cairo_t* cr = cairo_create(surface);
    
    if (options.transparent) {
        cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
        cairo_set_source_rgba(cr, 0, 0, 0, 0);
        cairo_paint(cr);
    }
    
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    
    // Draw bubble
    drawBubble(cr, 50, 50, 400, 100, options);
    
    // Text rendering setup with Pango
    PangoLayout* layout = pango_cairo_create_layout(cr);
    PangoFontDescription* desc = pango_font_description_from_string("Open Sans 14");
    pango_layout_set_font_description(layout, desc);
    pango_layout_set_text(layout, text.c_str(), -1);
    
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_move_to(cr, 50 + 12, 50 + 8);
    pango_cairo_show_layout(cr, layout);
    
    g_object_unref(layout);
    pango_font_description_free(desc);
    
    cairo_surface_write_to_png(surface, outputFile.c_str());
    
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
}

} // namespace Quote
