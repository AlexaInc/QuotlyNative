# Changelog

## Unreleased — Premium-emoji layout fix

### Fixed
* **Premium / custom emojis no longer overflow the bubble's right edge.**
  The text engine was reserving a Pango layout cell of `glyph_advance + 14px`
  for each custom-emoji entity, but the renderer paints the bitmap at **22 px**
  (16 px in reply previews). When an emoji landed at end-of-line the bitmap
  bled past the bubble — clearly visible on bubbles like *"Hello! Check out
  these premium emojis 👀🔥"* in the reference render.

  *src/text_engine.cpp*: introduced `customEmojiSpanOpen()` which emits
  `<span font_size='1pt' alpha='1' fallback='false' letter_spacing='N'>` with
  `N` derived from `Style::kEmojiSize + 4 px` (in Pango units). The shrunken
  placeholder glyph plus inflated letter-spacing make Pango reserve a cell
  that's always wide enough for the bitmap, so the line-breaker now correctly
  wraps or grows the bubble.

  *src/renderer.cpp*: emoji bitmaps are now **centered** inside the reserved
  cell (`ex = pos.x + (cell − emojiSize)/2`), with a final safety clamp
  against `bubbleX + bubbleW − kPadRight`.

* **Custom emojis in reply previews are no longer drawn past the ellipsis.**
  Reply previews are ellipsised single lines (`PANGO_ELLIPSIZE_END`), but
  emoji bitmaps were still painted at their original byte index. When the
  index fell inside the truncated tail, the bitmap floated in empty space to
  the right of the "…" — clearly visible on the *"Quoted message with bold
  and a link and custom …🤣"* preview.

  *src/renderer.cpp*: `drawReply` now reads the first layout line's visible
  byte range via `pango_layout_get_line_readonly(tl, 0)` and skips any emoji
  whose `byteIndex >= line->start_index + line->length`. An additional
  right-edge clamp catches any residual overflow.

### Added
* New `README.md` documenting the build, configuration, HTTP API, payload
  schema, architecture, and troubleshooting.
* This `CHANGELOG.md`.

### Unchanged
* All other source files (`api_handler.cpp/h`, `main.cpp`, `tg_client.cpp/h`,
  `mtproto/*`, `style_constants.h`, `text_engine.h`, `renderer.h`,
  `CMakeLists.txt`, `Dockerfile*`, tests, prebuilt PNGs) were **not modified**.
