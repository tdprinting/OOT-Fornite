#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Little-endian binary serialization with bounds-checked reads. A reader that runs out of data (or is asked for
// something malformed) flips `ok` to false and returns zeros, so decoding untrusted network bytes never reads out of range.
namespace royale {

class ByteWriter {
  public:
    void U8(uint8_t v) { buf.push_back(v); }
    void U16(uint16_t v) { U8(static_cast<uint8_t>(v)); U8(static_cast<uint8_t>(v >> 8)); }
    void I16(int16_t v) { U16(static_cast<uint16_t>(v)); }
    void U32(uint32_t v) { U16(static_cast<uint16_t>(v)); U16(static_cast<uint16_t>(v >> 16)); }
    void U64(uint64_t v) { U32(static_cast<uint32_t>(v)); U32(static_cast<uint32_t>(v >> 32)); }
    void F32(float v) { uint32_t u; std::memcpy(&u, &v, 4); U32(u); }
    // Length-prefixed (1 byte), truncated to 255 bytes.
    void Str(const std::string& s) {
        size_t n = s.size() > 255 ? 255 : s.size();
        U8(static_cast<uint8_t>(n));
        buf.insert(buf.end(), s.begin(), s.begin() + static_cast<long>(n));
    }
    std::vector<uint8_t> buf;
};

class ByteReader {
  public:
    ByteReader(const uint8_t* data, size_t size) : p(data), n(size) {}
    uint8_t U8() { return Need(1) ? p[pos++] : uint8_t(0); }
    uint16_t U16() { if (!Need(2)) return 0; uint16_t lo = U8(); uint16_t hi = U8(); return static_cast<uint16_t>(lo | (hi << 8)); }
    int16_t I16() { return static_cast<int16_t>(U16()); }
    uint32_t U32() { if (!Need(4)) return 0; uint32_t lo = U16(); uint32_t hi = U16(); return lo | (hi << 16); }
    uint64_t U64() { if (!Need(8)) return 0; uint64_t lo = U32(); uint64_t hi = U32(); return lo | (hi << 32); }
    float F32() { uint32_t u = U32(); float v; std::memcpy(&v, &u, 4); return v; }
    std::string Str(size_t maxLen = 255) {
        size_t len = U8();
        if (len > maxLen || !Need(len)) { ok = false; return {}; }
        std::string s(reinterpret_cast<const char*>(p + pos), len);
        pos += len;
        return s;
    }
    // True if every read so far succeeded and all bytes were consumed.
    bool Done() const { return ok && pos == n; }
    size_t Remaining() const { return n - pos; }
    bool ok = true;

  private:
    bool Need(size_t k) {
        if (!ok || n - pos < k) { ok = false; return false; }
        return true;
    }
    const uint8_t* p;
    size_t n;
    size_t pos = 0;
};

} // namespace royale
