// this file is part of AlexaInc / QuotlyNative — TG Client
// developer hansaka@alexainc

#include "tg_client.h"
#include <iostream>

namespace Quote {

TgClient::TgClient(int apiId, const std::string& apiHash) 
    : m_apiId(apiId), m_apiHash(apiHash) {
    // Client is constructed automatically
}

TgClient::~TgClient() {
    m_mtproto.disconnect();
}

bool TgClient::authenticate(const std::string& botToken) {
    if (m_apiId <= 0 || m_apiHash.empty() || botToken.empty()) {
        std::cerr << "TgClient: Missing credentials." << std::endl;
        return false;
    }
    // Connect to DC 2 by default
    return m_mtproto.connect(botToken, 2);
}

std::string TgClient::fetchCustomEmoji(const std::string& emojiId) {
    // Stub: Logic to call messages.getCustomEmojiDocuments via MTProto
    return "";
}

std::string TgClient::fetchAvatar(const std::string& userId) {
    // Stub: Logic to call photos.getUserPhotos via MTProto
    return "";
}

} // namespace Quote
