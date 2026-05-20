#include "api_handler.h"
#include <crow.h>

int main() {
    crow::SimpleApp app;
    
    Quote::ApiHandler::setupRoutes(app);
    
    app.port(7860).multithreaded().run();
    
    return 0;
}
