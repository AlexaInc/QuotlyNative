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
    uint64_t eid = std::stoull(emojiId);
    
    // 1. Check Cache
    std::string cachePath = "/tmp/emoji_" + emojiId + ".png";
    // (Actual file checking would go here, for now we re-download for verification)

    // 2. messages.getCustomEmojiDocuments document_id:Vector<long>
    MTProto::TLWriter w;
    w.writeInt32(MTProto::TL::messages_getCustomEmojiDocuments);
    w.writeInt32(MTProto::TL::vector);
    w.writeInt32(1);
    w.writeInt64(eid);

    auto res = m_mtproto.invoke(w.data());
    if (!res.ok) return "";

    MTProto::TLReader r(res.payload);
    if (r.readInt32() != MTProto::TL::vector) return "";
    if (r.readInt32() <= 0) return "";
    if (r.readInt32() != MTProto::TL::document) return "";

    int64_t doc_id        = r.readInt64();
    int64_t doc_access_hash = r.readInt64();
    MTProto::Bytes doc_file_ref = r.readBytes();
    r.readInt32(); // date
    r.readString(); // mime
    r.readInt32(); // size

    // 3. Parse Thumbs vector
    if (r.readInt32() != MTProto::TL::vector) return "";
    int32_t thumbCount = r.readInt32();
    
    int64_t target_id = doc_id;
    int64_t target_ah = doc_access_hash;
    MTProto::Bytes target_ref = doc_file_ref;
    std::string target_type = "";
    bool isThumb = false;

    for (int i=0; i<thumbCount; ++i) {
        int32_t tcid = r.readInt32();
        if (tcid == MTProto::TL::photoSize) {
            std::string type = r.readString();
            r.readInt32(); r.readInt32(); r.readInt32(); r.readInt32(); // location fields
            r.readInt32(); r.readInt32(); // w, h
            r.readInt32(); // total size
            if (target_type == "") { target_type = type; isThumb = true; }
        } else if (tcid == MTProto::TL::photoCachedSize) {
            std::string type = r.readString();
            r.readInt32(); r.readInt32(); r.readInt32(); r.readInt32(); // location fields
            r.readInt32(); r.readInt32(); // w, h
            MTProto::Bytes b = r.readBytes();
            if (!b.empty()) {
                // We got it! Save and return
                FILE* f = fopen(cachePath.c_str(), "wb");
                if (f) { fwrite(b.data(), 1, b.size(), f); fclose(f); }
                return cachePath;
            }
        } else { /* skip unknown */ }
    }

    // 4. upload.getFile location:InputDocumentFileLocation offset:0 limit:1MB
    MTProto::TLWriter fw;
    fw.writeInt32(MTProto::TL::upload_getFile);
    fw.writeInt32(MTProto::TL::inputDocumentFileLocation);
    fw.writeInt64(doc_id);
    fw.writeInt64(doc_access_hash);
    fw.writeBytes(doc_file_ref);
    fw.writeString(""); // thumb_size empty = full doc
    fw.writeInt32(0); // offset
    fw.writeInt32(1024*1024); // limit

    auto fres = m_mtproto.invoke(fw.data());
    if (fres.ok) {
        MTProto::TLReader fr(fres.payload);
        if (fr.readInt32() == MTProto::TL::upload_file) {
            fr.readInt32(); // type
            fr.readInt32(); // mtime
            MTProto::Bytes b = fr.readBytes();
            FILE* f = fopen(cachePath.c_str(), "wb");
            if (f) { fwrite(b.data(), 1, b.size(), f); fclose(f); }
            return cachePath;
        }
    }

    return "";
}

std::string TgClient::fetchAvatar(const std::string& userId) {
    // Stub: Logic to call photos.getUserPhotos via MTProto
    return "";
}

} // namespace Quote
