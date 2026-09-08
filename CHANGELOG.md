# Changelog

## Unreleased — Layout & script fixes (premium badge width, Sinhala fonts, timestamps)

### Premium / emoji-status badge overflowed the bubble

The measure pass computed the sender-name width from the name text only and
the draw pass then painted the 20 px emoji-status badge *after* it. With a
long name the badge landed outside the bubble's right wall (see the
"hansaka rasanjana ⭐" reference render). The measure pass now reserves
`kEmojiStatusGap + kEmojiStatusSize` whenever the badge will be drawn, and
the draw pass uses the same `Style` constants instead of hard-coded `4`/`20`,
so bubble width and badge position can never disagree again.

### Sinhala rendered with broken conjuncts ("ugly characters")

With the font description set to `Inter`, Pango's fontconfig fallback
resolved Sinhala codepoints to the legacy **LKLUG** font, which does not
shape modern conjuncts (න්‍ය, ක්‍ෂ, …) correctly at chat sizes — clusters came
out with wide gaps and decomposed marks. All font descriptions now use the
fallback list `Inter, Noto Sans, Noto Sans Sinhala` (`Style::kFontFamily`),
which pins Sinhala runs to Noto Sans Sinhala (verified run-by-run with a
Pango itemization probe) while Latin still uses Inter where installed.
`Noto Color Emoji` is deliberately *not* in the list: putting it there makes
fontconfig hijack ASCII digits for the emoji font.

### Optional Telegram-style timestamps

New payload fields `timeString` / `time` (ready-made text) or `date`
(unix epoch, formatted `H:MM AM/PM`). The time is right-aligned on the last
text line when it fits next to it (measured at the exact draw width so the
measure and draw passes always agree), otherwise on its own line below the
text; on bare photos it is overlaid on the photo's bottom-right corner,
matching Telegram.

### Build ergonomics

`CMakeLists.txt` now checks for `crow.h`, `asio.hpp` and
`nlohmann/json.hpp` at configure time and prints the exact install command
when one is missing, instead of failing with
`fatal error: asio.hpp: No such file or directory` inside `crow.h`.

New test assets: `tests/make_assets.py` (generates offline emoji bitmaps +
a sample photo), `tests/repro_premium_star.json`, `tests/repro_sinhala.json`,
`tests/screenshot_replica.json`.

### JPEG avatars & photos fell back to the dummy initials circle

Two independent causes:

1. Cairo only loads PNG. Non-PNG assets were converted by shelling out to
   ImageMagick (`magick`/`convert`), which is **not installed** in the
   Docker / HF runtime — so JPEGs never decoded.
2. `avatarBase64` / `photoBase64` payloads that declare the wrong MIME
   (real-world case: `data:image/png;base64,/9j/…` — JPEG bytes) were saved
   with a `.png` extension and then rejected by cairo's PNG loader.

New `src/image_decode.{h,cpp}` (vendored `stb_image.h`, zero new system
deps) sniffs the actual container and decodes JPEG/PNG/BMP/GIF natively —
premultiplied ARGB32, straight from the bytes — so a mislabelled JPEG still
renders as the real avatar. `dwebp`/`ffmpeg` shell-outs remain only for
webp/tgs/webm/mp4. All four image load sites (avatar, sticker, photo,
emoji-status badge) now go through `Quote::loadImageSurface()`, which
keeps cairo's error-surface contract, so every existing fallback behaves
exactly as before when a file is truly undecodable.

Verified end-to-end: a payload carrying the exact mislabelled JPEG
(`image/png` MIME, JPEG bytes) now renders the photo avatar instead of the
initials circle; correct `image/jpeg` payloads and all PNG / emoji /
Sinhala / timestamp regressions are unchanged.

## Unreleased — Custom emoji rendering rewrite (tdesktop-style inline glyphs)

### The bug

Premium / custom emojis used to be drawn as a **second pass** on top of an
already-laid-out Pango paragraph. The text engine emitted a `<span>` with
`letter_spacing` to "reserve" some horizontal room for the bitmap, and the
renderer then iterated `MessageData::customEmojis`, asked Pango for the
`pos.x / pos.y` of the placeholder character, and blitted the PNG there.

That model has three independent failure modes, all of which were visible in
the supplied reference render:

1. **Right-edge overflow.** Pango sized the reserved cell from the
   placeholder glyph's *advance*, not from the bitmap's actual width. When
   the placeholder happened to land at end-of-line the bitmap painted past
   the bubble's right wall.
2. **Vertical drift.** The second-pass code positioned the bitmap using
   `pos.y` + a custom centring formula. Any mismatch between the
   placeholder's font metrics and the bitmap height made the emoji ride
   above (or below) the surrounding text baseline.
3. **Orphan emoji past ellipsis.** Reply previews are single-line and
   ellipsised (`PANGO_ELLIPSIZE_END`). The overlay code drew the bitmap at
   the placeholder's *original* index even when that index was inside the
   truncated tail — leaving the bitmap dangling in empty space to the right
   of the "…".

### The fix

Custom emojis are now first-class glyphs in the Pango line, the way
[tdesktop](https://github.com/telegramdesktop/tdesktop)'s `CustomEmojiBlock`
treats them in `lib_ui/ui/text/text_block.cpp`:

```
// tdesktop, AbstractBlock::objectWidth():
case TextBlockType::CustomEmoji:
    return static_cast<const CustomEmojiBlock*>(this)->custom()->width();
// and in the renderer:
auto emojiY = (_t->_st->font->height - st::emojiSize) / 2;
```

Pango has a built-in mechanism for inline objects with author-controlled
metrics: `pango_attr_shape_new`. We attach one shape attribute per custom
emoji over the placeholder character's byte range, declaring the exact
ink/logical rectangle the glyph occupies. Pango then:

* reserves the correct horizontal space (end-of-line emojis wrap to the next
  line instead of overflowing the bubble);
* places the glyph on the baseline like any other glyph (no vertical drift);
* picks the ellipsis cutoff *after* accounting for the emoji box, so a reply
  preview will hide the emoji behind the "…" instead of leaking it.

Painting is delegated to a `pango_cairo_context_set_shape_renderer` callback
that pulls the resolved PNG path out of the shape attribute's user data and
blits it into the reserved box. Centring, sizing and ellipsis handling all
happen inside Pango — no second pass, no manual `index_to_pos`, no
fudge-factor offsets.

### Changed files

* **`src/text_engine.cpp`** — `custom_emoji` entities no longer emit any
  Pango markup span. The placeholder character is left in the layout text
  exactly as Telegram sent it; the renderer now owns the reservation.
* **`src/renderer.cpp`** — new `attachCustomEmojiShapes()` (adds the shape
  attrs) and `shapeRenderer()` (paints the bitmap at the cairo *current
  point* — i.e. the glyph baseline origin Pango sets up before invoking us).
  Wired into both the measurement pass (so bubble width / wrap calculations
  see the emoji boxes) and the paint pass for both body text and reply
  previews. Removed the now-dead manual `index_to_pos`-based overlay code.

### Side benefits

* Fewer code paths: body text and reply text share the same emoji plumbing.
* Bubble width auto-grows to fit the longest line including emojis, because
  the measurement layout already accounts for them.
* When the bitmap fetch fails (no entry in `emojiMap`), no shape attr is
  added at all — Pango falls back to whatever the OS font shows for that
  codepoint, which is a strictly better failure mode than the previous
  "transparent box plus nothing painted on top".

### Unchanged

`api_handler.*`, `main.cpp`, `tg_client.*`, `mtproto/*`, `style_constants.h`,
`renderer.h`, `text_engine.h`, `CMakeLists.txt`, `Dockerfile*`, tests,
prebuilt PNGs — all untouched.

### Documentation

* `README.md` — full project docs, build instructions, HTTP API reference,
  configuration matrix and an architecture diagram.
* `docs/bug_before.png` / `docs/bug_after.png` — same payload rendered
  through the old and new code, so the regression and its fix are easy to
  eyeball at a glance.
