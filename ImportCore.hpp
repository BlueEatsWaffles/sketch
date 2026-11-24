#pragma once
// Reads macro files made by other bots. Plain C++ only (no game code),
// so it can be tested on its own.
//
//   Eclipse                         -> .gdr2  (GDReplayFormat 2)
//   Mega Hack, xdBot, zBot, Prism   -> .gdr / .gdr.json  (GDReplayFormat 1)
//   yBot 2                          -> .ybot
//   ToastyReplay                    -> .ttr / .ttr2 / .ttr3
//   old xdBot                       -> .xd
//
// Every click is turned into a "tick" of the game's 240-per-second physics,
// which is what the sketch bot plays back.
#include <cstdint>
#include <string>
#include <vector>

namespace importer {

struct Click {
    uint32_t tick = 0;   // physics tick (240 per second)
    uint8_t button = 1;  // 1 = jump, 2 = left, 3 = right
    bool p1 = true;      // true = player 1
    bool down = false;   // true = press, false = release
};

struct Parsed {
    std::string format;  // "GDR2", "GDR", "yBot 2", "ToastyReplay 3", ...
    std::string bot;     // which bot made it, if the file says
    std::string level;   // level name, if the file says
    double fps = 240.0;  // the rate the file was recorded at
    std::vector<Click> clicks;
};

// True if this file name looks like a macro we can read.
bool isMacroFile(std::string const& fileName);

// Friendly name of the bot that normally makes this kind of file.
std::string botForFile(std::string const& fileName);

// Reads a macro from raw file bytes. `offset` moves every click by that many
// ticks (some bots count frames one earlier or later than others).
bool parse(std::vector<uint8_t> const& data, std::string const& fileName, int offset, Parsed& out, std::string& err);

// Makes a GDReplayFormat 1 file (.gdr, MessagePack) that most bots can load.
std::vector<uint8_t> writeGdr(std::vector<Click> const& clicks, std::string const& levelName, std::string const& botName,
                              double duration);

// zlib decompression (used by ToastyReplay files). Exposed for testing.
bool inflateZlib(uint8_t const* data, size_t size, std::vector<uint8_t>& out);

}  // namespace importer
