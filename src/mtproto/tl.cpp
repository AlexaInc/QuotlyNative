// this file is part of AlexaInc / QuotlyNative — TL Serialization
// developer hansaka@alexainc

#include "tl.h"
#include <stdexcept>
#include <cstring>

namespace MTProto {

// ── TLWriter ──────────────────────────────────────────────────────────────────

void TLWriter::writeInt32(int32_t v) {
    uint32_t u = static_cast<uint32_t>(v);
    m_data.push_back(u & 0xff);
    m_data.push_back((u >> 8) & 0xff);
    m_data.push_back((u >> 16) & 0xff);
    m_data.push_back((u >> 24) & 0xff);
}

void TLWriter::writeInt64(int64_t v) {
    uint64_t u = static_cast<uint64_t>(v);
    for (int i = 0; i < 8; ++i)
        m_data.push_back((u >> (8 * i)) & 0xff);
}

void TLWriter::writeInt128(const uint8_t* v) {
    m_data.insert(m_data.end(), v, v + 16);
}

void TLWriter::writeInt256(const uint8_t* v) {
    m_data.insert(m_data.end(), v, v + 32);
}

// TL byte-string: if len < 254, 1-byte prefix; else 4-byte (0xfe + 3-byte LE len)
// Then padded to 4-byte alignment
void TLWriter::writeBytes(const Bytes& b) {
    size_t len = b.size();
    if (len < 254) {
        m_data.push_back(static_cast<uint8_t>(len));
    } else {
        m_data.push_back(0xfe);
        m_data.push_back(len & 0xff);
        m_data.push_back((len >> 8) & 0xff);
        m_data.push_back((len >> 16) & 0xff);
    }
    m_data.insert(m_data.end(), b.begin(), b.end());
    // pad to 4-byte alignment
    size_t used = (len < 254) ? (1 + len) : (4 + len);
    while (used % 4 != 0) { m_data.push_back(0); ++used; }
}

void TLWriter::writeString(const std::string& s) {
    Bytes b(s.begin(), s.end());
    writeBytes(b);
}

// Writes a 32-bit unsigned value in BIG-ENDIAN (used for p, q in DH params)
void TLWriter::writeBigEndian32(uint32_t v) {
    Bytes b(4);
    b[0] = (v >> 24) & 0xff;
    b[1] = (v >> 16) & 0xff;
    b[2] = (v >> 8)  & 0xff;
    b[3] =  v        & 0xff;
    // Strip leading zeros for TL bytes field
    size_t start = 0;
    while (start < 3 && b[start] == 0) ++start;
    Bytes stripped(b.begin() + start, b.end());
    writeBytes(stripped);
}

// ── TLReader ──────────────────────────────────────────────────────────────────

TLReader::TLReader(const Bytes& data, size_t offset)
    : m_data(data), m_pos(offset) {}

int32_t TLReader::readInt32() {
    if (m_pos + 4 > m_data.size()) throw std::runtime_error("TLReader: buffer underflow (int32)");
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i)
        v |= static_cast<uint32_t>(m_data[m_pos++]) << (8 * i);
    return static_cast<int32_t>(v);
}

int64_t TLReader::readInt64() {
    if (m_pos + 8 > m_data.size()) throw std::runtime_error("TLReader: buffer underflow (int64)");
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i)
        v |= static_cast<uint64_t>(m_data[m_pos++]) << (8 * i);
    return static_cast<int64_t>(v);
}

Bytes TLReader::readInt128() {
    if (m_pos + 16 > m_data.size()) throw std::runtime_error("TLReader: buffer underflow (int128)");
    Bytes out(m_data.begin() + m_pos, m_data.begin() + m_pos + 16);
    m_pos += 16;
    return out;
}

Bytes TLReader::readInt256() {
    if (m_pos + 32 > m_data.size()) throw std::runtime_error("TLReader: buffer underflow (int256)");
    Bytes out(m_data.begin() + m_pos, m_data.begin() + m_pos + 32);
    m_pos += 32;
    return out;
}

Bytes TLReader::readBytes() {
    if (m_pos >= m_data.size()) throw std::runtime_error("TLReader: buffer underflow (bytes header)");
    size_t len;
    size_t header_size;
    if (m_data[m_pos] < 254) {
        len = m_data[m_pos++];
        header_size = 1;
    } else if (m_data[m_pos] == 0xfe) {
        ++m_pos;
        if (m_pos + 3 > m_data.size()) throw std::runtime_error("TLReader: buffer underflow (bytes len24)");
        len = static_cast<size_t>(m_data[m_pos])
            | (static_cast<size_t>(m_data[m_pos + 1]) << 8)
            | (static_cast<size_t>(m_data[m_pos + 2]) << 16);
        m_pos += 3;
        header_size = 4;
    } else {
        throw std::runtime_error("TLReader: invalid bytes prefix 0xff");
    }
    if (m_pos + len > m_data.size()) throw std::runtime_error("TLReader: buffer underflow (bytes content)");
    Bytes out(m_data.begin() + m_pos, m_data.begin() + m_pos + len);
    m_pos += len;
    // skip alignment padding
    size_t used = header_size + len;
    while (used % 4 != 0) { ++m_pos; ++used; }
    return out;
}

std::string TLReader::readString() {
    Bytes b = readBytes();
    return std::string(b.begin(), b.end());
}

Bytes TLReader::readRaw(size_t n) {
    if (m_pos + n > m_data.size()) throw std::runtime_error("TLReader: buffer underflow (raw)");
    Bytes out(m_data.begin() + m_pos, m_data.begin() + m_pos + n);
    m_pos += n;
    return out;
}

bool TLReader::atEnd() const {
    return m_pos >= m_data.size();
}

size_t TLReader::remaining() const {
    return m_pos < m_data.size() ? m_data.size() - m_pos : 0;
}

void TLReader::skip(size_t n) {
    if (m_pos + n > m_data.size()) throw std::runtime_error("TLReader: skip out of bounds");
    m_pos += n;
}

} // namespace MTProto
