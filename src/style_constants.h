// this file is part of AlexaInc / QuotlyNative — Style Constants
// developer hansaka@alexainc
// Values extracted from tdesktop sources:
//   Telegram/SourceFiles/ui/chat/message_bubble.cpp
//   Telegram/SourceFiles/ui/chat/chat_style.h

#pragma once
#include <string>
#include <array>

namespace Quote {
namespace Style {

// ── Bubble Corner Radii (from Images::CornersMask / BubbleRadiusSmall/Large) ─
constexpr double kBubbleRadiusSmall  = 6.0;
constexpr double kBubbleRadiusLarge  = 10.0;

// ── Message Max/Min Width ────────────────────────────────────────────────────
// Telegram Desktop: msgMaxWidth = 430 dp (approx), msgMinWidth = 160 dp
constexpr double kMsgMaxWidth  = 430.0;
constexpr double kMsgMinWidth  = 160.0;

// ── Padding (from Telegram msgPadding) ───────────────────────────────────────
constexpr double kPadLeft   = 12.0;
constexpr double kPadRight  = 10.0;
constexpr double kPadTop    =  7.0;
constexpr double kPadBottom =  8.0;

// ── Name area ────────────────────────────────────────────────────────────────
constexpr double kNamePadTop    = 4.0;
constexpr double kNamePadBottom = 2.0;
constexpr double kNameFontSize  = 13.0;

// ── Body text ────────────────────────────────────────────────────────────────
constexpr double kTextFontSize = 14.0;

// ── Font family fallback list ────────────────────────────────────────────────
// The order matters and is deliberate:
//   1. Inter            — the intended UI face (Latin), when installed.
//   2. Noto Sans        — deterministic Latin fallback (also what Inter
//                         metrics were designed against).
//   3. Noto Sans Sinhala — MUST be listed explicitly. Without it, fontconfig
//                         resolves Sinhala codepoints to the legacy "LKLUG"
//                         font, which breaks conjuncts (න්‍ය, ක්‍ෂ …) and adds
//                         wide gaps around ZWJ clusters at chat sizes.
// Emoji are intentionally NOT listed here: putting "Noto Color Emoji" in the
// family list makes fontconfig hijack ASCII digits for the emoji font. Pango's
// global fallback already routes 🙂/✨ to the color emoji font correctly.
const std::string kFontFamily = "Inter, Noto Sans, Noto Sans Sinhala";

// ── Emoji status (premium badge next to the sender name) ────────────────────
constexpr double kEmojiStatusSize = 20.0;
constexpr double kEmojiStatusGap  = 4.0;

// ── Timestamp (bottom-right, Telegram style) ─────────────────────────────────
constexpr double kTimeFontSize = 11.0;
constexpr double kTimeGap      = 6.0;  // horizontal gap when inline with text

// ── Avatar ───────────────────────────────────────────────────────────────────
constexpr double kAvatarSize       = 42.0;
constexpr double kAvatarMarginRight = 8.0;
constexpr double kAvatarMarginTop   = 0.0; // top-aligned with bubble

// ── Shadow (from msgShadow) ──────────────────────────────────────────────────
constexpr double kMsgShadow = 1.0;

// ── Canvas Outer Padding ─────────────────────────────────────────────────────
constexpr double kCanvasPad = 8.0;

// ── Tail geometry ────────────────────────────────────────────────────────────
constexpr double kTailWidth  = 9.0;
constexpr double kTailHeight = 20.0;

// ── Emoji Sizes ──────────────────────────────────────────────────────────────
constexpr double kEmojiSize      = 20.0;
constexpr double kEmojiSizeLarge = 40.0;
constexpr double kEmojiSizeHuge  = 64.0;

// ── Colors (Dark Theme — matches Telegram Desktop dark) ──────────────────────
const std::string kColorInBg      = "#182533";   // msgInBg dark mode
const std::string kColorOutBg     = "#2B5278";   // msgOutBg dark mode
const std::string kColorInText    = "#F5F5F5";
const std::string kColorOutText   = "#F5F5F5";
const std::string kColorInLink    = "#62BCF9";
const std::string kColorOutLink   = "#B0D6F5";
const std::string kColorInShadow  = "#00000040";

// ── Sender Name Colors (Telegram's 7-color scheme for group chats) ───────────
const std::array<std::string, 7> kNameColors = {
    "#FC5C51",  // Red
    "#FA790F",  // Orange
    "#895DD5",  // Purple
    "#0FB297",  // Green
    "#27A8E7",  // Blue
    "#FF5DA2",  // Pink
    "#FAC52D",  // Yellow
};

} // namespace Style
} // namespace Quote
