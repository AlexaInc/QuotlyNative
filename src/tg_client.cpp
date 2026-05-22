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
    if (!m_mtproto.is_connected()) return "";
    
    // 1. messages.getCustomEmojiDocuments document_id:Vector<long>
    MTProto::TLWriter w;
    w.writeInt32(MTProto::TL::messages_getCustomEmojiDocuments);
    w.writeInt32(MTProto::TL::vector);
    w.writeInt32(1); // count=1
    w.writeInt64(std::stoull(emojiId));

    auto res = m_mtproto.invoke(w.data());
    if (!res.ok) return "";

    // 2. Parse Document from Vector<Document>
    MTProto::TLReader r(res.payload);
    int32_t cid = r.readInt32();
    if (cid != MTProto::TL::vector) return "";
    int32_t count = r.readInt32();
    if (count <= 0) return "";

    // Parse Document object (simplified)
    int32_t doc_cid = r.readInt32();
    if (doc_cid != MTProto::TL::document) return "";
    
    int64_t id = r.readInt64();
    int64_t access_hash = r.readInt64();
    Bytes file_reference = r.readBytes();
    // ... we need more fields but for now let's just get the ID
    
    // 3. upload.getFile location:InputFileLocation offset:int limit:int
    // (Note: This needs a full TL implementation of InputDocumentFileLocation)
    return "placeholder:" + emojiId;
}

std::string TgClient::fetchAvatar(const std::string& userId) {
    // Stub: Logic to call photos.getUserPhotos via MTProto
    return "";
}

} // namespace Quote
