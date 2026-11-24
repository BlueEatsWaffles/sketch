#pragma once
#include "Bot.hpp"
#include "PredictCore.hpp"

#include <Geode/Geode.hpp>

#include <functional>
#include <string>

// ---------------------------------------------------------------------------
//  "Predict" tab: a bot that beats the level by itself (see PredictCore.hpp).
//  Uses practice mode while it thinks, never speedhack, and double-checks the
//  final run from the very start. Works on 2-player levels too.
// ---------------------------------------------------------------------------
namespace predictor {

bool active();                 // thinking or double-checking right now
bool restoring();              // inside our own checkpoint load (skip side effects)
bool start(PlayLayer* pl);     // restart the level and begin
void stop(bool restartLevel);
void onQuit();                 // the level is closing

// Called from GJBaseGameLayer::update instead of the normal game update.
// `realDt` is the normal frame time, `stepOneTick` runs exactly one 1/240 s tick.
void frame(PlayLayer* pl, float realDt, std::function<void(float)> const& update);
void tickBegin(GJBaseGameLayer* g);   // start of processCommands (one physics tick)
void tickEnd(GJBaseGameLayer* g);     // end of processCommands
void onDeath(PlayerObject* who);      // destroyPlayer while thinking
void onWin();                         // end of level while thinking

predict::Phase phase();
predict::Stats stats();
std::string message();
double elapsed();
float percentDone();           // 0..100 for the progress bar
bool twoPlayer();              // the level being predicted is a 2-player level

// The finished run
bool hasResult();
bot::Macro const& result();
std::string levelName();
std::string defaultName();     // "<level> - bot"
bool watch();                  // load into the bot and play it
bool saveToLibrary(std::string const& name, std::string& err);
bool saveToFolder(std::string const& folder, std::string const& name, int format, std::string& savedPath, std::string& err);

}  // namespace predictor
