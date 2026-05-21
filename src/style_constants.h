// this file is part of AlexaInc / QuotlyNative — Style Constants
// developer hansaka@alexainc

#pragma once
#include <string>

namespace Quote {
namespace Style {

// Bubble Radii (from TDesktop chat.style)
constexpr int kBubbleRadiusSmall = 10; // Guessed from roundRadiusLarge context
constexpr int kBubbleRadiusLarge = 16; 

// Emojis
constexpr double kEmojiSize = 20.0;
constexpr double kEmojiSizeLarge = 40.0;
constexpr double kEmojiSizeHuge = 64.0;

// Shadows
constexpr int kMsgShadow = 1;

// Padding (approximate from TDesktop msgPadding)
struct Padding {
    int left = 12;
    int top = 8;
    int right = 10;
    int bottom = 8;
};
const Padding kMsgPadding = {12, 8, 10, 8};

// Colors (Default Dark Theme)
const std::string kColorInBg = "#18222d";
const std::string kColorOutBg = "#2b5278";
const std::string kColorInText = "#ffffff";
const std::string kColorOutText = "#ffffff";
const std::string kColorInLink = "#62bcf9";
const std::string kColorOutLink = "#b0d6f5";
const std::string kColorInShadow = "#00000040";

// Tail path (Approximate geometry for pixel-perfect feel)
// This is a SVG-style path data
const std::string kTailPathIn = "M0,0 L8,0 L8,10 Q8,0 0,0 Z"; // Simplified for now, will refine
const std::string kTailPathOut = "M8,0 L0,0 L0,10 Q0,0 8,0 Z";

} // namespace Style
} // namespace Quote
