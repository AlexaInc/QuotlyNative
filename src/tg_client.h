// this file is part of AlexaInc / QuotlyNative — TG Client
// developer hansaka@alexainc

#pragma once
#include <string>
#include <vector>
#include <memory>
#include <map>
#include "mtproto/client.h"

namespace Quote {

class TgClient {
public:
    TgClient(int apiId, const std::string& apiHash);
    ~TgClient();

    bool authenticate(const std::string& botToken);
    bool isConnected() const { return m_mtproto.is_connected(); }
    
    // Fetch custom emoji as a local file path or buffer
    std::string fetchCustomEmoji(const std::string& emojiId);
    
    // Fetch avatar image
    std::string fetchAvatar(const std::string& userId);

private:
    int m_apiId;
    std::string m_apiHash;
    MTProto::Client m_mtproto;
};

} // namespace Quote
