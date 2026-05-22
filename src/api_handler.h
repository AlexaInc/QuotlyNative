// this file is part of AlexaInc / QuotlyNative — API Handler
// developer hansaka@alexainc

#pragma once
#include <crow.h>
#include <nlohmann/json.hpp>
#include <memory>

namespace Quote { class TgClient; }


namespace Quote {

extern void apiLog(const std::string& msg);

class ApiHandler {
public:
    static void setTgClient(const std::shared_ptr<TgClient>& tgClient);
    static void setupRoutes(crow::SimpleApp& app);
    static crow::response handleQuoteRequest(const crow::request& req);
};

} // namespace Quote
