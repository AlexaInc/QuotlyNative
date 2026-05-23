# QuotlyNative

A high-performance, native **C++17** rendering service that turns Telegram-
style message payloads into pixel-perfect quote images — bubbles, replies,
media, custom premium emojis, the lot. Built on **Cairo + Pango** for
typography and **Crow** for the HTTP API, with an embedded **MTProto** client
for fetching premium-emoji bitmaps directly from Telegram.

> Drop-in replacement for the JavaScript [`quote-api`](https://github.com/LyoSU/quote-api)
> service, but ~10× faster and an order of magnitude lighter on memory.

---

## ✨ Features

| Capability | Notes |
|---|---|
| Telegram-style chat bubbles | Tail, rounding, grouping, in/out colors |
| Rich text entities | `bold`, `italic`, `underline`, `strikethrough`, `spoiler`, `code`, `pre`, `text_link`, `mention`, `custom_emoji` |
| Premium / custom emojis | Inline glyphs with proper line-breaking + baseline alignment (tdesktop-style); bitmaps fetched on-demand via embedded MTProto client and cached on disk |
| Reply previews | Single-line, ellipsised, with name color and custom-emoji support |
| Inline media | Photos (with caption layout) and stickers (bare, rounded thumbnail) |
| Multi-message threads | Author grouping, avatar shown only on last bubble of a group |
| Avatars | Initials-based fallback colored by Telegram's 7-color palette |
| Transparent PNG output | Switchable via the request payload |
| HTTP API | `POST /quote` and `POST /api/generate` (JS-compat alias) |
| Pre-baked MTProto session | `--gen-auth-key` / `--load-auth-key` to bypass IP-reputation issues on PaaS |

---

## 🩹 Bug fixes in this build

The custom-emoji rendering pipeline has been rewritten end-to-end to mirror
what [tdesktop](https://github.com/telegramdesktop/tdesktop) does in
`lib_ui/ui/text/text_block.cpp`. Specifically:

* **Bitmaps no longer overflow the bubble's right edge.** A custom emoji is
  now a real glyph in the Pango line, so the line-breaker reserves the right
  amount of horizontal space and wraps end-of-line emojis to the next line
  instead of letting them bleed past the bubble.
* **Bitmaps no longer drift above or below the text baseline.** The emoji
  is placed on the line's baseline by Pango itself (just like every other
  glyph), so vertical alignment matches the text exactly — no more "emoji
  riding low" artefacts.
* **Reply previews no longer show orphan emojis past the ellipsis.** Because
  the emoji is a real glyph in the layout, Pango's ellipsizer correctly
  chooses the cutoff *before* the emoji when the line doesn't fit, instead
  of cutting off the text but still rendering the bitmap.

Implementation summary:

| | Before | After |
|---|---|---|
| Custom emoji width reservation | `<span letter_spacing='14336'>` in markup | `pango_attr_shape_new` (real glyph metrics) |
| Vertical placement | Manual centring formula in a second draw pass | Pango baseline placement (no second pass) |
| Painting | Manual loop over `customEmojis` with `pango_layout_index_to_pos` | `pango_cairo_context_set_shape_renderer` callback |
| End-of-line handling | Could overflow; could orphan past ellipsis | Wraps to next line / is hidden by ellipsis |

A before/after comparison rendered from the user-supplied payload:

| `docs/bug_before.png` | `docs/bug_after.png` |
|---|---|
| Emoji bitmaps escape the bubble; reply emoji floats past `…`; vertical drift | All emojis sit inside the bubble; reply emoji is correctly hidden behind the ellipsis; baseline-aligned |

See `CHANGELOG.md` for the long-form write-up.

---

## 🏗 Building

### Dependencies

```bash
sudo apt-get install \
    build-essential cmake pkg-config \
    libcairo2-dev libpango1.0-dev \
    libssl-dev libboost-system-dev zlib1g-dev \
    nlohmann-json3-dev
# Crow is header-only — drop crow.h into /usr/local/include or vendor it.
```

### Compile

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

You'll get a single `QuoteAPI` binary.

### Docker

```bash
docker build -t quotlynative .
docker run --rm -p 7860:7860 \
    -e TG_API_ID=12345 -e TG_API_HASH=deadbeef...32hex \
    -e BOT_TOKEN=123:abc \
    quotlynative
```

A `Dockerfile.hf` is also provided, pre-tuned for **Hugging Face Spaces**.

---

## ⚙️ Configuration

| Env var | Required | Description |
|---|---|---|
| `TG_API_ID` | for premium emojis | From <https://my.telegram.org> |
| `TG_API_HASH` | for premium emojis | 32-hex string from the same page |
| `BOT_TOKEN` | optional | From [@BotFather](https://t.me/BotFather); enables bot-context emoji fetching |
| `PORT` | optional | HTTP listen port (default **7860**) |

Without TG credentials the server still starts and renders **everything
except** premium emojis (in that case the placeholder character is shown as
whatever your system emoji font provides — better than a blank box).

---

## 🚀 HTTP API

### `POST /quote` (alias: `POST /api/generate`)

Request body:

```jsonc
{
  "transparent": true,
  "messages": [
    {
      "text": "Hello! Check out these premium emojis ✨🔥",
      "entities": [
        { "offset": 0,  "length": 5,  "type": "bold" },
        { "offset": 38, "length": 1,  "type": "custom_emoji", "custom_emoji_id": "5210956306952758910" },
        { "offset": 39, "length": 2,  "type": "custom_emoji", "custom_emoji_id": "5233605022419270727" }
      ],
      "from": {
        "id": 8749605964,
        "first_name": "Alexa",
        "last_name": "User",
        "emoji_status_custom_emoji_id": "5210956306952758910"
      },
      "reply_to": {
        "text": "Original message",
        "from": { "id": 998877665, "first_name": "Replier" }
      }
    }
  ]
}
```

Response: `image/png` binary.

#### Field reference

* **`text`** *(string)* — Message text. Aliased as `message`.
* **`entities`** *(array)* — Telegram-style entity list. Offsets are **UTF-16
  code units** (same as the Bot API). Supported `type` values:
  `bold`, `italic`, `underline`, `strikethrough`, `spoiler`, `code`, `pre`,
  `text_link`, `url`, `mention`, `custom_emoji`.
* **`from.id`** *(int)* — Telegram user id; used as the grouping key and to
  pick a name color.
* **`from.first_name` / `from.last_name`** — Displayed as `"First Last"`.
* **`from.emoji_status_custom_emoji_id`** *(string)* — Drawn at 20 px next to
  the sender name.
* **`reply_to.text` / `reply_to.from` / `reply_to.entities`** — Renders a
  single-line reply preview with accent bar in the sender's name color.
* **`mediaBase64`** *(`data:image/...;base64,...`)* — Inline photo or sticker.
* **`mediaType`** *(string)* — `"photo"` or `"sticker"`. Inferred from
  `mediaBase64` MIME if omitted.
* **`avatarBase64`** — Reserved (renderer currently uses initials only).
* **`transparent`** *(bool, top-level)* — Set to `false` for an opaque
  background.

#### Multi-message grouping

Consecutive messages with the same `from.id` are grouped. The sender name
is shown only on the **first** bubble of a group; the avatar only on the
**last**. Bubble corner rounding follows Telegram's "soft top / sharp middle
/ tail-or-rounded bottom" convention automatically.

### `GET /debug/logs`

Returns the last 25 KB of structured API logs (useful in Docker / HF Spaces).

---

## 🧩 Architecture

```
                    ┌─────────────┐
HTTP (Crow) ────►   │ ApiHandler  │  parses JSON, decodes base64 media,
                    └──────┬──────┘  resolves custom-emoji ids
                           │
                ┌──────────┴──────────┐
                │                     │
       ┌────────▼─────────┐  ┌────────▼─────────┐
       │   TextEngine     │  │   TgClient       │
       │ entities → Pango │  │ MTProto fetch    │
       │ markup           │  │ premium emojis   │
       └────────┬─────────┘  └────────┬─────────┘
                │                     │ (cached PNGs)
                └──────────┬──────────┘
                           │
                    ┌──────▼──────────────────────────────┐
                    │  Renderer (Cairo + Pango)           │
                    │                                     │
                    │   ┌──────────────────────────────┐  │
                    │   │ attachCustomEmojiShapes()    │  │
                    │   │ ─ adds pango_attr_shape per  │  │
                    │   │   custom emoji, so emojis    │  │
                    │   │   are real glyphs in the     │  │
                    │   │   line (width reserved,      │  │
                    │   │   wrapping handled, baseline │  │
                    │   │   alignment automatic)       │  │
                    │   └──────────────┬───────────────┘  │
                    │                  ▼                  │
                    │   pango_cairo_show_layout(…)        │
                    │   ─ invokes shapeRenderer()         │
                    │     for each emoji shape, which     │
                    │     blits the cached bitmap into    │
                    │     the reserved box.               │
                    └─────────────────────────────────────┘
```

* **`src/api_handler.cpp`** — HTTP entrypoint; JSON parsing; base64 decoding;
  temp-file lifecycle.
* **`src/text_engine.cpp`** — Converts Telegram entities to Pango markup
  (bold/italic/underline/…), with UTF-16 → UTF-8 offset translation. **Does
  not** emit any markup for `custom_emoji` entities — those are handled
  natively in `renderer.cpp` via `pango_attr_shape_new`.
* **`src/renderer.cpp`** — All Cairo drawing: bubbles, tails, avatars,
  photos, stickers, reply previews, and custom-emoji-as-glyph plumbing.
* **`src/style_constants.h`** — All visual constants (radii, paddings,
  colors, emoji sizes) extracted from `tdesktop` source.
* **`src/tg_client.cpp` + `src/mtproto/*`** — Embedded MTProto client used to
  download `documentClassic` files for `custom_emoji_id` ids.
* **`src/main.cpp`** — CLI parsing, credentials validation, Crow boot.

### Why `pango_attr_shape_new` ?

Pango is unaware of our emoji bitmaps. If we ask it to lay out
`"Hello ✨ world"` it will shape the ✨ using whatever the system emoji font
produces — that's a glyph of *some* size, not necessarily the size of the
bitmap we want to paint.

`pango_attr_shape_new` is Pango's officially supported escape hatch for that
exact mismatch: it tells Pango *"for these N bytes, treat the run as a
single shaped glyph with these exact ink + logical extents"*. Pango then:

* breaks lines correctly (the shape's logical width is what the line-breaker
  budgets against the layout width),
* aligns vertically on the baseline (the shape's `ink_rect.y` is the offset
  from baseline, negative = above),
* applies ellipsization correctly (the shape participates in the
  end-of-line truncation decision).

At render time, `pango_cairo_context_set_shape_renderer` installs a callback
that Pango will invoke for every shape attribute it encounters — with the
cairo *current point* already moved to the glyph's baseline origin. We just
blit the cached PNG inside the reserved box.

This is mechanically the same as what tdesktop does with its
`CustomEmojiBlock`, just expressed in Pango's vocabulary instead of Qt's.

---

## 🧪 Testing

```bash
# Render a complex payload to /tmp/out.png and diff against the golden
curl -s -X POST http://localhost:7860/quote \
     -H 'Content-Type: application/json' \
     --data @tests/complex_test.json \
     -o /tmp/out.png

python3 tests/verify_api.py
```

The repository also ships a few pre-rendered reference PNGs at the root
(`test_Full_Premium.png`, `test_Multi_Message_Mixed.png`, …) so you can
eyeball regressions without running the server.

---

## 📝 License

Licensed under the project's existing license terms.
Original author: **hansaka@alexainc** (see `// developer hansaka@alexainc`
headers throughout `src/`).

---

## 🙋 Troubleshooting

* **`-404` errors from Telegram during handshake** — your egress IP is
  rate-limited (common on Hugging Face Spaces). Run
  `./QuoteAPI --gen-auth-key auth.bin` once on a clean residential IP, then
  ship `auth.bin` and start the server with
  `./QuoteAPI --load-auth-key auth.bin`.
* **Premium emojis render as blank boxes** — the bitmap fetch failed (check
  `/debug/logs`). Pango still reserved the correct horizontal space, so
  layout is unaffected — the box just isn't filled in.
* **Premium emojis render as monochrome system glyphs** — `emojiMap` doesn't
  contain the `documentId`. No shape attribute is added for that emoji, so
  Pango falls back to your OS emoji font (Noto Color Emoji, Twemoji, …).
  Fix the MTProto fetch and the bitmap will replace the fallback.
* **Text overflows the bubble** — should not happen after this patch; please
  file an issue with the offending payload.
