# Changelog

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
