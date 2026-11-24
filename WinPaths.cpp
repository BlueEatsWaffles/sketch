// Only talks to Windows, so no Geode headers here.
#include "WinPaths.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <objbase.h>
#include <shlobj.h>

namespace winpaths {

namespace {
std::string narrow(wchar_t const* w) {
    if (!w || !*w) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string s(static_cast<size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}
}  // namespace

std::string downloads() {
    // FOLDERID_Downloads = {374DE290-123F-4565-9164-39C4925E467B}
    static GUID const kDownloads = {0x374DE290, 0x123F, 0x4565, {0x91, 0x64, 0x39, 0xC4, 0x92, 0x5E, 0x46, 0x7B}};
    PWSTR path = nullptr;
    std::string out;
    if (SUCCEEDED(SHGetKnownFolderPath(kDownloads, 0, nullptr, &path))) out = narrow(path);
    if (path) CoTaskMemFree(path);
    if (out.empty()) {
        wchar_t home[MAX_PATH] = {};
        DWORD n = GetEnvironmentVariableW(L"USERPROFILE", home, MAX_PATH);
        if (n > 0 && n < MAX_PATH) out = narrow(home) + "\\Downloads";
    }
    return out;
}

}  // namespace winpaths
