#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace Quote {

struct Entity {
    int offset;
    int length;
    std::string type;
    std::string custom_emoji_id;
    std::string url;
};

class TextEngine {
public:
    static std::string processEntities(const std::string& text, const nlohmann::json& entities);
    static bool isOnlyEmoji(const std::string& text);
};

} // namespace Quote
