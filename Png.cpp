#include "Png.hpp"

namespace png {

namespace {
uint32_t crcTable[256];
bool crcReady = false;

uint32_t crc(uint8_t const* p, size_t n, uint32_t c = 0xFFFFFFFFu) {
    if (!crcReady) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t v = i;
            for (int k = 0; k < 8; k++) v = (v & 1) ? 0xEDB88320u ^ (v >> 1) : v >> 1;
            crcTable[i] = v;
        }
        crcReady = true;
    }
    for (size_t i = 0; i < n; i++) c = crcTable[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c;
}

void be32(std::vector<uint8_t>& o, uint32_t v) {
    for (int i = 3; i >= 0; i--) o.push_back(static_cast<uint8_t>(v >> (8 * i)));
}

void chunk(std::vector<uint8_t>& o, char const* type, std::vector<uint8_t> const& data) {
    be32(o, static_cast<uint32_t>(data.size()));
    size_t start = o.size();
    o.insert(o.end(), type, type + 4);
    o.insert(o.end(), data.begin(), data.end());
    be32(o, crc(o.data() + start, o.size() - start) ^ 0xFFFFFFFFu);
}
}  // namespace

std::vector<uint8_t> encodeRgb(uint8_t const* rgb, int w, int h, bool bottomUp) {
    std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::vector<uint8_t> ihdr;
    be32(ihdr, static_cast<uint32_t>(w));
    be32(ihdr, static_cast<uint32_t>(h));
    ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});  // 8-bit RGB
    chunk(out, "IHDR", ihdr);

    // raw scanlines: filter byte 0 + pixels
    size_t row = static_cast<size_t>(w) * 3;
    std::vector<uint8_t> raw;
    raw.reserve((row + 1) * static_cast<size_t>(h));
    for (int y = 0; y < h; y++) {
        int src = bottomUp ? (h - 1 - y) : y;
        raw.push_back(0);
        uint8_t const* p = rgb + static_cast<size_t>(src) * row;
        raw.insert(raw.end(), p, p + row);
    }
    // zlib stream made of "stored" (uncompressed) blocks
    std::vector<uint8_t> z = {0x78, 0x01};
    size_t pos = 0;
    uint32_t a = 1, b = 0;
    for (uint8_t v : raw) {
        a = (a + v) % 65521u;
        b = (b + a) % 65521u;
    }
    do {
        size_t n = raw.size() - pos;
        if (n > 65535) n = 65535;
        bool last = pos + n >= raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(static_cast<uint8_t>(n & 0xFF));
        z.push_back(static_cast<uint8_t>(n >> 8));
        z.push_back(static_cast<uint8_t>(~n & 0xFF));
        z.push_back(static_cast<uint8_t>((~n >> 8) & 0xFF));
        z.insert(z.end(), raw.begin() + static_cast<long>(pos), raw.begin() + static_cast<long>(pos + n));
        pos += n;
    } while (pos < raw.size());
    be32(z, (b << 16) | a);
    chunk(out, "IDAT", z);
    chunk(out, "IEND", {});
    return out;
}

}  // namespace png
