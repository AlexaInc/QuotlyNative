#include "api_handler.h"
#include "renderer.h"
#include "text_engine.h"
#include <filesystem>
#include <iostream>

namespace Quote {

void ApiHandler::setupRoutes(crow::SimpleApp& app) {
    CROW_ROUTE(app, "/quote").methods(crow::HTTPMethod::POST)(handleQuoteRequest);
}

crow::response ApiHandler::handleQuoteRequest(const crow::request& req) {
    try {
        auto body = nlohmann::json::parse(req.body);
        
        if (!body.contains("messages") || !body["messages"].is_array()) {
            return crow::response(400, "Invalid payload: 'messages' array required");
        }
        
        std::string outputPath = "quote_" + std::to_string(time(NULL)) + ".png";
        
        // Extract shared options
        bool transparent = body.value("transparent", true);
        
        // Final options for the renderer
        std::string firstMsgText = body["messages"][0].value("text", "");
        bool onlyEmoji = TextEngine::isOnlyEmoji(firstMsgText);

        RenderOptions options;
        options.transparent = transparent;
        options.hasBubble = !onlyEmoji;
        
        // Determine grouping/rounding (simplified logic for now)
        // In a real scenario, we'd check previous/next message status
        options.rounding.topLeft = CornerRounding::Large;
        options.rounding.topRight = CornerRounding::Large;
        options.rounding.bottomLeft = CornerRounding::Tail;
        options.rounding.bottomRight = CornerRounding::Large;
        options.isOut = false; // Default to IN for testing
        
        auto entities = body["messages"][0].value("entities", nlohmann::json::array());
        
        std::string processedText = TextEngine::processEntities(firstMsgText, entities);
        
        Renderer::renderQuote(outputPath, processedText, options);
        
        std::ifstream file(outputPath, std::ios::binary);
        if (!file) return crow::response(500, "Failed to read generated quote");
        
        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        
        crow::response res(content);
        res.set_header("Content-Type", "image/png");
        
        std::filesystem::remove(outputPath);
        
        return res;
        
    } catch (const std::exception& e) {
        return crow::response(500, std::string("Internal Error: ") + e.what());
    }
}

} // namespace Quote
