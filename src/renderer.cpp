// this file is part of AlexaInc / QuotlyNative — Cairo Renderer
// developer hansaka@alexainc

#include "renderer.h"
#include "style_constants.h"
#include <cmath>
#include <iostream>
#include <algorithm>
#include <map>
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
    cairo_set_source_rgba(cr, 0.8, 0.8, 0.8, 0.8);
    cairo_move_to(cr, x + 8, y + 18);
    pango_cairo_show_layout(cr, tl);
    
    if (!reply.customEmojis.empty()) {
        // Reply previews are single-line and ellipsized. Determine the byte
        // range that is actually visible so we don't draw emoji bitmaps over
        // (or beyond) the ellipsis. (Bug fix: emojis used to leak past the
        // truncated reply text and float into empty space outside the bubble.)
        int visibleEnd = (int)reply.text.size();
        PangoLayoutLine* firstLine = pango_layout_get_line_readonly(tl, 0);
        if (firstLine) {
            visibleEnd = firstLine->start_index + firstLine->length;
        }
        int layoutPxW = 0, layoutPxH = 0;
        pango_layout_get_pixel_size(tl, &layoutPxW, &layoutPxH);

        for (const auto& ce : reply.customEmojis) {
            uint64_t eid = ce.documentId;
            if (!emojiMap.count(eid)) continue;
            int byteIndex = std::clamp(ce.offset, 0, (int)reply.text.size());
            if (byteIndex >= visibleEnd) continue; // hidden behind ellipsis

            PangoRectangle pos;
            pango_layout_index_to_pos(tl, byteIndex, &pos);
            double emojiSize = 16;
            double cellW = std::max(emojiSize, (double)PANGO_PIXELS(pos.width));
            double ex = x + 8 + PANGO_PIXELS(pos.x) + (cellW - emojiSize) / 2.0;
            double lineH = std::max(emojiSize, (double)PANGO_PIXELS(pos.height));
            double ey = y + 18 + PANGO_PIXELS(pos.y) + (lineH - emojiSize) / 2.0;

            // Final safety clamp: never draw past the layout's right edge.
            if (ex + emojiSize > x + 8 + layoutPxW) continue;
            drawEmojiSurface(cr, ex, ey, emojiSize, emojiMap.at(eid));
        }
    }

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
        int tw = 0, th = 0;
        bool hasText = !msg.text.empty() || !msg.pangoMarkup.empty();
        if (hasText) measureLayout(tl, maxTextWidth, tw, th);

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
        bool barePHoto = isPhoto && !msg.photoPath.empty() && !hasText;
        if (isPhoto && !msg.photoPath.empty()) {
            ImageSize isz = getImageSize(msg.photoPath);
            fitMediaIntoBounds(isz, kPhotoMaxW, kPhotoMaxH, kPhotoMinW,
                               kPhotoMaxW, kPhotoMaxH, photoW, photoH);
            if (!barePHoto && isz.w > 0 && isz.h > 0) {
                const double preferredW = std::clamp(std::max((double)tw, photoW), kPhotoMinW, kPhotoMaxW);
                const double preferredH = preferredW * (double)isz.h / (double)isz.w;
                if (preferredH <= kPhotoMaxH) {
                    photoW = preferredW;
                    photoH = preferredH;
                }
            }
        }

        double contentW = (double)tw;
        if (barePHoto) {
            contentW = photoW;
        } else if (isPhoto) {
            contentW = std::max(contentW, photoW);
        }
        double msgW = std::max({contentW, nameW, (double)(msg.reply.hasReply ? 150 : 0)}) + kPadLeft + kPadRight;
        if (barePHoto) msgW = photoW; 
        msgW = std::clamp(msgW, kMsgMinWidth, kMsgMaxWidth);
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

    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, (int)ceil(canvasW), (int)ceil(canvasH));
    cairo_t* cr = cairo_create(surface);
    if (options.transparent) { cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE); cairo_set_source_rgba(cr, 0, 0, 0, 0); cairo_paint(cr); }
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    double curY = kCanvasPad;
    double bubbleX = kCanvasPad + kAvatarSize + kAvatarMarginRight;

    for (size_t i = 0; i < messages.size(); ++i) {
        const auto& msg = messages[i];
        const auto& sz = sizes[i];

        // ── STICKER: bare thumbnail, no bubble ──────────────────────────
        if (sz.isSticker) {
            if (lastInGroup[i]) {
                double avatarY = curY + sz.h - kAvatarSize;
                drawAvatar(cr, kCanvasPad, avatarY, kAvatarSize, msg.senderName, msg.senderId);
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
            drawAvatar(cr, kCanvasPad, avatarY, kAvatarSize, msg.senderName, msg.senderId);
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

            double px = barePhotoRender ? bubbleX : bubbleX + kPadLeft;
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

        // ── Text with Inline Emojis ───────────────────────────────────────
        if (hasText) {
            PangoLayout* tl = pango_cairo_create_layout(cr);
            PangoFontDescription* td = pango_font_description_from_string("Inter 14");
            pango_layout_set_font_description(tl, td);
            pango_layout_set_width(tl, (sz.bubbleW - kPadLeft - kPadRight) * PANGO_SCALE);
            pango_layout_set_wrap(tl, PANGO_WRAP_WORD_CHAR);
            if (!msg.pangoMarkup.empty()) pango_layout_set_markup(tl, msg.pangoMarkup.c_str(), -1);
            else pango_layout_set_text(tl, msg.text.c_str(), -1);

            cairo_set_source_rgb(cr, 1, 1, 1);
            cairo_move_to(cr, bubbleX + kPadLeft, py);
            pango_cairo_show_layout(cr, tl);

            // ── Inline Emojis ─────────────────────────────────────────────
            if (!msg.customEmojis.empty()) {
                // The text engine reserves a wide cell for each custom emoji
                // via `letter_spacing`, so Pango's line breaker has already
                // accounted for the bitmap's footprint. We just need to centre
                // the bitmap inside that reserved cell to keep visual rhythm.
                // (Bug fix: previously the bitmap anchored to pos.x and could
                // overflow the bubble on the right when the cell was wider
                // than the placeholder glyph.)
                for (const auto& ce : msg.customEmojis) {
                    uint64_t eid = ce.documentId;
                    if (!emojiMap.count(eid)) {
                        std::cout << "[Renderer] Asset NOT in emojiMap: " << eid << std::endl;
                        continue;
                    }

                    PangoRectangle pos;
                    int byteIndex = std::clamp(ce.offset, 0, (int)msg.text.size());
                    pango_layout_index_to_pos(tl, byteIndex, &pos);
                    double emojiSize = 22;
                    double cellW = std::max(emojiSize, (double)PANGO_PIXELS(pos.width));
                    double ex = bubbleX + kPadLeft + PANGO_PIXELS(pos.x) + (cellW - emojiSize) / 2.0;
                    double lineH = std::max(emojiSize, (double)PANGO_PIXELS(pos.height));
                    double ey = py + PANGO_PIXELS(pos.y) + (lineH - emojiSize) / 2.0;

                    // Safety clamp: never let the bitmap escape the bubble's
                    // inner text rectangle.
                    double rightEdge = bubbleX + sz.bubbleW - kPadRight;
                    if (ex + emojiSize > rightEdge) {
                        ex = rightEdge - emojiSize;
                    }
                    std::cout << "[Renderer] Drawing inline emoji " << eid << " at " << ex << ", " << ey << std::endl;
                    drawEmojiSurface(cr, ex, ey, emojiSize, emojiMap.at(eid));
                }
            }

            g_object_unref(tl); pango_font_description_free(td);
        }

        curY += sz.h + 12;
    }

    cairo_surface_write_to_png(surface, outputFile.c_str());
    cairo_destroy(cr); cairo_surface_destroy(surface);
}

} // namespace Quote
