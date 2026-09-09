// this file is part of AlexaInc / QuotlyNative — dependency-free image decoding
// developer hansaka@alexainc

#include "image_decode.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>
#include <sys/stat.h>

#include <webp/decode.h>
#include <webp/demux.h>

#define STB_IMAGE_IMPLEMENTATION
#include "third_party/stb_image.h"

namespace Quote {

static bool fileExists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && st.st_size > 0;
}

static bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
}

std::string prepareDrawablePath(const std::string& path) {
    if (path.empty()) return "";
    if (endsWith(path, ".png")) return path;

    std::string pngPath = path + ".png";
    if (fileExists(pngPath)) return pngPath;

    // Only containers we cannot decode natively go through external tools.
    // (PNG/JPEG/BMP/GIF via stb_image, WEBP via libwebp — no shell-out.)
    std::string cmd;
    if (endsWith(path, ".webm") || endsWith(path, ".mp4") || endsWith(path, ".tgs")) {
        cmd = "ffmpeg -y -i \"" + path + "\" -frames:v 1 \"" + pngPath + "\" >/dev/null 2>&1";
    }

    if (!cmd.empty()) system(cmd.c_str());
    return fileExists(pngPath) ? pngPath : path;
}

static std::vector<unsigned char> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    return std::vector<unsigned char>((std::istreambuf_iterator<char>(f)),
                                      std::istreambuf_iterator<char>());
}

static bool isWebpMagic(const std::vector<unsigned char>& b) {
    return b.size() >= 12 && !memcmp(b.data(), "RIFF", 4) &&
           !memcmp(b.data() + 8, "WEBP", 4);
}

// RGBA (straight alpha) → cairo premultiplied native-endian ARGB32.
static cairo_surface_t* surfaceFromRGBA(const unsigned char* px, int w, int h) {
    if (!px || w <= 0 || h <= 0) {
        return cairo_image_surface_create(CAIRO_FORMAT_ARGB32, -1, -1);
    }
    const int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, w);
    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    if (cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) return surf;

    unsigned char* data = cairo_image_surface_get_data(surf);
    for (int y = 0; y < h; ++y) {
        unsigned char* row = data + (size_t)y * stride;
        const unsigned char* s = px + (size_t)y * w * 4;
        for (int x = 0; x < w; ++x) {
            const unsigned int r = s[x * 4 + 0];
            const unsigned int g = s[x * 4 + 1];
            const unsigned int b = s[x * 4 + 2];
            const unsigned int a = s[x * 4 + 3];
            row[x * 4 + 0] = (unsigned char)((b * a) / 255);
            row[x * 4 + 1] = (unsigned char)((g * a) / 255);
            row[x * 4 + 2] = (unsigned char)((r * a) / 255);
            row[x * 4 + 3] = (unsigned char)a;
        }
    }
    cairo_surface_mark_dirty(surf);
    return surf;
}

// Decode via stb_image (sniffs the real format from the bytes, so a JPEG saved
// under a .png extension — a common bot payload mistake — still loads).
static cairo_surface_t* surfaceFromStb(const std::string& path) {
    int w = 0, h = 0, n = 0;
    unsigned char* px = stbi_load(path.c_str(), &w, &h, &n, 4);
    if (!px) {
        // error surface — callers check cairo_surface_status(), same contract
        // as cairo_image_surface_create_from_png()
        return cairo_image_surface_create(CAIRO_FORMAT_ARGB32, -1, -1);
    }
    cairo_surface_t* surf = surfaceFromRGBA(px, w, h);
    stbi_image_free(px);
    return surf;
}

// Native WebP decode. Animated stickers (ANIM/ANMF containers) render their
// FIRST frame — frame 1 is always a full keyframe, so no blending needed.
// Static WebP is just a one-frame container, so the same path covers both.
static cairo_surface_t* surfaceFromWebp(const std::vector<unsigned char>& bytes) {
    cairo_surface_t* err = nullptr;
    WebPData wpd{bytes.data(), bytes.size()};
    WebPDemuxer* demux = WebPDemux(&wpd);
    if (!demux) return cairo_image_surface_create(CAIRO_FORMAT_ARGB32, -1, -1);

    WebPIterator it;
    if (WebPDemuxGetFrame(demux, 1, &it)) {
        int w = 0, h = 0;
        unsigned char* px = WebPDecodeRGBA(it.fragment.bytes, it.fragment.size, &w, &h);
        if (px) {
            err = surfaceFromRGBA(px, w, h);
            WebPFree(px);
        }
        WebPDemuxReleaseIterator(&it);
    }
    WebPDemuxDelete(demux);
    if (err) return err;
    return cairo_image_surface_create(CAIRO_FORMAT_ARGB32, -1, -1);
}

// Crop fully-transparent margins (bots frequently ship media canvases padded
// with alpha=0 — that made photos look shifted inside the quote). The same
// function is used by probeImageSize(), so the measure and draw passes always
// agree on the cropped dimensions.
static cairo_surface_t* alphaCrop(cairo_surface_t* s) {
    if (cairo_surface_status(s) != CAIRO_STATUS_SUCCESS ||
        cairo_image_surface_get_format(s) != CAIRO_FORMAT_ARGB32) return s;
    const int w = cairo_image_surface_get_width(s);
    const int h = cairo_image_surface_get_height(s);
    const int stride = cairo_image_surface_get_stride(s);
    const unsigned char* data = cairo_image_surface_get_data(s);
    int x0 = w, y0 = h, x1 = -1, y1 = -1;
    for (int y = 0; y < h; ++y) {
        const unsigned char* row = data + (size_t)y * stride;
        for (int x = 0; x < w; ++x) {
            if (row[x * 4 + 3] != 0) {
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
                if (y < y0) y0 = y;
                if (y > y1) y1 = y;
            }
        }
    }
    if (x1 < 0 || (x0 == 0 && y0 == 0 && x1 == w - 1 && y1 == h - 1)) return s;
    const int cw = x1 - x0 + 1, ch = y1 - y0 + 1;
    cairo_surface_t* c = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, cw, ch);
    if (cairo_surface_status(c) != CAIRO_STATUS_SUCCESS) { cairo_surface_destroy(c); return s; }
    unsigned char* cd = cairo_image_surface_get_data(c);
    const int cstride = cairo_image_surface_get_stride(c);
    for (int y = 0; y < ch; ++y)
        memcpy(cd + (size_t)y * cstride,
               data + (size_t)(y0 + y) * stride + x0 * 4, (size_t)cw * 4);
    cairo_surface_mark_dirty(c);
    cairo_surface_destroy(s);
    return c;
}

cairo_surface_t* loadImageSurface(const std::string& path) {
    if (path.empty()) return cairo_image_surface_create(CAIRO_FORMAT_ARGB32, -1, -1);

    const std::string drawable = prepareDrawablePath(path);

    // Animated/video containers that could not be converted are unsupported.
    if (endsWith(drawable, ".webm") || endsWith(drawable, ".mp4") || endsWith(drawable, ".tgs")) {
        return cairo_image_surface_create(CAIRO_FORMAT_ARGB32, -1, -1);
    }

    // Sniff the real container — payloads frequently mislabel MIME types.
    std::vector<unsigned char> head = readFile(drawable);
    if (head.empty()) return cairo_image_surface_create(CAIRO_FORMAT_ARGB32, -1, -1);

    if (isWebpMagic(head)) return alphaCrop(surfaceFromWebp(head));

    if (endsWith(drawable, ".png") ||
        (head.size() >= 8 && !memcmp(head.data(), "\x89PNG\r\n\x1a\n", 8))) {
        cairo_surface_t* s = cairo_image_surface_create_from_png(drawable.c_str());
        if (cairo_surface_status(s) == CAIRO_STATUS_SUCCESS) return alphaCrop(s);
        cairo_surface_destroy(s);
        // Fall through: file claims .png but may hold JPEG bytes — stb sniffs.
    }

    return alphaCrop(surfaceFromStb(drawable));
}

// Dimensions of the *drawable* surface (after alpha-margin crop), so the
// measure pass lays out exactly what the draw pass will paint.
DecodedSize probeImageSize(const std::string& path) {
    cairo_surface_t* s = loadImageSurface(path);
    DecodedSize sz{0, 0};
    if (cairo_surface_status(s) == CAIRO_STATUS_SUCCESS) {
        sz.w = cairo_image_surface_get_width(s);
        sz.h = cairo_image_surface_get_height(s);
    }
    cairo_surface_destroy(s);
    return sz;
}

} // namespace Quote
