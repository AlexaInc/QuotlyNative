// this file is part of AlexaInc / QuotlyNative — Main Entry Point
// developer hansaka@alexainc

#include "api_handler.h"
#include "tg_client.h"
#include <crow.h>
#include <iostream>
#include <cstdlib>

int main() {
    crow::SimpleApp app;
    
    // Auto-read credentials from environment
    const char* apiIdEnv = std::getenv("TG_API_ID");
    const char* apiHashEnv = std::getenv("TG_API_HASH");
    const char* botTokenEnv = std::getenv("BOT_TOKEN");
    
    if (apiIdEnv && apiHashEnv) {
        int apiId = std::stoi(apiIdEnv);
        std::string apiHash = apiHashEnv;
        auto tgClient = std::make_shared<Quote::TgClient>(apiId, apiHash);
        
        if (botTokenEnv) {
            tgClient->authenticate(botTokenEnv);
        }
        std::cout << "✅ TG credentials loaded from environment" << std::endl;
    } else {
        std::cout << "⚠️  TG credentials missing in environment (TG_API_ID, TG_API_HASH)" << std::endl;
    }
    
    Quote::ApiHandler::setupRoutes(app);
    
    // Get port from environment or default to 7860 (Hugging Face)
    uint16_t port = 7860;
    const char* envPort = std::getenv("PORT");
    if (envPort) {
        port = static_cast<uint16_t>(std::stoi(envPort));
    }
    
    std::cout << "🚀 QuotlyNative listening on port " << port << std::endl;
    app.port(port).multithreaded().run();
    
    return 0;
}
