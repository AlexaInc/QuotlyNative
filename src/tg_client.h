#pragma once
#include <string>
#include <vector>
#include <memory>
#include <map>

namespace Quote {

class TgClient {
public:
    TgClient(int apiId, const std::string& apiHash);
    ~TgClient();

    bool authenticate(const std::string& botToken);
    
    // Fetch custom emoji as a local file path or buffer
    std::string fetchCustomEmoji(const std::string& emojiId);
    
    // Fetch avatar image
    std::string fetchAvatar(const std::string& userId);

private:
    int m_apiId;
    std::string m_apiHash;
    // TDLib client pointer (opaque for now)
    void* m_client;
};

} // namespace Quote
