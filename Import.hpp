#pragma once
#include <cstdint>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
//  Finds macros made by other bots (Eclipse, Mega Hack, xdBot, zBot, Prism,
//  yBot 2, ToastyReplay) on this PC and loads them into the Sketchbook bot.
// ---------------------------------------------------------------------------
namespace imports {

struct Found {
    std::string path;    // full path (UTF-8)
    std::string name;    // file name
    std::string bot;     // which bot probably made it
    std::string where;   // short folder hint
    int64_t modified = 0;
};

void scan();                    // looks in the background
bool scanning();
std::vector<Found> found();     // newest first

// Loads one into the bot. On success `summary` says what was loaded.
bool load(std::string const& path, std::string& summary, std::string& err);

}  // namespace imports
