// this file is part of AlexaInc / QuotlyNative — Cairo Renderer
// developer hansaka@alexainc

#include "renderer.h"
#include "style_constants.h"
#include <cmath>
#include <iostream>
#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <sys/stat.h>

namespace Quote {
static void drawEmojiSurface(cairo_t* cr, double x, double y, double size, const std::string& path);

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

struct ImageSize { int w=0, h=0; };

static bool fileExists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && st.st_size > 0;
}

static bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
}

static std::string prepareDrawablePath(const std::string& path) {
    if (path.empty()) return "";
    if (endsWith(path, ".png")) return path;

    std::string pngPath = path + ".png";
    if (fileExists(pngPath)) return pngPath;

    std::string cmd;
    if (endsWith(path, ".webp")) {
        cmd = "dwebp \"" + path + "\" -o \"" + pngPath + "\" >/dev/null 2>&1";
    } else if (endsWith(path, ".jpg") || endsWith(path, ".jpeg") || endsWith(path, ".gif")) {
        cmd = "(magick \"" + path + "\" \"" + pngPath + "\" || convert \"" + path + "\" \"" + pngPath + "\") >/dev/null 2>&1";
    } else if (endsWith(path, ".webm") || endsWith(path, ".mp4") || endsWith(path, ".tgs")) {
        cmd = "ffmpeg -y -i \"" + path + "\" -frames:v 1 \"" + pngPath + "\" >/dev/null 2>&1";
    }

    if (!cmd.empty()) system(cmd.c_str());
    return fileExists(pngPath) ? pngPath : path;
}

static ImageSize getImageSize(const std::string& path) {
    std::string drawable = prepareDrawablePath(path);
    FILE* f = fopen(drawable.c_str(), "rb");
    if (!f) return {0,0};
    uint8_t buf[32];
    if (fread(buf, 1, 32, f) < 8) { fclose(f); return {0,0}; }
    ImageSize res = {0,0};
    if (buf[0]==0x89 && buf[1]=='P' && buf[2]=='N' && buf[3]=='G') {
        fseek(f, 16, SEEK_SET);
        uint8_t d[8]; fread(d, 1, 8, f);
        res.w = (d[0]<<24)|(d[1]<<16)|(d[2]<<8)|d[3];
        res.h = (d[4]<<24)|(d[5]<<16)|(d[6]<<8)|d[7];
    } else if (buf[0]==0xFF && buf[1]==0xD8) {
        fseek(f, 2, SEEK_SET);
        while (true) {
            uint8_t marker[2]; if (fread(marker, 1, 2, f) < 2) break;
            if (marker[0] != 0xFF) break;
            if (marker[1] >= 0xC0 && marker[1] <= 0xC3) {
                fseek(f, 3, SEEK_CUR);
                uint8_t d[4]; fread(d, 1, 4, f);
                res.h = (d[0]<<8)|d[1];
                res.w = (d[2]<<8)|d[3];
                break;
            } else {
                uint8_t l[2]; fread(l, 1, 2, f);
                int len = (l[0]<<8)|l[1];
                fseek(f, len - 2, SEEK_CUR);
            }
        }
    }
    fclose(f);
    return res;
}

static void fitMediaIntoBounds(const ImageSize& isz, double maxW, double maxH,
                               double minW, double fallbackW, double fallbackH,
                               double& outW, double& outH) {
    if (isz.w <= 0 || isz.h <= 0) {
        outW = fallbackW;
        outH = fallbackH;
        return;
    }

    double scale = std::min(maxW / isz.w, maxH / isz.h);
    outW = std::max(1.0, isz.w * scale);
    outH = std::max(1.0, isz.h * scale);

    if (outW < minW) {
        double minScale = minW / isz.w;
        double candidateH = isz.h * minScale;
        if (candidateH <= maxH) {
            outW = minW;
            outH = candidateH;
        }
    }
}

static const std::string& nameColor(int userId) {
    using namespace Style;
    return kNameColors[std::abs(userId) % kNameColors.size()];
}

// ─── Custom-emoji-as-glyph plumbing (tdesktop-style) ────────────────────────
//
// tdesktop models a custom emoji as a CustomEmojiBlock that participates in
// line layout exactly like a normal glyph: it has a fixed width
// (emojiSize + 2*emojiPadding) and is drawn vertically centred on the line
// (y = (font.height - emojiSize) / 2).
//
// The Pango equivalent is `pango_attr_shape_new`: attach a "shape" attribute
// over the placeholder character's byte range, telling Pango the exact ink &
// logical extents of the glyph. Pango then:
//   • reserves the right amount of horizontal space (so end-of-line emojis
//     wrap to the next line instead of overflowing the bubble),
//   • places the glyph on the baseline like any other glyph (no vertical
//     drift below the text line),
//   • invokes our `shapeRenderer` callback at paint time so we can blit the
//     downloaded emoji PNG into the reserved box.
//
// `pango_attr_shape_new_with_data` lets us stash arbitrary user data per
// shape — we use it to pass the emoji's resolved image path through to the
// renderer callback without a global table.

namespace {

struct ShapeEmoji {
    std::string path;
    double      size;       // px — bitmap will be rendered at size×size
};

static void shapeEmojiFree(gpointer data) {
    delete static_cast<ShapeEmoji*>(data);
}

static gpointer shapeEmojiCopy(gconstpointer data) {
    if (!data) return nullptr;
    const auto* src = static_cast<const ShapeEmoji*>(data);
    return new ShapeEmoji{src->path, src->size};
}

// Pango calls this for every shape attribute in the layout while the layout
// is being painted with `pango_cairo_show_layout`. The CTM is already set up
// so that (0,0) is the glyph's origin on the baseline.
static void shapeRenderer(cairo_t*               cr,
                          PangoAttrShape*        attr,
                          gboolean               do_path,
                          gpointer               /*user_data*/)
{
    if (do_path) return;                       // we don't contribute to text path
    const auto* emoji = static_cast<const ShapeEmoji*>(attr->data);
    if (!emoji || emoji->path.empty()) return;

    // Pango sets the cairo *current point* to the glyph's baseline origin
    // before invoking us; it does NOT translate the CTM. So the bitmap must
    // be painted offset from that current point, not from (0, 0).
    double baseX = 0, baseY = 0;
    if (cairo_has_current_point(cr)) {
        cairo_get_current_point(cr, &baseX, &baseY);
    }

    // attr->ink_rect.y is negative (baseline → ascender). Add baseX/baseY so
    // the bitmap lands exactly inside the box Pango reserved for the glyph.
    const double x = PANGO_PIXELS(attr->ink_rect.x);
    const double y = PANGO_PIXELS(attr->ink_rect.y);
    const double w = PANGO_PIXELS(attr->ink_rect.width);
    const double h = PANGO_PIXELS(attr->ink_rect.height);
    const double size = std::min(w, h);
    // Centre horizontally inside the reserved cell (in case width > size due
    // to padding).
    const double cx = baseX + x + (w - size) / 2.0;
    const double cy = baseY + y + (h - size) / 2.0;
    drawEmojiSurface(cr, cx, cy, size, emoji->path);
}

// Add a `pango_attr_shape` for every custom emoji covering its UTF-8 byte
// range, so the placeholder character is replaced (visually and metrically)
// by a fixed-size box. `lineHeightPx` should be the font's full line height
// — we use it as the shape's logical/ink height so the emoji centres on the
// text line just like tdesktop's `(font.height - emojiSize) / 2` formula.
static void attachCustomEmojiShapes(
    PangoLayout*                                       layout,
    const std::string&                                 plainText,
    const std::vector<CustomEmoji>&                    emojis,
    double                                             emojiSizePx,
    const std::map<uint64_t, std::string>&             emojiMap)
{
    if (emojis.empty()) return;

    // Resolve font metrics so we know how tall the line is going to be.
    // We need this to centre the bitmap vertically on the baseline.
    PangoContext*            ctx        = pango_layout_get_context(layout);
    const PangoFontDescription* fontDesc = pango_layout_get_font_description(layout);
    if (!fontDesc) fontDesc = pango_context_get_font_description(ctx);
    PangoFontMetrics*        metrics    = pango_context_get_metrics(
        ctx, fontDesc, pango_context_get_language(ctx));
    const int ascentPU  = pango_font_metrics_get_ascent(metrics);   // pango units
    const int descentPU = pango_font_metrics_get_descent(metrics);
    pango_font_metrics_unref(metrics);

    const int sizePU       = static_cast<int>(emojiSizePx * PANGO_SCALE);
    const int paddingPU    = static_cast<int>(2 * PANGO_SCALE);     // 2px h-pad
    // Box width = emoji + 2*padding; matches tdesktop's
    //   objectWidth() = st::emojiSize + 2*st::emojiPadding
    const int boxWidthPU   = sizePU + 2 * paddingPU;
    // The shape's "height" in Pango is split as ascent-portion above the
    // baseline and descent-portion below. We want the bitmap centred on the
    // text x-height-ish region: anchor the top of the box at `ascent - size`
    // (mirroring tdesktop's `(font.height - emojiSize) / 2`).
    const int lineHeightPU = ascentPU + descentPU;
    const int topGapPU     = std::max(0, (lineHeightPU - sizePU) / 2);
    const int inkYPU       = -(ascentPU - topGapPU); // y is negative-up from baseline

    PangoRectangle inkRect{0, inkYPU,         boxWidthPU, sizePU};
    PangoRectangle logRect{0, -ascentPU,      boxWidthPU, lineHeightPU};

    // Grab the existing attribute list (markup-derived bold/italic/etc) so we
    // can merge our shape attrs into it without dropping the styling.
    PangoAttrList* existing = pango_layout_get_attributes(layout);
    PangoAttrList* attrs    = existing ? pango_attr_list_copy(existing)
                                        : pango_attr_list_new();

    const int textLen = static_cast<int>(plainText.size());
    for (const auto& ce : emojis) {
        auto it = emojiMap.find(ce.documentId);
        if (it == emojiMap.end()) continue;          // bitmap unavailable
        const int start = std::clamp<int>(ce.offset,             0, textLen);
        const int end   = std::clamp<int>(ce.offset + ce.length, start, textLen);
        if (end <= start) continue;

        auto* userData = new ShapeEmoji{it->second, emojiSizePx};
        PangoAttribute* shape = pango_attr_shape_new_with_data(
            &inkRect, &logRect, userData, &shapeEmojiCopy, &shapeEmojiFree);
        shape->start_index = static_cast<guint>(start);
        shape->end_index   = static_cast<guint>(end);
        pango_attr_list_insert(attrs, shape);        // takes ownership
    }

    pango_layout_set_attributes(layout, attrs);
    pango_attr_list_unref(attrs);
}

// Convenience: install the shape renderer on whichever PangoContext the layout
// is using. The callback is per-context, not per-layout, so this is idempotent
// — calling it more than once just resets to the same function.
static void installShapeRenderer(PangoLayout* layout) {
    PangoContext* ctx = pango_layout_get_context(layout);
    pango_cairo_context_set_shape_renderer(ctx, &shapeRenderer, nullptr, nullptr);
}

} // anonymous namespace

void Renderer::measureLayout(PangoLayout* layout, int maxWidth, int& outWidth, int& outHeight) {
    pango_layout_set_width(layout, maxWidth * PANGO_SCALE);
    pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
    pango_layout_get_pixel_size(layout, &outWidth, &outHeight);
}

// ── drawAvatar ────────────────────────────────────────────────────────────────
//
// When `avatarPath` is non-empty, the image is loaded, clipped to a circle,
// and drawn as the avatar.  Otherwise the original coloured-circle-with-
// initials fallback is used.

void Renderer::drawAvatar(cairo_t* cr, double x, double y, double size,
                          const std::string& name, int userId,
                          const std::string& avatarPath) {
    using namespace Style;
    double cx = x + size / 2.0, cy = y + size / 2.0, radius = size / 2.0;

    // ── Try drawing the real avatar image first ─────────────────────────
    if (!avatarPath.empty()) {
        std::string drawablePath = prepareDrawablePath(avatarPath);
        cairo_surface_t* img = cairo_image_surface_create_from_png(drawablePath.c_str());
        if (cairo_surface_status(img) == CAIRO_STATUS_SUCCESS) {
            double iw = cairo_image_surface_get_width(img);
            double ih = cairo_image_surface_get_height(img);
            if (iw > 0 && ih > 0) {
                // Clip to a circle
                cairo_save(cr);
                cairo_new_path(cr);
                cairo_arc(cr, cx, cy, radius, 0, 2 * M_PI);
                cairo_clip(cr);

                // Scale the image to fill the circle (centre-crop)
                double scale = std::max(size / iw, size / ih);
                double drawW = iw * scale;
                double drawH = ih * scale;
                cairo_translate(cr, cx - drawW / 2.0, cy - drawH / 2.0);
                cairo_scale(cr, scale, scale);
                cairo_set_source_surface(cr, img, 0, 0);
                cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BEST);
                cairo_paint(cr);
                cairo_restore(cr);

                cairo_surface_destroy(img);
                return;
            }
        }
        cairo_surface_destroy(img);
        // Fall through to initials-on-colour-circle if image failed
    }

    // ── Fallback: coloured circle with initials ─────────────────────────
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

void Renderer::drawReply(cairo_t* cr, double x, double y, double width, const ReplyData& reply, const std::map<uint64_t, std::string>& emojiMap) {
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
    if (!reply.pangoMarkup.empty()) pango_layout_set_markup(tl, reply.pangoMarkup.c_str(), -1);
    else pango_layout_set_text(tl, reply.text.c_str(), -1);

    // Inline custom emojis: 16px in reply previews to match Telegram's
    // compact reply card. Shape attrs make Pango reserve real space, so
    // ellipsis falls *after* the emoji box if the emoji is past the cutoff,
    // and the bitmap is painted by Pango itself (no manual positioning).
    attachCustomEmojiShapes(tl, reply.text, reply.customEmojis, 16.0, emojiMap);
    installShapeRenderer(tl);

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

// ── drawEmojiSurface ─────────────────────────────────────────────────────────

static void drawEmojiSurface(cairo_t* cr, double x, double y, double size, const std::string& path) {
    if (path.empty()) return;
    std::string finalPath = prepareDrawablePath(path);
    cairo_surface_t* img = cairo_image_surface_create_from_png(finalPath.c_str());
    if (cairo_surface_status(img) == CAIRO_STATUS_SUCCESS) {
        double iw = cairo_image_surface_get_width(img);
        double ih = cairo_image_surface_get_height(img);
        if (iw > 0 && ih > 0) {
            cairo_save(cr);
            cairo_translate(cr, x, y);
            cairo_scale(cr, size / iw, size / ih);
            cairo_set_source_surface(cr, img, 0, 0);
            cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BEST);
            cairo_paint(cr);
            cairo_restore(cr);
        }
    }
    cairo_surface_destroy(img);
}

// ── renderQuote ───────────────────────────────────────────────────────────────

void Renderer::renderQuote(
    const std::string& outputFile,
    const std::vector<MessageData>& messages,
    const RenderOptions& options,
    const std::map<uint64_t, std::string>& emojiMap)
{
    if (messages.empty()) return;
    using namespace Style;

    cairo_surface_t* measure_surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_t* measure_cr = cairo_create(measure_surf);
    int maxTextWidth = (int)(kMsgMaxWidth - kPadLeft - kPadRight);

    // Portrait media is the common case for quotes. Avoid squeezing tall images
    // into a square box, otherwise the bubble becomes too thin and the media
    // looks visually misplaced compared with Telegram-style quote cards.
    constexpr double kStickerMaxW  = 180;
    constexpr double kStickerMaxH  = 320;
    constexpr double kStickerMinW  = 150;
    constexpr double kPhotoMaxW    = 210;
    constexpr double kPhotoMaxH    = 420;
    constexpr double kPhotoMinW    = 180;
    constexpr double kPhotoBorderR = 12; // thin border radius
    constexpr double kPhotoBorder  = 2;  // border thickness
    constexpr double kInlineEmojiSize = 22; // px — body-text inline custom emoji

    struct MsgSize {
        double h;
        double textH;
        double nameH;
        double replyH;
        double mediaW;
        double mediaH;
        double bubbleW;
        bool isSticker;
    };
    std::vector<MsgSize> sizes;
    double totalH = 0;
    double maxW = kMsgMinWidth;

    std::vector<bool> firstInGroup(messages.size());
    std::vector<bool> lastInGroup(messages.size());
    for (size_t i = 0; i < messages.size(); ++i) {
        firstInGroup[i] = (i == 0 || messages[i-1].senderKey != messages[i].senderKey);
        lastInGroup[i]  = (i == messages.size() - 1 || messages[i+1].senderKey != messages[i].senderKey);
    }

    for (size_t i = 0; i < messages.size(); ++i) {
        const auto& msg = messages[i];
        bool isSticker = (msg.mediaType == MediaType::Sticker);
        bool isPhoto   = (msg.mediaType == MediaType::Photo);

        if (isSticker) {
            ImageSize isz = getImageSize(msg.photoPath);
            double sw = 0, sh = 0;
            fitMediaIntoBounds(isz, kStickerMaxW, kStickerMaxH, kStickerMinW,
                               kStickerMaxW, kStickerMaxH, sw, sh);
            sizes.push_back({sh + 8, 0, 0, 0, sw, sh, sw, true});
            maxW = std::max(maxW, sw + kPadLeft + kPadRight);
            totalH += (sh + 8) + 8; // inter-message gap
            continue;
        }

        PangoLayout* tl = pango_cairo_create_layout(measure_cr);
        PangoFontDescription* td = pango_font_description_from_string("Inter 14");
        pango_layout_set_font_description(tl, td);
        if (!msg.pangoMarkup.empty()) pango_layout_set_markup(tl, msg.pangoMarkup.c_str(), -1);
        else pango_layout_set_text(tl, msg.text.c_str(), -1);

        // Attach shape attributes for inline custom emojis BEFORE measuring,
        // so width/wrap calculations include the reserved emoji boxes — that
        // is the whole point of doing this Pango-side rather than overlaying
        // bitmaps on top of an oblivious layout.
        attachCustomEmojiShapes(tl, msg.text, msg.customEmojis, kInlineEmojiSize, emojiMap);

        int tw = 0, th = 0;
        bool hasText = !msg.text.empty() || !msg.pangoMarkup.empty();

        double nameH = 0, nameW = 0;
        bool hasCaption = !msg.text.empty() || !msg.pangoMarkup.empty();
        bool showName = firstInGroup[i] && !msg.senderName.empty();
        
        // If it's a Photo with no caption, we don't show the name (matches sticker behavior)
        if (isPhoto && !hasCaption) showName = false;

        if (showName) {
            PangoLayout* nl = pango_cairo_create_layout(measure_cr);
            PangoFontDescription* nd = pango_font_description_from_string("Inter Bold 13");
            pango_layout_set_font_description(nl, nd);
            pango_layout_set_text(nl, msg.senderName.c_str(), -1);
            int nw, nh; pango_layout_get_pixel_size(nl, &nw, &nh);
            nameH = nh + kNamePadTop + kNamePadBottom; nameW = nw;
            g_object_unref(nl); pango_font_description_free(nd);
        }

        double replyH = msg.reply.hasReply ? 40 : 0;
        double photoH = 0;
        double photoW = 0;
        ImageSize photoSize;
        bool barePHoto = isPhoto && !msg.photoPath.empty() && !hasText;
        if (isPhoto && !msg.photoPath.empty()) {
            photoSize = getImageSize(msg.photoPath);
            fitMediaIntoBounds(photoSize, kPhotoMaxW, kPhotoMaxH, kPhotoMinW,
                               kPhotoMaxW, kPhotoMaxH, photoW, photoH);
        }

        if (hasText) {
            // First measure at the normal Telegram message width. Do not force
            // captions to the small thumbnail width: that made a 6-line
            // Telegram caption become 10+ lines in quote output.
            measureLayout(tl, maxTextWidth, tw, th);
        }

        double contentW = (double)tw;
        double msgW = 0;
        if (barePHoto) {
            contentW = photoW;
            msgW = photoW;
        } else if (isPhoto && photoW > 0) {
            // Telegram-style captioned media: if caption/name/reply needs more
            // width, expand the displayed image up to the bubble width instead
            // of leaving a narrow image inside a wide bubble. This keeps the
            // bubble visually fixed to the image while preserving caption line
            // count close to Telegram.
            const double captionNeededW = std::max({
                photoW,
                (double)tw + kPadLeft + kPadRight,
                nameW + kPadLeft + kPadRight,
                (double)(msg.reply.hasReply ? 150 : 0) + kPadLeft + kPadRight
            });
            double desiredPhotoW = std::clamp(captionNeededW, kPhotoMinW, kMsgMaxWidth);
            if (photoSize.w > 0 && photoSize.h > 0 && desiredPhotoW > photoW) {
                double desiredPhotoH = desiredPhotoW * (double)photoSize.h / (double)photoSize.w;
                if (desiredPhotoH > kPhotoMaxH) {
                    desiredPhotoW = std::max(photoW, kPhotoMaxH * (double)photoSize.w / (double)photoSize.h);
                    desiredPhotoH = desiredPhotoW * (double)photoSize.h / (double)photoSize.w;
                }
                photoW = desiredPhotoW;
                photoH = desiredPhotoH;
            }

            msgW = std::max({
                photoW,
                (double)tw + kPadLeft + kPadRight,
                nameW + kPadLeft + kPadRight,
                (double)(msg.reply.hasReply ? 150 : 0) + kPadLeft + kPadRight
            });
            msgW = std::clamp(msgW, kMsgMinWidth, kMsgMaxWidth);

            // Re-measure after the final bubble/media width is known. This is
            // what reduces the accidental extra wrapping in long captions.
            if (hasText) {
                int finalTextW = (int)std::max(1.0, msgW - kPadLeft - kPadRight);
                measureLayout(tl, finalTextW, tw, th);
            }
        } else {
            msgW = std::max({contentW, nameW, (double)(msg.reply.hasReply ? 150 : 0)}) + kPadLeft + kPadRight;
            msgW = std::clamp(msgW, kMsgMinWidth, kMsgMaxWidth);
        }
        maxW = std::max(maxW, barePHoto ? 0.0 : msgW);

        double msgH = barePHoto
                    ? photoH
                    : kPadTop + nameH + replyH + (hasText ? th : 0) + (photoH > 0 ? photoH + 8 : 0) + kPadBottom;
        sizes.push_back({msgH, (double)th, nameH, replyH, photoW, photoH, msgW, false});
        totalH += msgH + 12; // Standard inter-bubble gap (12px)
        g_object_unref(tl); pango_font_description_free(td);
    }

    
    double maxNeededW = 0;
    for (size_t i = 0; i < messages.size(); ++i) {
        double w = sizes[i].bubbleW;
        if (!messages[i].isOutgoing) w += Style::kAvatarSize + Style::kAvatarMarginRight;
        if (!sizes[i].isSticker) w += Style::kTailWidth;
        maxNeededW = std::max(maxNeededW, w);
    }
    double contentW = maxNeededW;
    double canvasW = Style::kCanvasPad + contentW + Style::kCanvasPad;
    
    if (!messages.empty()) totalH -= 12; // remove last gap for equal padding
    double canvasH = Style::kCanvasPad + std::max(totalH, (double)Style::kAvatarSize) + Style::kCanvasPad;

    cairo_destroy(measure_cr); cairo_surface_destroy(measure_surf);

    const double outputScale = std::clamp(options.outputScale, 1.0, 5.0);
    const int surfaceW = std::max(1, (int)ceil(canvasW * outputScale));
    const int surfaceH = std::max(1, (int)ceil(canvasH * outputScale));

    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, surfaceW, surfaceH);
    cairo_t* cr = cairo_create(surface);

    cairo_font_options_t* fontOptions = cairo_font_options_create();
    cairo_font_options_set_antialias(fontOptions, CAIRO_ANTIALIAS_BEST);
    cairo_font_options_set_hint_style(fontOptions, CAIRO_HINT_STYLE_FULL);
    cairo_set_font_options(cr, fontOptions);
    cairo_font_options_destroy(fontOptions);

    if (options.transparent) { cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE); cairo_set_source_rgba(cr, 0, 0, 0, 0); cairo_paint(cr); }
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    if (outputScale != 1.0) cairo_scale(cr, outputScale, outputScale);

    double curY = kCanvasPad;
    double bubbleX = kCanvasPad + kAvatarSize + kAvatarMarginRight;

    for (size_t i = 0; i < messages.size(); ++i) {
        const auto& msg = messages[i];
        const auto& sz = sizes[i];

        // ── STICKER: bare thumbnail, no bubble ──────────────────────────
        if (sz.isSticker) {
            if (lastInGroup[i]) {
                double avatarY = curY + sz.h - kAvatarSize;
                drawAvatar(cr, kCanvasPad, avatarY, kAvatarSize, msg.senderName, msg.senderId, msg.avatarPath);
            }

            double sw = sz.mediaW;
            double sh = sz.mediaH;
            double sx = bubbleX;
            double sy = curY + 4;
            double sr = 8;

            std::string drawablePath = prepareDrawablePath(msg.photoPath);
            cairo_surface_t* image = cairo_image_surface_create_from_png(drawablePath.c_str());
            if (cairo_surface_status(image) == CAIRO_STATUS_SUCCESS) {
                cairo_new_path(cr);
                cairo_arc(cr, sx + sr, sy + sr, sr, M_PI, 3*M_PI/2);
                cairo_arc(cr, sx + sw - sr, sy + sr, sr, 3*M_PI/2, 2*M_PI);
                cairo_arc(cr, sx + sw - sr, sy + sh - sr, sr, 0, M_PI/2);
                cairo_arc(cr, sx + sr, sy + sh - sr, sr, M_PI/2, M_PI);
                cairo_close_path(cr);
                cairo_save(cr);
                cairo_clip(cr);
                cairo_translate(cr, sx, sy);
                cairo_scale(cr, sw / cairo_image_surface_get_width(image),
                                sh / cairo_image_surface_get_height(image));
                cairo_set_source_surface(cr, image, 0, 0);
                cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BEST);
                cairo_paint(cr);
                cairo_restore(cr);
            } else {
                cairo_new_path(cr);
                cairo_arc(cr, sx + sr, sy + sr, sr, M_PI, 3*M_PI/2);
                cairo_arc(cr, sx + sw - sr, sy + sr, sr, 3*M_PI/2, 2*M_PI);
                cairo_arc(cr, sx + sw - sr, sy + sh - sr, sr, 0, M_PI/2);
                cairo_arc(cr, sx + sr, sy + sh - sr, sr, M_PI/2, M_PI);
                cairo_close_path(cr);
                cairo_set_source_rgba(cr, 0.15, 0.15, 0.15, 0.6);
                cairo_fill(cr);
            }
            cairo_surface_destroy(image);

            curY += sz.h + 12;
            continue;
        }

        // ── NORMAL / PHOTO: draw bubble ──────────────────────────────────
        bool hasText = !msg.text.empty() || !msg.pangoMarkup.empty();
        bool isPhoto = (msg.mediaType == MediaType::Photo);
        bool showBubble = !sz.isSticker;
        bool showName   = firstInGroup[i] && !msg.senderName.empty();

        // If photo has no caption, don't show bubble/name (Telegram style)
        if (isPhoto && !hasText) {
            showBubble = false;
            showName = false;
        }

        double bubbleX = msg.isOutgoing 
            ? (kCanvasPad + contentW - sz.bubbleW - (sz.isSticker ? 0 : kTailWidth))
            : (kCanvasPad + kAvatarSize + kAvatarMarginRight);

        if (showBubble) {
            RenderOptions mOpts = options;
            mOpts.isOut = msg.isOutgoing;
            mOpts.rounding.topLeft = (firstInGroup[i]) ? CornerRounding::Large : CornerRounding::Small;
            mOpts.rounding.topRight = (firstInGroup[i]) ? CornerRounding::Large : CornerRounding::Small;
            mOpts.rounding.bottomLeft = (lastInGroup[i]) ? CornerRounding::Tail : CornerRounding::Small;
            mOpts.rounding.bottomRight = CornerRounding::Large;

            drawBubble(cr, bubbleX, curY, sz.bubbleW, sz.h, mOpts);
        }

        if (lastInGroup[i]) {
            double avatarY = curY + sz.h - kAvatarSize;
            drawAvatar(cr, kCanvasPad, avatarY, kAvatarSize, msg.senderName, msg.senderId, msg.avatarPath);
        }

        double py = curY + kPadTop;
        if (showName) {
            auto color = hexToRGBA(nameColor(msg.senderId));
            cairo_set_source_rgba(cr, color.r, color.g, color.b, color.a);
            PangoLayout* nl = pango_cairo_create_layout(cr);
            PangoFontDescription* nd = pango_font_description_from_string("Inter Bold 13");
            pango_layout_set_font_description(nl, nd);
            pango_layout_set_text(nl, msg.senderName.c_str(), -1);
            cairo_move_to(cr, bubbleX + kPadLeft, py + kNamePadTop);
            pango_cairo_show_layout(cr, nl);

            // ── Emoji Status ─────────────────────────────────────────────
            if (msg.emojiStatusId != 0 && emojiMap.count(msg.emojiStatusId)) {
                std::cout << "[Renderer] Drawing status emoji: " << msg.emojiStatusId << std::endl;
                int nw, nh; pango_layout_get_pixel_size(nl, &nw, &nh);
                drawEmojiSurface(cr, bubbleX + kPadLeft + nw + 4,
                                 py + kNamePadTop + (nh - 20)/2.0,
                                 20, emojiMap.at(msg.emojiStatusId));
            }
            py += sz.nameH; g_object_unref(nl); pango_font_description_free(nd);
        }

        // ── Reply Block ──────────────────────────────────────────────────
        if (msg.reply.hasReply) {
            drawReply(cr, bubbleX + kPadLeft, py, sz.bubbleW - kPadLeft - kPadRight, msg.reply, emojiMap);
            py += sz.replyH;
        }

        // ── PHOTO inside bubble with thin rounded border ──────────────────
        if (msg.mediaType == MediaType::Photo && !msg.photoPath.empty()) {
            bool barePhotoRender = !hasText;
            double pw = sz.mediaW;
            double ph = sz.mediaH;

            double px = barePhotoRender
                ? bubbleX
                : bubbleX + std::max(0.0, (sz.bubbleW - pw) / 2.0);
            cairo_new_path(cr);
            cairo_arc(cr, px + kPhotoBorderR, py + kPhotoBorderR, kPhotoBorderR, M_PI, 3*M_PI/2);
            cairo_arc(cr, px + pw - kPhotoBorderR, py + kPhotoBorderR, kPhotoBorderR, 3*M_PI/2, 2*M_PI);
            cairo_arc(cr, px + pw - kPhotoBorderR, py + ph - kPhotoBorderR, kPhotoBorderR, 0, M_PI/2);
            cairo_arc(cr, px + kPhotoBorderR, py + ph - kPhotoBorderR, kPhotoBorderR, M_PI/2, M_PI);
            cairo_close_path(cr);

            // Draw actual image if available
            std::string drawablePath = prepareDrawablePath(msg.photoPath);
            cairo_surface_t* image = cairo_image_surface_create_from_png(drawablePath.c_str());
            if (cairo_surface_status(image) == CAIRO_STATUS_SUCCESS) {
                cairo_save(cr); cairo_clip(cr);
                cairo_translate(cr, px, py);
                cairo_scale(cr, pw / cairo_image_surface_get_width(image),
                                ph / cairo_image_surface_get_height(image));
                cairo_set_source_surface(cr, image, 0, 0);
                cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BEST);
                cairo_paint(cr);
                cairo_restore(cr);
            } else {
                cairo_set_source_rgba(cr, 0.15, 0.15, 0.18, 1);
                cairo_fill_preserve(cr);
                cairo_set_source_rgba(cr, 0.35, 0.35, 0.4, 0.6);
                cairo_set_line_width(cr, kPhotoBorder);
                cairo_stroke(cr);
            }
            cairo_surface_destroy(image);
            py += sz.mediaH + (barePhotoRender ? 0 : 8);
        }

        // ── Text with Inline Emojis (via Pango shape attrs) ───────────────
        if (hasText) {
            PangoLayout* tl = pango_cairo_create_layout(cr);
            PangoFontDescription* td = pango_font_description_from_string("Inter 14");
            pango_layout_set_font_description(tl, td);
            pango_layout_set_width(tl, (sz.bubbleW - kPadLeft - kPadRight) * PANGO_SCALE);
            pango_layout_set_wrap(tl, PANGO_WRAP_WORD_CHAR);
            if (!msg.pangoMarkup.empty()) pango_layout_set_markup(tl, msg.pangoMarkup.c_str(), -1);
            else pango_layout_set_text(tl, msg.text.c_str(), -1);

            // Inline custom emojis: handled exactly like tdesktop's
            // CustomEmojiBlock — a real glyph in the line, properly sized
            // (kInlineEmojiSize) and vertically centred on the baseline.
            // The shape renderer callback paints our bitmap when Pango
            // walks the layout.
            attachCustomEmojiShapes(tl, msg.text, msg.customEmojis, kInlineEmojiSize, emojiMap);
            installShapeRenderer(tl);

            cairo_set_source_rgb(cr, 1, 1, 1);
            cairo_move_to(cr, bubbleX + kPadLeft, py);
            pango_cairo_show_layout(cr, tl);

            g_object_unref(tl); pango_font_description_free(td);
        }

        curY += sz.h + 12;
    }

    cairo_surface_write_to_png(surface, outputFile.c_str());
    cairo_destroy(cr); cairo_surface_destroy(surface);
}

} // namespace Quote
