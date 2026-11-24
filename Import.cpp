#include "Import.hpp"
#include "Bot.hpp"
#include "Config.hpp"
#include "ImportCore.hpp"
#include "Util.hpp"

#include <Geode/Geode.hpp>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <thread>

using namespace geode::prelude;
namespace fs = std::filesystem;

namespace imports {

namespace {

std::mutex g_mx;
std::vector<Found> g_found;
std::atomic<bool> g_scanning{false};

std::string lower(std::string s) {
    for (auto& ch : s) ch = static_cast<char>((ch >= 'A' && ch <= 'Z') ? ch - 'A' + 'a' : ch);
    return s;
}

// Folder names tell us which bot saved the file.
std::string guessBot(std::string const& lowerPath, std::string const& name) {
    static struct { char const* key; char const* bot; } const hints[] = {
        {"eclipse", "Eclipse"},   {"megahack", "Mega Hack"},   {"absolllute", "Mega Hack"},
        {"xdbot", "xdBot"},       {"zbot", "zBot"},             {"toasty", "ToastyReplay"},
        {"prism", "Prism Menu"},  {"ybot", "yBot 2"},
    };
    for (auto const& h : hints)
        if (lowerPath.find(h.key) != std::string::npos) return h.bot;
    return importer::botForFile(name);
}

struct Root {
    fs::path path;
    int depth;  // how many folders deep to look
};

void walk(Root const& root, std::vector<Found>& out, size_t& visited) {
    // big folders that never hold macros
    static char const* const skip[] = {"resources", "geode", "unzipped", "logs", "temp", ".git", "renders",
                                       "node_modules", "steamapps", "sfx"};
    std::error_code ec;
    if (!fs::is_directory(root.path, ec)) return;
    fs::recursive_directory_iterator it(root.path, fs::directory_options::skip_permission_denied, ec);
    if (ec) return;
    for (fs::recursive_directory_iterator end; it != end; it.increment(ec)) {
        if (ec || ++visited > 300000 || out.size() >= 3000) break;
        auto const& entry = *it;
        std::error_code e2;
        if (entry.is_directory(e2)) {
            std::string dn = lower(util::pathStr(entry.path().filename()));
            bool stop = it.depth() + 1 >= root.depth;
            for (auto s : skip)
                if (dn == s) stop = true;
            if (stop) it.disable_recursion_pending();
            continue;
        }
        if (!entry.is_regular_file(e2)) continue;
        std::string name = util::pathStr(entry.path().filename());
        if (!importer::isMacroFile(name)) continue;
        Found f;
        f.path = util::pathStr(entry.path());
        f.name = name;
        f.bot = guessBot(lower(f.path), name);
        f.where = util::pathStr(entry.path().parent_path().filename());
        auto t = entry.last_write_time(e2);
        f.modified = e2 ? 0 : static_cast<int64_t>(t.time_since_epoch().count());
        out.push_back(std::move(f));
    }
}

}  // namespace

void scan() {
    bool expected = false;
    if (!g_scanning.compare_exchange_strong(expected, true)) return;
    std::vector<Root> roots;
    roots.push_back({bot::folder(), 2});                          // our macros folder (drop files here)
    roots.push_back({dirs::getModsSaveDir(), 4});                 // every Geode mod's save folder
    roots.push_back({dirs::getModConfigDir(), 4});                // every Geode mod's config folder
    roots.push_back({dirs::getGameDir(), 3});                     // the Geometry Dash folder
    roots.push_back({dirs::getSaveDir(), 2});                     // AppData\Local\GeometryDash
    if (cfg().importDir[0]) roots.push_back({util::u8path(cfg().importDir), 5});

    std::thread([roots] {
        std::vector<Found> out;
        size_t visited = 0;
        for (auto const& r : roots) walk(r, out, visited);
        std::sort(out.begin(), out.end(), [](Found const& a, Found const& b) { return a.path < b.path; });
        out.erase(std::unique(out.begin(), out.end(), [](Found const& a, Found const& b) { return a.path == b.path; }),
                  out.end());
        std::stable_sort(out.begin(), out.end(), [](Found const& a, Found const& b) { return a.modified > b.modified; });
        {
            std::lock_guard<std::mutex> lock(g_mx);
            g_found = std::move(out);
        }
        g_scanning = false;
    }).detach();
}

bool scanning() { return g_scanning; }

std::vector<Found> found() {
    std::lock_guard<std::mutex> lock(g_mx);
    return g_found;
}

bool load(std::string const& path, std::string& summary, std::string& err) {
    auto p = util::u8path(path);
    std::ifstream in(p, std::ios::binary);
    if (!in) {
        err = "Couldn't open that file.";
        return false;
    }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::string fileName = util::pathStr(p.filename());

    importer::Parsed parsed;
    if (!importer::parse(data, fileName, cfg().importOffset, parsed, err)) return false;

    bot::Macro m;
    m.level = parsed.level;
    m.inputs.reserve(parsed.clicks.size());
    for (auto const& c : parsed.clicks) {
        bot::Input i;
        i.frame = c.tick;
        i.button = c.button;
        i.down = c.down;
        i.p1 = c.p1;
        m.inputs.push_back(i);
    }
    bot::loadImported(std::move(m));

    // name it after the file, so "Save" keeps a Sketchbook copy
    std::string stem = util::pathStr(p.stem());
    if (stem.size() > 4 && lower(stem.substr(stem.size() - 4)) == ".gdr") stem.resize(stem.size() - 4);
    std::snprintf(cfg().macroName, sizeof(cfg().macroName), "%s", util::safeFileName(stem).c_str());

    char buf[256];
    std::string who = parsed.bot.empty() ? std::string() : (" by " + parsed.bot);
    std::snprintf(buf, sizeof(buf), "%zu clicks (%s%s)", parsed.clicks.size(), parsed.format.c_str(), who.c_str());
    summary = buf;
    if (parsed.fps > 0.0 && (parsed.fps < 239.5 || parsed.fps > 240.5)) {
        std::snprintf(buf, sizeof(buf), " - made at %.0f TPS, may not play perfectly", parsed.fps);
        summary += buf;
    }
    return true;
}

}  // namespace imports
