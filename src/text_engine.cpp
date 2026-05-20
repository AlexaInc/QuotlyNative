#include "text_engine.h"
#include <algorithm>
#include <regex>

namespace Quote {

std::string TextEngine::processEntities(const std::string& text, const nlohmann::json& entities) {
    if (entities.empty()) return text;

    // Sort entities by offset
    std::vector<nlohmann::json> sortedEntities = entities.get<std::vector<nlohmann::json>>();
    std::sort(sortedEntities.begin(), sortedEntities.end(), [](const nlohmann::json& a, const nlohmann::json& b) {
        return a["offset"].get<int>() < b["offset"].get<int>();
    });

    std::string result = "";
    int cursor = 0;

    for (const auto& e : sortedEntities) {
        int offset = e["offset"].get<int>();
        int length = e["length"].get<int>();
        std::string type = e["type"].get<std::string>();

        // Add text before entity
        result += text.substr(cursor, offset - cursor);

        // Apply entity logic matching reference JS
        if (type == "url" || type == "text_url" || type == "mention" || type == "bot_command") {
            // Logic from user feedback: add line break if needed
            // (Simulated plain text check)
            if (!result.empty() && result.back() != '\n' && result.back() != ' ') {
                // Simplified regex-like check for alphanumeric or specific unicode range
                char last = result.back();
                if (isalnum(last)) {
                    result += "\n"; // Equivalent to <br/> in our text engine
                }
            }
        }
        
        if (type == "custom_emoji") {
            // Placeholder for premium emoji rendering
            result += "[Emoji:" + e.value("custom_emoji_id", "0") + "]";
        } else {
            // Add entity text (this would be styled in a full implementation)
            result += text.substr(offset, length);
        }

        cursor = offset + length;
    }

    result += text.substr(cursor);
    return result;
}

bool TextEngine::isOnlyEmoji(const std::string& text) {
    // Basic emoji-only check (placeholder for fuller regex)
    // Telegram considers 1-3 emojis as "Large"
    return false; // To be implemented with emoji regex
}

} // namespace Quote
