// this file is part of AlexaInc / QuotlyNative — API Handler
// developer hansaka@alexainc

#pragma once
#include <crow.h>
#include <nlohmann/json.hpp>

namespace Quote {

extern void apiLog(const std::string& msg);

class ApiHandler {
public:
    static void setupRoutes(crow::SimpleApp& app);
    static crow::response handleQuoteRequest(const crow::request& req);
};

} // namespace Quote
