#pragma once
#include <crow.h>
#include <nlohmann/json.hpp>

namespace Quote {

class ApiHandler {
public:
    static void setupRoutes(crow::SimpleApp& app);
    static crow::response handleQuoteRequest(const crow::request& req);
};

} // namespace Quote
