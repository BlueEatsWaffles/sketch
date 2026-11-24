#pragma once
#include <Geode/Geode.hpp>

#include <string>

// ---------------------------------------------------------------------------
//  Video maker. Two engines:
//   * FFmpeg   - best quality, any size, adds the level's music (the song
//                file, or the game's real sound recorded in a second pass).
//                FFmpeg is a free program the PLAYER has to install.
//   * Built-in - uses Windows' own encoder. Nothing to install, faster,
//                but lower quality, no music, size = your game window.
// ---------------------------------------------------------------------------
namespace render {

enum Engine { FFmpeg = 0, Builtin = 1 };

constexpr int kEncCount = 6;
extern char const* const kEncNames[kEncCount];    // ffmpeg names (libx264...)
extern char const* const kEncLabels[kEncCount];   // friendly names

struct Report {
    bool checked = false;
    bool checking = false;
    bool ffmpegFound = false;
    std::string ffmpegPath;
    std::string version;
    bool enc[kEncCount] = {};
    bool outputOk = false;
    std::string outputDir;
    bool builtinOk = false;   // Windows has an H.264 encoder
    bool builtinGpu = false;  // ...on the graphics card
};

Report report();
void runCheck();              // checks the PC in the background
std::string findFfmpeg();
std::string outputDir();
std::string songPath(PlayLayer* pl);
void openFolder(std::string const& path);
void openUrl(std::string const& url);
std::string saveDirStr();

bool active();
int engine();                 // engine of the running video
int fps();                    // frame rate of the running video
int fpsFor(int idx);          // FFmpeg frame rate for a "Frame rate" choice
bool locksTime();             // true = game is slowed down so every frame is perfect
int frames();
float videoSeconds();
double elapsed();

bool start(PlayLayer* pl, bool restartLevel);
void stop(bool keep);
void capture(float realDt);   // called once per frame while active
void onLevelComplete();
void onDeath();                // the player died (stops a sound recording pass)
void tick(float realDt);       // every frame
// "Record the game" sound: after the video, the macro plays again at normal
// speed while the game's real sound is recorded, then added to the video.
bool audioPassActive();
double audioPassSeconds();
void cancelAudioPass();
bool screenshot(std::string& savedPath);  // saves a PNG of the game (no menu) to <mod folder>/screenshots
void onQuit();

}  // namespace render
