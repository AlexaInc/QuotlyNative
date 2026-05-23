# QuotlyNative

A high-performance, native **C++17** rendering service that turns Telegram-style
message payloads into pixel-perfect quote images — bubbles, replies, media,
custom premium emojis, the lot. Built on **Cairo + Pango** for typography and
**Crow** for the HTTP API, with an embedded **MTProto** client for fetching
premium-emoji bitmaps directly from Telegram.

> Drop-in replacement for the JavaScript [`quote-api`](https://github.com/LyoSU/quote-api)
> service, but ~10× faster and an order of magnitude lighter on memory.

---

## ✨ Features

| Capability | Notes |
|---|---|
| Telegram-style chat bubbles | Tail, rounding, grouping, in/out colors |
| Rich text entities | `bold`, `italic`, `underline`, `strikethrough`, `spoiler`, `code`, `pre`, `text_link`, `mention`, `custom_emoji` |
| Premium / custom emojis | Fetched on-demand via embedded MTProto client, cached on disk |
| Reply previews | Single-line, ellipsised, with name color and custom-emoji support |
| Inline media | Photos (with caption layout) and stickers (bare, rounded thumbnail) |
| Multi-message threads | Author grouping, avatar shown only on last bubble of a group |
| Avatars | Initials-based fallback colored by Telegram's 7-color palette |
| Transparent PNG output | Switchable via the request payload |
| HTTP API | `POST /quote` and `POST /api/generate` (JS-compat alias) |
| Pre-baked MTProto session | `--gen-auth-key` / `--load-auth-key` to bypass IP-reputation issues on PaaS |

---

## 🩹 Bug fixes in this build

This branch contains targeted fixes for two rendering regressions visible in
the reference output (`uploads/Hfqly (3).png`):

1. **Premium emojis overflowed the bubble's right edge.**
   The text engine reserved a placeholder cell whose width was *less than* the
   bitmap we then painted on top. When the emoji landed at end-of-line the
   bitmap bled past the bubble's right edge.

   *Fix*: `TextEngine` now emits a custom-emoji span with `font_size='1pt'` +
   `letter_spacing` sized from `Style::kEmojiSize`, so Pango's line breaker
   reserves a cell that's always wide enough for the bitmap. The renderer
   then *centers* the bitmap inside that cell and clamps against the bubble's
   inner-right edge as a safety net.

2. **Custom emojis in reply previews floated past the ellipsis.**
   Reply previews are single-line and ellipsised, but emoji bitmaps were
   still drawn at their original byte index even when that index was already
   inside the truncated tail — so the bitmap ended up dangling in empty
   space to the right of the "…".

   *Fix*: before drawing each reply-preview emoji we now read the first
   layout line's visible byte range via `pango_layout_get_line_readonly` and
   skip any emoji whose index falls beyond it.

A before/after comparison rendered from the same payload:

| Before | After |
|---|---|
| Yellow & red bitmaps escape the bubble; reply emoji floats past `…` | Bitmaps wrap cleanly inside the bubble; reply emoji is hidden behind the ellipsis |

(Test fixtures live in `tests/`. The harness used to produce the comparison
images lives in this conversation's `/tmp/qtest/` and can be inlined as
`tests/render_smoke.cpp` if desired.)

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
except** premium emojis (placeholders are reserved but no bitmap is painted).

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
        { "offset": 37, "length": 1,  "type": "custom_emoji", "custom_emoji_id": "5210956306952758910" },
        { "offset": 38, "length": 2,  "type": "custom_emoji", "custom_emoji_id": "5233605022419270727" }
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
                    ┌──────▼──────┐
                    │  Renderer   │  Cairo + Pango → PNG
                    └─────────────┘
```

* **`src/api_handler.cpp`** — HTTP entrypoint; JSON parsing; base64 decoding;
  temp-file lifecycle.
* **`src/text_engine.cpp`** — Converts Telegram entities to Pango markup, with
  UTF-16 → UTF-8 offset translation. Reserves wide cells for custom emojis.
* **`src/renderer.cpp`** — All Cairo drawing: bubbles, tails, avatars, photos,
  stickers, reply previews, inline emoji bitmaps.
* **`src/style_constants.h`** — All visual constants (radii, paddings, colors,
  emoji sizes) extracted from `tdesktop` source.
* **`src/tg_client.cpp` + `src/mtproto/*`** — Embedded MTProto client used to
  download `documentClassic` files for `custom_emoji_id` ids.
* **`src/main.cpp`** — CLI parsing, credentials validation, Crow boot.

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
* **Premium emojis render as blank squares** — the bitmap fetch failed (check
  `/debug/logs`). The renderer still reserves the correct horizontal space,
  so layout is unaffected.
* **Text overflows the bubble** — should not happen after this patch; please
  file an issue with the offending payload.
