// this file is part of AlexaInc / QuotlyNative — TG Client
// developer hansaka@alexainc

#include "tg_client.h"
#include "api_handler.h"
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
    if (!m_mtproto.is_connected()) {
        std::cerr << "[TgClient] Error: Not connected" << std::endl;
        return "";
    }
    uint64_t eid = std::stoull(emojiId);
    std::string cachePath = "/tmp/emoji_" + emojiId + ".png";

    std::cout << "[TgClient] Fetching doc for emoji: " << eid << std::endl;

    // 1. messages.getCustomEmojiDocuments
    MTProto::TLWriter w;
    w.writeInt32(MTProto::TL::messages_getCustomEmojiDocuments);
    w.writeInt32(MTProto::TL::vector);
    w.writeInt32(1);
    w.writeInt64(eid);

    auto res = m_mtproto.invoke(w.data());
    if (!res.ok) {
        apiLog("[TgClient] RPC failed for getCustomEmojiDocuments");
        return "";
    }

    MTProto::TLReader r(res.payload);
    int32_t vecType = r.readInt32();
    if (vecType != MTProto::TL::vector) {
        apiLog("[TgClient] Expected vector (0x1cb5c415), got: 0x" + std::string(1, ' '));
        return "";
    }
    int32_t count = r.readInt32();
    if (count <= 0) {
        apiLog("[TgClient] No documents returned for emojiId");
        return "";
    }

    int32_t docType = r.readInt32();
    if (docType != MTProto::TL::document) {
        apiLog("[TgClient] Expected document (0x8fd1496a), got: 0x" + std::string(1, ' '));
        return "";
    }

    int64_t doc_id          = r.readInt64();
    int64_t doc_access_hash = r.readInt64();
    MTProto::Bytes doc_file_ref = r.readBytes();
    r.readInt32(); // date
    r.readString(); // mime
    r.readInt32(); // size

    apiLog("[TgClient] Found Document ID: " + std::to_string(doc_id));

    // ... (helper logic)
    skipPhotoSizeVector(r);
    skipPhotoSizeVector(r);
    r.readInt32(); // dc_id
    int32_t attrVec = r.readInt32();
    if (attrVec == MTProto::TL::vector) {
        int32_t ac = r.readInt32();
        for (int i=0; i<ac; ++i) r.readInt32();
    }

    apiLog("[TgClient] Downloading document...");
    MTProto::TLWriter fw;
    fw.writeInt32(MTProto::TL::upload_getFile);
    fw.writeInt32(MTProto::TL::inputDocumentFileLocation);
    fw.writeInt64(doc_id);
    fw.writeInt64(doc_access_hash);
    fw.writeBytes(doc_file_ref);
    fw.writeString(""); // thumb_size
    fw.writeInt32(0); // offset
    fw.writeInt32(1024*1024); // limit

    auto fres = m_mtproto.invoke(fw.data());
    if (fres.ok) {
        MTProto::TLReader fr(fres.payload);
        int32_t ftype = fr.readInt32();
        if (ftype == MTProto::TL::upload_file) {
            fr.readInt32(); // type
            fr.readInt32(); // mtime
            MTProto::Bytes b = fr.readBytes();
            apiLog("[TgClient] Downloaded " + std::to_string(b.size()) + " bytes.");
            FILE* f = fopen(cachePath.c_str(), "wb");
            if (f) { fwrite(b.data(), 1, b.size(), f); fclose(f); }
            return cachePath;
        } else {
            apiLog("[TgClient] upload.getFile returned unknown type");
        }
    } else {
        apiLog("[TgClient] upload.getFile RPC failed");
    }

    return "";
}

std::string TgClient::fetchAvatar(const std::string& userId) {
    // Stub: Logic to call photos.getUserPhotos via MTProto
    return "";
}

} // namespace Quote
