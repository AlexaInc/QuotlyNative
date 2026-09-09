// this file is part of AlexaInc / QuotlyNative — dependency-free image decoding
// developer hansaka@alexainc
//
// Cairo can only load PNG. Previously every non-PNG asset was converted by
// shelling out to ImageMagick (`magick`/`convert`), which is NOT installed in
// the Docker/HF runtime — so JPEG avatars & photos silently fell back to the
// dummy initials circle / grey placeholder. Worse, payloads that declare the
// wrong MIME (e.g. `data:image/png;base64,/9j/...` — JPEG bytes) were saved
// with a .png extension and then rejected by cairo's PNG loader.
//
// This module fixes both: it sniffs the real container and decodes
// JPEG/PNG/BMP/GIF natively via stb_image (no external tools), keeping the
// dwebp/ffmpeg shell-out only for webp/tgs/webm.

#pragma once
#include <string>
#include <cairo.h>

namespace Quote {

// Convert containers we cannot decode natively (webp/tgs/webm/mp4) to PNG via
// external tools when available; returns either a converted *.png, or the
// original path for natively-decodable files (png/jpg/jpeg/gif/bmp/…).
std::string prepareDrawablePath(const std::string& path);

// Load any supported image into a cairo ARGB32 surface (premultiplied).
// Never returns nullptr — check cairo_surface_status() on the result, exactly
// like surfaces created by cairo_image_surface_create_from_png().
// Supported natively: PNG, JPEG, BMP, GIF (stb_image) and WebP incl. the
// first frame of *animated* WebP stickers (libwebp demux).
cairo_surface_t* loadImageSurface(const std::string& path);

struct DecodedSize { int w = 0, h = 0; };

// Pixel dimensions of the image without fully decoding it (webp canvas,
// PNG IHDR, JPEG SOF). {0,0} when unknown.
DecodedSize probeImageSize(const std::string& path);

} // namespace Quote
