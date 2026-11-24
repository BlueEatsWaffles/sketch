#include "Util.hpp"
#include <chrono>

namespace util {

std::string keyName(int k) {
    if (k < 0) return "None";
    if (k >= 'A' && k <= 'Z') return std::string(1, static_cast<char>(k));
    if (k >= '0' && k <= '9') return std::string(1, static_cast<char>(k));
    if (k >= 0x70 && k <= 0x87) return "F" + std::to_string(k - 0x6F);
    if (k >= 0x60 && k <= 0x69) return "Num" + std::to_string(k - 0x60);
    switch (k) {
        case 0x08: return "Backspace";
        case 0x09: return "Tab";
        case 0x0D: return "Enter";
        case 0x10: return "Shift";
        case 0x11: return "Ctrl";
        case 0x12: return "Alt";
        case 0x13: return "Pause";
        case 0x14: return "Caps";
        case 0x1B: return "Esc";
        case 0x20: return "Space";
        case 0x21: return "PgUp";
        case 0x22: return "PgDn";
        case 0x23: return "End";
        case 0x24: return "Home";
        case 0x25: return "Left";
        case 0x26: return "Up";
        case 0x27: return "Right";
        case 0x28: return "Down";
        case 0x2D: return "Insert";
        case 0x2E: return "Delete";
        case 0xBA: return ";";
        case 0xBB: return "=";
        case 0xBC: return ",";
        case 0xBD: return "-";
        case 0xBE: return ".";
        case 0xBF: return "/";
        case 0xC0: return "`";
        case 0xDB: return "[";
        case 0xDC: return "\\";
        case 0xDD: return "]";
        case 0xDE: return "'";
        default: break;
    }
    return "Key " + std::to_string(k);
}

double now() {
    using namespace std::chrono;
    static auto const start = steady_clock::now();
    return duration<double>(steady_clock::now() - start).count();
}

std::tm localTime(std::time_t t) {
    std::tm out{};
#ifdef _WIN32
    localtime_s(&out, &t);
#else
    localtime_r(&t, &out);
#endif
    return out;
}

std::filesystem::path u8path(std::string const& s) {
    std::u8string u(s.begin(), s.end());
    return std::filesystem::path(u);
}

std::string pathStr(std::filesystem::path const& p) {
    auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

std::string safeFileName(std::string const& s) {
    std::string out;
    for (char c : s) {
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                  c == '-' || c == '_' || c == ' ' || c == '.';
        if (ok) out += c;
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
    if (out.empty()) out = "untitled";
    return out;
}

}  // namespace util
