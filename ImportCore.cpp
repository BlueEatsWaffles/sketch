// Readers for other bots' macro files. Layouts follow the open-source
// GDReplayFormat (maxnut), zcb3 (zeozeozeo) and ybot_fmt (kepe) projects.
#include "ImportCore.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <utility>

namespace importer {

namespace {

// ---------------------------------------------------------------------------
//  Small helpers
// ---------------------------------------------------------------------------

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>((c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c);
    return s;
}

bool endsWith(std::string const& s, std::string const& end) {
    return s.size() >= end.size() && s.compare(s.size() - end.size(), end.size(), end) == 0;
}

// Turns "frame N at F fps" into a 240-per-second tick.
uint32_t toTick(double frame, double fps) {
    if (!(fps > 0.0) || !std::isfinite(fps)) fps = 240.0;
    double t = (std::fabs(fps - 240.0) < 1e-6) ? frame : frame * 240.0 / fps;
    if (!std::isfinite(t) || t < 0.0) t = 0.0;
    return static_cast<uint32_t>(std::llround(t));
}

uint32_t timeToTick(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0.0) seconds = 0.0;
    return static_cast<uint32_t>(std::floor(seconds * 240.0 + 1e-6));
}

uint8_t fixButton(long long b) { return (b >= 1 && b <= 3) ? static_cast<uint8_t>(b) : 1; }

// Bounds-checked little reader over a byte buffer.
struct Reader {
    uint8_t const* p;
    size_t n;
    size_t pos = 0;
    bool bad = false;

    Reader(uint8_t const* data, size_t size) : p(data), n(size) {}
    bool left(size_t k) const { return !bad && pos + k <= n; }
    bool empty() const { return pos >= n; }

    uint8_t u8() {
        if (!left(1)) { bad = true; return 0; }
        return p[pos++];
    }
    uint64_t le(int bytes) {
        if (!left(static_cast<size_t>(bytes))) { bad = true; pos = n; return 0; }
        uint64_t v = 0;
        for (int i = 0; i < bytes; i++) v |= static_cast<uint64_t>(p[pos + i]) << (8 * i);
        pos += static_cast<size_t>(bytes);
        return v;
    }
    uint64_t be(int bytes) {
        if (!left(static_cast<size_t>(bytes))) { bad = true; pos = n; return 0; }
        uint64_t v = 0;
        for (int i = 0; i < bytes; i++) v = (v << 8) | p[pos + i];
        pos += static_cast<size_t>(bytes);
        return v;
    }
    float f32le() { uint32_t u = static_cast<uint32_t>(le(4)); float f; std::memcpy(&f, &u, 4); return f; }
    double f64le() { uint64_t u = le(8); double d; std::memcpy(&d, &u, 8); return d; }
    float f32be() { uint32_t u = static_cast<uint32_t>(be(4)); float f; std::memcpy(&f, &u, 4); return f; }
    double f64be() { uint64_t u = be(8); double d; std::memcpy(&d, &u, 8); return d; }
    // LEB128: 7 bits per byte, low bits first, top bit = "more bytes follow"
    uint64_t varint() {
        uint64_t v = 0;
        for (int shift = 0; shift < 64; shift += 7) {
            uint8_t b = u8();
            if (bad) return 0;
            v |= static_cast<uint64_t>(b & 0x7F) << shift;
            if (!(b & 0x80)) return v;
        }
        bad = true;
        return 0;
    }
    std::string str(size_t len) {
        if (!left(len)) { bad = true; pos = n; return {}; }
        std::string s(reinterpret_cast<char const*>(p + pos), len);
        pos += len;
        return s;
    }
    void skip(size_t k) {
        if (!left(k)) { bad = true; pos = n; return; }
        pos += k;
    }
};

// ---------------------------------------------------------------------------
//  zlib inflate (a small version of Mark Adler's "puff")
// ---------------------------------------------------------------------------

struct Huff {
    short count[16];
    short symbol[288];
};

struct Inflater {
    uint8_t const* in;
    size_t inLen;
    size_t pos = 0;
    uint32_t bitbuf = 0;
    int bitcnt = 0;
    bool fail = false;
    std::vector<uint8_t>& out;

    Inflater(uint8_t const* d, size_t n, std::vector<uint8_t>& o) : in(d), inLen(n), out(o) {}

    int bits(int need) {
        uint32_t val = bitbuf;
        while (bitcnt < need) {
            if (pos >= inLen) { fail = true; return 0; }
            val |= static_cast<uint32_t>(in[pos++]) << bitcnt;
            bitcnt += 8;
        }
        bitbuf = val >> need;
        bitcnt -= need;
        return static_cast<int>(val & ((1u << need) - 1u));
    }

    static int construct(Huff& h, short const* length, int n) {
        for (int len = 0; len <= 15; len++) h.count[len] = 0;
        for (int s = 0; s < n; s++) h.count[length[s]]++;
        if (h.count[0] == n) return 0;
        int left = 1;
        for (int len = 1; len <= 15; len++) {
            left <<= 1;
            left -= h.count[len];
            if (left < 0) return left;
        }
        short offs[16];
        offs[1] = 0;
        for (int len = 1; len < 15; len++) offs[len + 1] = static_cast<short>(offs[len] + h.count[len]);
        for (int s = 0; s < n; s++)
            if (length[s] != 0) h.symbol[offs[length[s]]++] = static_cast<short>(s);
        return left;
    }

    int decode(Huff const& h) {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len <= 15; len++) {
            code |= bits(1);
            if (fail) return -1;
            int count = h.count[len];
            if (code - count < first) return h.symbol[index + (code - first)];
            index += count;
            first += count;
            first <<= 1;
            code <<= 1;
        }
        return -1;
    }

    bool stored() {
        bitbuf = 0;
        bitcnt = 0;
        if (pos + 4 > inLen) return false;
        unsigned len = in[pos] | (static_cast<unsigned>(in[pos + 1]) << 8);
        unsigned nlen = in[pos + 2] | (static_cast<unsigned>(in[pos + 3]) << 8);
        pos += 4;
        if (len != (~nlen & 0xFFFFu)) return false;
        if (pos + len > inLen) return false;
        out.insert(out.end(), in + pos, in + pos + len);
        pos += len;
        return true;
    }

    bool codes(Huff const& lencode, Huff const& distcode) {
        static short const lbase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                                        35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
        static short const lext[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
        static short const dbase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129,
                                        193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097,
                                        6145, 8193, 12289, 16385, 24577};
        static short const dext[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
                                       7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
        for (;;) {
            int sym = decode(lencode);
            if (sym < 0 || fail) return false;
            if (sym < 256) {
                out.push_back(static_cast<uint8_t>(sym));
            } else if (sym == 256) {
                return true;
            } else {
                sym -= 257;
                if (sym >= 29) return false;
                int len = lbase[sym] + bits(lext[sym]);
                int ds = decode(distcode);
                if (ds < 0 || ds >= 30 || fail) return false;
                size_t dist = static_cast<size_t>(dbase[ds] + bits(dext[ds]));
                if (fail || dist > out.size()) return false;
                size_t from = out.size() - dist;
                for (int i = 0; i < len; i++) out.push_back(out[from + static_cast<size_t>(i)]);
            }
        }
    }

    bool fixed() {
        static bool built = false;
        static Huff lencode, distcode;
        if (!built) {
            short lengths[288];
            int s = 0;
            for (; s < 144; s++) lengths[s] = 8;
            for (; s < 256; s++) lengths[s] = 9;
            for (; s < 280; s++) lengths[s] = 7;
            for (; s < 288; s++) lengths[s] = 8;
            construct(lencode, lengths, 288);
            for (s = 0; s < 30; s++) lengths[s] = 5;
            construct(distcode, lengths, 30);
            built = true;
        }
        return codes(lencode, distcode);
    }

    bool dynamic() {
        static short const order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
        short lengths[320];
        Huff lencode, distcode;
        int nlen = bits(5) + 257;
        int ndist = bits(5) + 1;
        int ncode = bits(4) + 4;
        if (fail || nlen > 286 || ndist > 30) return false;
        int index = 0;
        for (; index < ncode; index++) lengths[order[index]] = static_cast<short>(bits(3));
        for (; index < 19; index++) lengths[order[index]] = 0;
        if (fail || construct(lencode, lengths, 19) != 0) return false;
        index = 0;
        while (index < nlen + ndist) {
            int sym = decode(lencode);
            if (sym < 0 || fail) return false;
            if (sym < 16) {
                lengths[index++] = static_cast<short>(sym);
            } else {
                short len = 0;
                if (sym == 16) {
                    if (index == 0) return false;
                    len = lengths[index - 1];
                    sym = 3 + bits(2);
                } else if (sym == 17) {
                    sym = 3 + bits(3);
                } else {
                    sym = 11 + bits(7);
                }
                if (fail || index + sym > nlen + ndist) return false;
                while (sym--) lengths[index++] = len;
            }
        }
        if (lengths[256] == 0) return false;
        int err = construct(lencode, lengths, nlen);
        if (err < 0 || (err > 0 && nlen - lencode.count[0] != 1)) return false;
        err = construct(distcode, lengths + nlen, ndist);
        if (err < 0 || (err > 0 && ndist - distcode.count[0] != 1)) return false;
        return codes(lencode, distcode);
    }

    bool run() {
        int last = 0;
        do {
            last = bits(1);
            int type = bits(2);
            if (fail) return false;
            bool ok = type == 0 ? stored() : type == 1 ? fixed() : type == 2 ? dynamic() : false;
            if (!ok) return false;
        } while (!last);
        return true;
    }
};

// ---------------------------------------------------------------------------
//  A tiny JSON / MessagePack value (GDR 1 files are one or the other)
// ---------------------------------------------------------------------------

struct Val {
    enum Type { Null, Bool, Num, Str, Arr, Obj } type = Null;
    bool b = false;
    double n = 0.0;
    std::string s;
    std::vector<Val> arr;        // array items, or object values
    std::vector<std::string> keys;  // object keys (same order as arr)

    Val const* get(char const* key) const {
        if (type != Obj) return nullptr;
        for (size_t i = 0; i < keys.size() && i < arr.size(); i++)
            if (keys[i] == key) return &arr[i];
        return nullptr;
    }
    double num(double def = 0.0) const { return type == Num ? n : (type == Bool ? (b ? 1.0 : 0.0) : def); }
    bool truthy() const { return type == Bool ? b : (type == Num ? n != 0.0 : false); }
};

struct Json {
    char const* p;
    char const* e;
    bool bad = false;
    int depth = 0;

    void ws() {
        while (p < e && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    }
    bool lit(char const* w) {
        size_t k = std::strlen(w);
        if (static_cast<size_t>(e - p) >= k && std::memcmp(p, w, k) == 0) {
            p += k;
            return true;
        }
        return false;
    }
    std::string str() {
        std::string out;
        if (p >= e || *p != '"') { bad = true; return out; }
        p++;
        while (p < e && *p != '"') {
            char c = *p++;
            if (c == '\\' && p < e) {
                char x = *p++;
                switch (x) {
                    case 'n': out += '\n'; break;
                    case 't': out += '\t'; break;
                    case 'r': out += '\r'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'u': {
                        if (e - p < 4) { bad = true; return out; }
                        unsigned cp = static_cast<unsigned>(std::strtoul(std::string(p, 4).c_str(), nullptr, 16));
                        p += 4;
                        if (cp < 0x80) out += static_cast<char>(cp);
                        else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
                        else { out += static_cast<char>(0xE0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
                        break;
                    }
                    default: out += x; break;
                }
            } else {
                out += c;
            }
        }
        if (p >= e) { bad = true; return out; }
        p++;
        return out;
    }
    Val value() {
        Val v;
        if (++depth > 64) { bad = true; return v; }
        ws();
        if (p >= e) { bad = true; return v; }
        if (*p == '{') {
            p++;
            v.type = Val::Obj;
            ws();
            if (p < e && *p == '}') { p++; depth--; return v; }
            while (!bad) {
                ws();
                std::string k = str();
                ws();
                if (p >= e || *p != ':') { bad = true; break; }
                p++;
                v.keys.push_back(std::move(k));
                v.arr.push_back(value());
                ws();
                if (p < e && *p == ',') { p++; continue; }
                if (p < e && *p == '}') { p++; break; }
                bad = true;
            }
        } else if (*p == '[') {
            p++;
            v.type = Val::Arr;
            ws();
            if (p < e && *p == ']') { p++; depth--; return v; }
            while (!bad) {
                v.arr.push_back(value());
                ws();
                if (p < e && *p == ',') { p++; continue; }
                if (p < e && *p == ']') { p++; break; }
                bad = true;
            }
        } else if (*p == '"') {
            v.type = Val::Str;
            v.s = str();
        } else if (lit("true")) {
            v.type = Val::Bool;
            v.b = true;
        } else if (lit("false")) {
            v.type = Val::Bool;
        } else if (lit("null")) {
        } else {
            std::string num;
            while (p < e && (std::strchr("+-.eE", *p) || (*p >= '0' && *p <= '9'))) num += *p++;
            if (num.empty()) bad = true;
            v.type = Val::Num;
            v.n = std::strtod(num.c_str(), nullptr);
        }
        depth--;
        return v;
    }
};

struct MsgPack {
    Reader r;
    int depth = 0;
    explicit MsgPack(Reader rr) : r(rr) {}

    Val value() {
        Val v;
        if (++depth > 64 || r.bad) { r.bad = true; return v; }
        uint8_t t = r.u8();
        auto arr = [&](uint64_t n) {
            v.type = Val::Arr;
            if (n > r.n) { r.bad = true; return; }
            for (uint64_t i = 0; i < n && !r.bad; i++) v.arr.push_back(value());
        };
        auto map = [&](uint64_t n) {
            v.type = Val::Obj;
            if (n > r.n) { r.bad = true; return; }
            for (uint64_t i = 0; i < n && !r.bad; i++) {
                Val k = value();
                Val x = value();
                v.keys.push_back(k.type == Val::Str ? k.s : std::string());
                v.arr.push_back(std::move(x));
            }
        };
        auto str = [&](uint64_t n) { v.type = Val::Str; v.s = r.str(static_cast<size_t>(n)); };
        auto num = [&](double d) { v.type = Val::Num; v.n = d; };

        if (t <= 0x7F) num(t);
        else if (t >= 0xE0) num(static_cast<int8_t>(t));
        else if (t >= 0x80 && t <= 0x8F) map(t & 0x0F);
        else if (t >= 0x90 && t <= 0x9F) arr(t & 0x0F);
        else if (t >= 0xA0 && t <= 0xBF) str(t & 0x1F);
        else switch (t) {
            case 0xC0: break;
            case 0xC2: v.type = Val::Bool; v.b = false; break;
            case 0xC3: v.type = Val::Bool; v.b = true; break;
            case 0xC4: r.skip(static_cast<size_t>(r.be(1))); break;
            case 0xC5: r.skip(static_cast<size_t>(r.be(2))); break;
            case 0xC6: r.skip(static_cast<size_t>(r.be(4))); break;
            case 0xC7: { size_t n = static_cast<size_t>(r.be(1)); r.skip(1 + n); break; }
            case 0xC8: { size_t n = static_cast<size_t>(r.be(2)); r.skip(1 + n); break; }
            case 0xC9: { size_t n = static_cast<size_t>(r.be(4)); r.skip(1 + n); break; }
            case 0xCA: num(r.f32be()); break;
            case 0xCB: num(r.f64be()); break;
            case 0xCC: num(static_cast<double>(r.be(1))); break;
            case 0xCD: num(static_cast<double>(r.be(2))); break;
            case 0xCE: num(static_cast<double>(r.be(4))); break;
            case 0xCF: num(static_cast<double>(r.be(8))); break;
            case 0xD0: num(static_cast<int8_t>(r.be(1))); break;
            case 0xD1: num(static_cast<int16_t>(r.be(2))); break;
            case 0xD2: num(static_cast<int32_t>(r.be(4))); break;
            case 0xD3: num(static_cast<double>(static_cast<int64_t>(r.be(8)))); break;
            case 0xD4: r.skip(2); break;
            case 0xD5: r.skip(3); break;
            case 0xD6: r.skip(5); break;
            case 0xD7: r.skip(9); break;
            case 0xD8: r.skip(17); break;
            case 0xD9: str(r.be(1)); break;
            case 0xDA: str(r.be(2)); break;
            case 0xDB: str(r.be(4)); break;
            case 0xDC: arr(r.be(2)); break;
            case 0xDD: arr(r.be(4)); break;
            case 0xDE: map(r.be(2)); break;
            case 0xDF: map(r.be(4)); break;
            default: r.bad = true; break;
        }
        depth--;
        return v;
    }
};

// ---------------------------------------------------------------------------
//  Formats
// ---------------------------------------------------------------------------

// GDReplayFormat 1 (.gdr = MessagePack, .gdr.json = JSON): Mega Hack, xdBot, zBot, Prism
bool fromGdr1(Val const& root, Parsed& out, std::string& err) {
    Val const* inputs = root.get("inputs");
    if (!inputs || inputs->type != Val::Arr) {
        err = "This file has no clicks in it.";
        return false;
    }
    if (auto f = root.get("framerate")) out.fps = f->num(240.0);
    if (auto b = root.get("bot")) {
        if (auto n = b->get("name"); n && n->type == Val::Str) out.bot = n->s;
    }
    if (auto l = root.get("level")) {
        if (auto n = l->get("name"); n && n->type == Val::Str) out.level = n->s;
    }
    out.clicks.reserve(inputs->arr.size());
    for (auto const& in : inputs->arr) {
        Val const* fr = in.get("frame");
        if (!fr) continue;
        Click c;
        c.tick = toTick(fr->num(), out.fps);
        if (auto b = in.get("btn")) c.button = fixButton(static_cast<long long>(b->num(1)));
        if (auto p2 = in.get("2p")) c.p1 = !p2->truthy();
        if (auto d = in.get("down")) c.down = d->truthy();
        out.clicks.push_back(c);
    }
    return true;
}

// GDReplayFormat 2 (.gdr2): Eclipse
bool fromGdr2(Reader r, Parsed& out, std::string& err) {
    r.skip(3);  // "GDR"
    r.varint();  // version
    auto readStr = [&]() { return r.str(static_cast<size_t>(r.varint())); };
    std::string inputTag = readStr();
    readStr();  // author
    readStr();  // description
    r.f32be();  // duration
    r.varint();  // game version
    out.fps = r.f64be();
    r.varint();  // seed
    r.varint();  // coins
    r.varint();  // ldm
    bool platformer = r.varint() != 0;
    out.bot = readStr();
    r.varint();  // bot version
    r.varint();  // level id
    out.level = readStr();
    r.skip(static_cast<size_t>(r.varint()));  // replay extension
    uint64_t deaths = r.varint();
    for (uint64_t i = 0; i < deaths && !r.bad; i++) r.varint();
    r.varint();  // total input count (the file is read until it ends instead)
    uint64_t p1Left = r.varint();
    if (r.bad) {
        err = "This .gdr2 file is broken.";
        return false;
    }
    uint64_t prev = 0;
    while (!r.empty() && !r.bad) {
        uint64_t packed = r.varint();
        if (r.bad) break;
        Click c;
        uint64_t delta;
        if (platformer) {
            delta = packed >> 3;
            c.button = fixButton(static_cast<long long>((packed >> 1) & 3));
        } else {
            delta = packed >> 1;
            c.button = 1;
        }
        c.down = (packed & 1) != 0;
        uint64_t frame = prev + delta;
        prev = frame;
        c.p1 = p1Left > 0;
        if (p1Left > 0 && --p1Left == 0) prev = 0;  // player 2's clicks start counting again
        c.tick = toTick(static_cast<double>(frame), out.fps);
        if (!inputTag.empty()) r.skip(static_cast<size_t>(r.varint()));  // input extension
        out.clicks.push_back(c);
    }
    if (r.bad && out.clicks.empty()) {
        err = "This .gdr2 file is broken.";
        return false;
    }
    return true;
}

// yBot 2 (.ybot)
bool fromYbot2(Reader r, Parsed& out, std::string& err) {
    r.skip(4);  // "ybot"
    r.le(4);    // version
    uint32_t metaLen = static_cast<uint32_t>(r.le(4));
    uint32_t blobs = static_cast<uint32_t>(r.le(4));
    size_t metaStart = r.pos;
    double fps = 240.0;
    if (metaLen >= 28 && r.left(28)) {
        Reader m = r;
        m.pos = metaStart + 24;
        float f = m.f32le();
        if (std::isfinite(f) && f > 0.f) fps = f;
    }
    r.skip(metaLen);
    for (uint32_t i = 0; i < blobs && !r.bad; i++) r.skip(static_cast<size_t>(r.le(4)));
    if (r.bad) {
        err = "This .ybot file is broken.";
        return false;
    }
    out.fps = fps;
    double t = 0.0;
    while (!r.empty() && !r.bad) {
        uint64_t val = r.varint();
        if (r.bad) break;
        uint8_t flags = static_cast<uint8_t>(val & 0xF);
        t += static_cast<double>(val >> 4) / fps;
        int button = flags >> 2;
        if (button < 1 || button > 3) {  // "frame rate changed" event
            float f = r.f32le();
            if (std::isfinite(f) && f > 0.f) fps = f;
            continue;
        }
        Click c;
        c.p1 = (flags & 1) != 0;
        c.down = (flags & 2) != 0;
        c.button = static_cast<uint8_t>(button);
        c.tick = static_cast<uint32_t>(std::llround(t * 240.0));
        out.clicks.push_back(c);
    }
    return true;
}

// ToastyReplay: shared metadata of the old TTR and TTR2 files
bool toastyMeta(Reader& r, size_t headerSize, double& fps) {
    auto s16 = [&]() { r.str(static_cast<size_t>(r.le(2))); };
    s16();  // author
    s16();  // name
    s16();  // level name
    r.le(4);  // level id
    fps = r.f64le();
    r.f64le();  // duration
    r.skip(4 + 4 + 4 + 4 + 8);  // game version, start x, start y, seed, time
    if (r.bad || headerSize > r.n) return false;
    if (r.pos < headerSize) r.pos = headerSize;
    if (!(fps > 0.0)) fps = 240.0;
    return true;
}

Click toastyClick(int64_t tick, uint8_t type, uint8_t flags, double fps) {
    Click c;
    c.tick = toTick(static_cast<double>(tick < 0 ? 0 : tick), fps);
    c.button = fixButton(type);
    c.p1 = (flags & 0x01) == 0;
    c.down = (flags & 0x02) != 0;
    return c;
}

bool inflateRest(Reader& r, std::vector<uint8_t>& payload) {
    r.le(4);  // size after unpacking
    if (r.bad || r.pos > r.n) return false;
    return inflateZlib(r.p + r.pos, r.n - r.pos, payload);
}

bool fromTtrLegacy(Reader r, Parsed& out, std::string& err) {
    r.skip(4);  // "TTR\0"
    unsigned version = static_cast<unsigned>(r.le(2));
    uint32_t flags = static_cast<uint32_t>(r.le(4));
    size_t headerSize = static_cast<size_t>(r.le(4));
    if (!toastyMeta(r, headerSize, out.fps)) {
        err = "This ToastyReplay file is broken.";
        return false;
    }
    std::vector<uint8_t> payload;
    if (version == 1) {
        if (r.pos > r.n) return false;
        payload.assign(r.p + r.pos, r.p + r.n);
    } else if (!inflateRest(r, payload)) {
        err = "Couldn't unpack this ToastyReplay file.";
        return false;
    }
    Reader p(payload.data(), payload.size());
    bool timed = (flags & ((1u << 0) | (1u << 5) | (1u << 6))) != 0;
    bool exact = version >= 6 && (flags & (1u << 7)) != 0;
    uint32_t count = static_cast<uint32_t>(p.le(4));
    int64_t tick = 0;
    for (uint32_t i = 0; i < count && !p.bad; i++) {
        if (version == 1) {
            tick = static_cast<int32_t>(p.le(4));
            uint8_t type = p.u8();
            uint8_t fl = p.u8();
            p.f32le();
            out.clicks.push_back(toastyClick(tick, type, fl, out.fps));
        } else {
            tick += static_cast<int64_t>(p.varint());
            uint8_t type = p.u8();
            uint8_t fl = p.u8();
            if (timed) p.f32le();
            if (exact) p.f32le();
            out.clicks.push_back(toastyClick(tick, type, fl, out.fps));
        }
    }
    out.format = "ToastyReplay";
    return true;
}

bool fromTtr2(Reader r, Parsed& out, std::string& err) {
    r.skip(4);  // "TTR2"
    r.le(2);    // version
    uint32_t flags = static_cast<uint32_t>(r.le(4));
    size_t headerSize = static_cast<size_t>(r.le(4));
    std::vector<uint8_t> payload;
    if (!toastyMeta(r, headerSize, out.fps) || !inflateRest(r, payload)) {
        err = "Couldn't unpack this ToastyReplay 2 file.";
        return false;
    }
    // The first list in the file is the winning run; we play only that one.
    Reader p(payload.data(), payload.size());
    bool cbs = (flags & ((1u << 0) | (1u << 5))) != 0;
    uint32_t count = static_cast<uint32_t>(p.le(4));
    int64_t tick = 0;
    for (uint32_t i = 0; i < count && !p.bad; i++) {
        tick += static_cast<int64_t>(p.varint());
        if (cbs) p.f64le();
        uint8_t type = p.u8();
        uint8_t fl = p.u8();
        if (p.bad) break;
        out.clicks.push_back(toastyClick(tick, type, fl, out.fps));
    }
    return true;
}

bool fromTtr3(Reader r, Parsed& out, std::string& err) {
    r.skip(4);  // "TTR3"
    unsigned version = static_cast<unsigned>(r.le(2));
    r.le(2);  // reserved
    uint32_t flags = static_cast<uint32_t>(r.le(4));
    size_t headerLen = static_cast<size_t>(r.le(4));
    r.le(8);  // source format
    r.le(8);  // game version
    r.le(4);  // level id
    out.level = r.str(static_cast<size_t>(r.le(2)));
    r.str(static_cast<size_t>(r.le(2)));  // author
    out.fps = r.f64le();
    if (!(out.fps > 0.0)) out.fps = 240.0;
    if (r.bad || version == 0 || headerLen > r.n) {
        err = "This ToastyReplay 3 file is broken.";
        return false;
    }
    r.pos = headerLen;
    unsigned sections = static_cast<unsigned>(r.le(2));
    struct Sec { uint8_t kind; uint64_t off, size; };
    std::vector<Sec> secs;
    for (unsigned i = 0; i < sections && !r.bad; i++) {
        Sec s;
        s.kind = r.u8();
        r.skip(3);
        s.off = r.le(8);
        s.size = r.le(8);
        secs.push_back(s);
    }
    if (r.bad) {
        err = "This ToastyReplay 3 file is broken.";
        return false;
    }
    std::vector<uint8_t> payload;
    if (flags & (1u << 10)) {
        if (!inflateZlib(r.p + r.pos, r.n - r.pos, payload)) {
            err = "Couldn't unpack this ToastyReplay 3 file.";
            return false;
        }
    } else {
        payload.assign(r.p + r.pos, r.p + r.n);
    }
    for (auto const& s : secs) {
        if (s.kind != 1) continue;  // 1 = the winning run's clicks
        if (s.off + s.size > payload.size()) {
            err = "This ToastyReplay 3 file is broken.";
            return false;
        }
        Reader p(payload.data() + s.off, static_cast<size_t>(s.size));
        uint64_t count = p.le(8);
        for (uint64_t i = 0; i < count && !p.bad; i++) {
            double time = p.f64le();
            uint8_t type = p.u8();
            uint8_t fl = p.u8();
            p.le(2);
            if (p.bad) break;
            Click c;
            c.tick = timeToTick(time);
            c.button = fixButton(type);
            c.p1 = (fl & 0x01) == 0;
            c.down = (fl & 0x02) != 0;
            out.clicks.push_back(c);
        }
    }
    return true;
}

// old xdBot text macros (.xd): first line = fps, then frame|down|button|player1|...
bool fromXd(std::vector<uint8_t> const& data, Parsed& out, std::string& err) {
    std::string text(data.begin(), data.end());
    size_t pos = 0;
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = (nl == std::string::npos) ? text.size() : nl + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        std::vector<std::string> parts;
        size_t a = 0;
        for (;;) {
            size_t b = line.find('|', a);
            parts.push_back(line.substr(a, b == std::string::npos ? std::string::npos : b - a));
            if (b == std::string::npos) break;
            a = b + 1;
        }
        if (parts.size() == 1) {
            double f = std::strtod(parts[0].c_str(), nullptr);
            if (f > 0.0) out.fps = f;
            continue;
        }
        if (parts.size() < 4) continue;
        Click c;
        c.tick = toTick(std::strtod(parts[0].c_str(), nullptr), out.fps);
        c.down = std::atoi(parts[1].c_str()) == 1;
        c.button = fixButton(std::atoi(parts[2].c_str()));
        c.p1 = std::atoi(parts[3].c_str()) == 1;
        out.clicks.push_back(c);
    }
    if (out.clicks.empty()) {
        err = "No clicks found in this .xd file.";
        return false;
    }
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------

bool inflateZlib(uint8_t const* data, size_t size, std::vector<uint8_t>& out) {
    out.clear();
    if (size < 2) return false;
    if ((data[0] & 0x0F) != 8 || ((data[0] << 8) | data[1]) % 31 != 0) return false;  // not zlib
    if (data[1] & 0x20) return false;  // preset dictionary: not used by any bot
    Inflater inf(data + 2, size - 2, out);
    return inf.run();
}

namespace {
struct MpOut {
    std::vector<uint8_t> b;
    void raw(uint8_t v) { b.push_back(v); }
    void be(uint64_t v, int n) {
        for (int i = n - 1; i >= 0; i--) b.push_back(static_cast<uint8_t>(v >> (8 * i)));
    }
    void map(size_t n) {
        if (n < 16) raw(static_cast<uint8_t>(0x80 | n));
        else { raw(0xDE); be(n, 2); }
    }
    void arr(size_t n) {
        if (n < 16) raw(static_cast<uint8_t>(0x90 | n));
        else if (n < 65536) { raw(0xDC); be(n, 2); }
        else { raw(0xDD); be(n, 4); }
    }
    void str(std::string const& s) {
        size_t n = s.size();
        if (n < 32) raw(static_cast<uint8_t>(0xA0 | n));
        else if (n < 256) { raw(0xD9); be(n, 1); }
        else { raw(0xDA); be(n, 2); }
        b.insert(b.end(), s.begin(), s.end());
    }
    void uint(uint64_t v) {
        if (v < 128) raw(static_cast<uint8_t>(v));
        else if (v < 256) { raw(0xCC); be(v, 1); }
        else if (v < 65536) { raw(0xCD); be(v, 2); }
        else { raw(0xCE); be(v, 4); }
    }
    void f64(double d) {
        uint64_t u;
        std::memcpy(&u, &d, 8);
        raw(0xCB);
        be(u, 8);
    }
    void boolean(bool v) { raw(v ? 0xC3 : 0xC2); }
};
}  // namespace

std::vector<uint8_t> writeGdr(std::vector<Click> const& clicks, std::string const& levelName, std::string const& botName,
                              double duration) {
    MpOut o;
    o.map(12);
    o.str("gameVersion"); o.f64(2.2081);
    o.str("description"); o.str("Made by the Sketchbook Menu Predict bot");
    o.str("version"); o.f64(1.0);
    o.str("duration"); o.f64(duration);
    o.str("bot"); o.map(2); o.str("name"); o.str(botName); o.str("version"); o.str("1.0");
    o.str("level"); o.map(2); o.str("id"); o.uint(0); o.str("name"); o.str(levelName);
    o.str("author"); o.str("");
    o.str("seed"); o.uint(0);
    o.str("coins"); o.uint(0);
    o.str("ldm"); o.boolean(false);
    o.str("framerate"); o.f64(240.0);
    o.str("inputs");
    o.arr(clicks.size());
    for (auto const& c : clicks) {
        o.map(4);
        o.str("frame"); o.uint(c.tick);
        o.str("btn"); o.uint(c.button);
        o.str("2p"); o.boolean(!c.p1);
        o.str("down"); o.boolean(c.down);
    }
    return o.b;
}

bool isMacroFile(std::string const& fileName) {
    std::string n = lower(fileName);
    for (char const* ext : {".gdr", ".gdr.json", ".gdr2", ".ybot", ".ttr", ".ttr2", ".ttr3", ".xd"})
        if (endsWith(n, ext)) return true;
    return false;
}

std::string botForFile(std::string const& fileName) {
    std::string n = lower(fileName);
    if (endsWith(n, ".gdr2")) return "Eclipse";
    if (endsWith(n, ".gdr") || endsWith(n, ".gdr.json")) return "GDR (Mega Hack, xdBot, zBot...)";
    if (endsWith(n, ".ybot")) return "yBot 2";
    if (endsWith(n, ".ttr") || endsWith(n, ".ttr2") || endsWith(n, ".ttr3")) return "ToastyReplay";
    if (endsWith(n, ".xd")) return "old xdBot";
    return "?";
}

bool parse(std::vector<uint8_t> const& data, std::string const& fileName, int offset, Parsed& out, std::string& err) {
    out = Parsed{};
    if (data.empty()) {
        err = "The file is empty.";
        return false;
    }
    Reader r(data.data(), data.size());
    auto magic = [&](char const* m, size_t k) { return data.size() >= k && std::memcmp(data.data(), m, k) == 0; };
    bool ok = false;
    std::string n = lower(fileName);

    if (magic("TTR3", 4)) {
        out.format = "ToastyReplay 3";
        ok = fromTtr3(r, out, err);
    } else if (magic("TTR2", 4)) {
        out.format = "ToastyReplay 2";
        ok = fromTtr2(r, out, err);
    } else if (magic("TTR\0", 4)) {
        ok = fromTtrLegacy(r, out, err);
    } else if (magic("ybot", 4)) {
        out.format = "yBot 2";
        ok = fromYbot2(r, out, err);
    } else if (magic("GDR", 3)) {
        out.format = "GDR2";
        ok = fromGdr2(r, out, err);
    } else if (endsWith(n, ".xd")) {
        out.format = "xdBot (old)";
        ok = fromXd(data, out, err);
    } else {
        size_t i = 0;
        while (i < data.size() && (data[i] == ' ' || data[i] == '\n' || data[i] == '\r' || data[i] == '\t')) i++;
        if (i < data.size() && data[i] == '{') {
            Json j{reinterpret_cast<char const*>(data.data()), reinterpret_cast<char const*>(data.data()) + data.size()};
            Val root = j.value();
            out.format = "GDR (JSON)";
            ok = !j.bad && fromGdr1(root, out, err);
            if (j.bad && err.empty()) err = "This JSON file is broken.";
        } else {
            MsgPack mp{r};
            Val root = mp.value();
            out.format = "GDR";
            ok = !mp.r.bad && fromGdr1(root, out, err);
            if (mp.r.bad && err.empty()) err = "This doesn't look like a macro file this menu can read.";
        }
    }
    if (!ok) return false;
    if (out.clicks.empty()) {
        err = "This macro has no clicks in it.";
        return false;
    }
    for (auto& c : out.clicks) {
        long long t = static_cast<long long>(c.tick) + offset;
        c.tick = static_cast<uint32_t>(t < 0 ? 0 : t);
    }
    std::stable_sort(out.clicks.begin(), out.clicks.end(), [](Click const& a, Click const& b) { return a.tick < b.tick; });
    return true;
}

}  // namespace importer
