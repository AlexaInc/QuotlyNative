// this file is part of AlexaInc / QuotlyNative — dependency-free image decoding
// developer hansaka@alexainc

#include "image_decode.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>

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

    // Only containers stb_image cannot parse go through external tools.
    // (dwebp ships in the runtime image; ffmpeg is optional.)
    std::string cmd;
    if (endsWith(path, ".webp")) {
        cmd = "dwebp \"" + path + "\" -o \"" + pngPath + "\" >/dev/null 2>&1";
    } else if (endsWith(path, ".webm") || endsWith(path, ".mp4") || endsWith(path, ".tgs")) {
        cmd = "ffmpeg -y -i \"" + path + "\" -frames:v 1 \"" + pngPath + "\" >/dev/null 2>&1";
    }

    if (!cmd.empty()) system(cmd.c_str());
    return fileExists(pngPath) ? pngPath : path;
}

// Decode via stb_image (sniffs the real format from the bytes, so a JPEG saved
// under a .png extension — a common bot payload mistake — still loads) and
// convert straight-through RGBA into cairo's premultiplied native-endian
// ARGB32 pixel layout.
static cairo_surface_t* surfaceFromStb(const std::string& path) {
    int w = 0, h = 0, n = 0;
    unsigned char* px = stbi_load(path.c_str(), &w, &h, &n, 4);
    if (!px || w <= 0 || h <= 0) {
        if (px) stbi_image_free(px);
        // error surface — callers check cairo_surface_status(), same contract
        // as cairo_image_surface_create_from_png()
        return cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    }

    const int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, w);
    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    if (cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
        stbi_image_free(px);
        return surf;
    }

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
    stbi_image_free(px);
    cairo_surface_mark_dirty(surf);
    return surf;
}

cairo_surface_t* loadImageSurface(const std::string& path) {
    if (path.empty()) return cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);

    const std::string drawable = prepareDrawablePath(path);

    // Animated/video containers that could not be converted are unsupported.
    if (endsWith(drawable, ".webm") || endsWith(drawable, ".mp4") || endsWith(drawable, ".tgs")) {
        return cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    }

    if (endsWith(drawable, ".png")) {
        cairo_surface_t* s = cairo_image_surface_create_from_png(drawable.c_str());
        if (cairo_surface_status(s) == CAIRO_STATUS_SUCCESS) return s;
        cairo_surface_destroy(s);
        // Fall through: the file claims .png but may hold JPEG bytes (wrong
        // MIME in the payload) — stb sniffs the actual container.
    }

    return surfaceFromStb(drawable);
}

} // namespace Quote
