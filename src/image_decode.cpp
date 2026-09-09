// this file is part of AlexaInc / QuotlyNative — dependency-free image decoding
// developer hansaka@alexainc

#include "image_decode.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
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
        return cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
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
        return cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
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
    if (!demux) return cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);

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
    return cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
}

cairo_surface_t* loadImageSurface(const std::string& path) {
    if (path.empty()) return cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);

    const std::string drawable = prepareDrawablePath(path);

    // Animated/video containers that could not be converted are unsupported.
    if (endsWith(drawable, ".webm") || endsWith(drawable, ".mp4") || endsWith(drawable, ".tgs")) {
        return cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    }

    // Sniff the real container — payloads frequently mislabel MIME types.
    std::vector<unsigned char> head = readFile(drawable);
    if (head.empty()) return cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);

    if (isWebpMagic(head)) return surfaceFromWebp(head);

    if (endsWith(drawable, ".png") ||
        (head.size() >= 8 && !memcmp(head.data(), "\x89PNG\r\n\x1a\n", 8))) {
        cairo_surface_t* s = cairo_image_surface_create_from_png(drawable.c_str());
        if (cairo_surface_status(s) == CAIRO_STATUS_SUCCESS) return s;
        cairo_surface_destroy(s);
        // Fall through: file claims .png but may hold JPEG bytes — stb sniffs.
    }

    return surfaceFromStb(drawable);
}

DecodedSize probeImageSize(const std::string& path) {
    const std::string drawable = prepareDrawablePath(path);
    std::vector<unsigned char> b = readFile(drawable);
    if (b.size() < 12) return {0, 0};

    // WebP (static or animated): canvas size from the demuxer.
    if (isWebpMagic(b)) {
        WebPData wpd{b.data(), b.size()};
        WebPDemuxer* demux = WebPDemux(&wpd);
        if (!demux) return {0, 0};
        DecodedSize sz{(int)WebPDemuxGetI(demux, WEBP_FF_CANVAS_WIDTH),
                       (int)WebPDemuxGetI(demux, WEBP_FF_CANVAS_HEIGHT)};
        WebPDemuxDelete(demux);
        return sz;
    }

    // PNG
    if (!memcmp(b.data(), "\x89PNG\r\n\x1a\n", 8) && b.size() >= 24) {
        DecodedSize sz;
        sz.w = (b[16] << 24) | (b[17] << 16) | (b[18] << 8) | b[19];
        sz.h = (b[20] << 24) | (b[21] << 16) | (b[22] << 8) | b[23];
        return sz;
    }

    // JPEG: walk markers to the SOF frame header.
    if (b[0] == 0xFF && b[1] == 0xD8) {
        FILE* f = fopen(drawable.c_str(), "rb");
        if (!f) return {0, 0};
        fseek(f, 2, SEEK_SET);
        DecodedSize sz{0, 0};
        while (true) {
            uint8_t marker[2];
            if (fread(marker, 1, 2, f) < 2) break;
            if (marker[0] != 0xFF) break;
            if (marker[1] >= 0xC0 && marker[1] <= 0xC3) {
                fseek(f, 3, SEEK_CUR);
                uint8_t d[4];
                if (fread(d, 1, 4, f) == 4) {
                    sz.h = (d[0] << 8) | d[1];
                    sz.w = (d[2] << 8) | d[3];
                }
                break;
            } else {
                uint8_t l[2];
                if (fread(l, 1, 2, f) < 2) break;
                int len = (l[0] << 8) | l[1];
                fseek(f, len - 2, SEEK_CUR);
            }
        }
        fclose(f);
        return sz;
    }

    return {0, 0};
}

} // namespace Quote
