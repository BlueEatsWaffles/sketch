#pragma once
#include <Geode/Geode.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
//  Macro bot  = records your clicks (frame + button) and plays them back.
//  Replay bot = the same, plus "frame fixes": it also saves where the player
//               is every few ticks and snaps it back there during playback,
//               so the run can never drift.
// ---------------------------------------------------------------------------
namespace bot {

enum class State { Idle, Record, Play };

struct Input {
    uint32_t frame = 0;
    uint8_t button = 1;
    bool down = false;
    bool p1 = true;
};

struct PState {
    float x = 0.f, y = 0.f, rot = 0.f;
    double yVel = 0.0;
};

struct Fix {
    uint32_t frame = 0;
    bool dual = false;
    PState a, b;
};

struct Macro {
    std::vector<Input> inputs;
    std::vector<Fix> fixes;
    std::string level;
};

State state();
Macro const& macro();
bool injecting();
uint32_t frame(GJBaseGameLayer* g);
uint32_t lastFrame();

void startRecord();
void startPlay();
void stop();

// hooks
void record(GJBaseGameLayer* g, bool down, int button, bool p1);
void beforeTick(GJBaseGameLayer* g);
void afterTick(GJBaseGameLayer* g);
void onReset(PlayLayer* pl, bool duringInit);
void onComplete();
void onQuit();
bool blocksUser();
void inject(GJBaseGameLayer* g, bool down, int button, bool p1);

// files
std::filesystem::path folder();
std::vector<std::string> list();
bool save(std::string const& name, std::string& err);
// Writes any macro to any path in the sketch format (.skm).
bool writeFile(std::filesystem::path const& path, Macro const& m, std::string& err);
bool load(std::string const& name, std::string& err);
bool remove(std::string const& name);

// Replace the current macro with one made by another bot (see Import.cpp).
void loadImported(Macro m);

}  // namespace bot
