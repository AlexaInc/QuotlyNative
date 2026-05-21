// this file is part of AlexaInc / QuotlyNative — API Handler
// developer hansaka@alexainc

#include "api_handler.h"
#include "renderer.h"
#include "text_engine.h"
#include <filesystem>
#include <iostream>
#include <fstream>

namespace Quote {

void ApiHandler::setupRoutes(crow::SimpleApp& app) {
    CROW_ROUTE(app, "/quote").methods(crow::HTTPMethod::POST)(handleQuoteRequest);
}

crow::response ApiHandler::handleQuoteRequest(const crow::request& req) {
    try {
        auto body = nlohmann::json::parse(req.body);

        if (!body.contains("messages") || !body["messages"].is_array() || body["messages"].empty()) {
            return crow::response(400, "Invalid payload: 'messages' array required");
        }

        std::string outputPath = "/tmp/quote_" + std::to_string(time(NULL)) + "_" +
                                  std::to_string(rand()) + ".png";

        bool transparent = body.value("transparent", true);

        const auto& firstMsg = body["messages"][0];

        // ── Build MessageData from JSON ──────────────────────────────────────
        MessageData msg;
        msg.text = firstMsg.value("text", "");
        msg.isOutgoing = false;

        // Extract sender info
        if (firstMsg.contains("from")) {
            const auto& from = firstMsg["from"];
            std::string firstName = from.value("first_name", "");
            std::string lastName  = from.value("last_name", "");
            msg.senderName = firstName;
            if (!lastName.empty()) msg.senderName += " " + lastName;
            msg.senderId = from.value("id", 0);
        }

        // Process entities → Pango markup
        auto entities = firstMsg.value("entities", nlohmann::json::array());
        msg.pangoMarkup = TextEngine::processEntities(msg.text, entities);

        // ── RenderOptions ────────────────────────────────────────────────────
        RenderOptions options;
        options.transparent = transparent;
        options.hasBubble = !TextEngine::isOnlyEmoji(msg.text);

        options.rounding.topLeft     = CornerRounding::Large;
        options.rounding.topRight    = CornerRounding::Large;
        options.rounding.bottomLeft  = CornerRounding::Tail;
        options.rounding.bottomRight = CornerRounding::Large;

        // ── Render ───────────────────────────────────────────────────────────
        Renderer::renderQuote(outputPath, msg, options);

        std::ifstream file(outputPath, std::ios::binary);
        if (!file) return crow::response(500, "Failed to read generated quote");

        std::string content((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());

        crow::response res(content);
        res.set_header("Content-Type", "image/png");

        std::filesystem::remove(outputPath);

        return res;

    } catch (const std::exception& e) {
        return crow::response(500, std::string("Internal Error: ") + e.what());
    }
}

} // namespace Quote
