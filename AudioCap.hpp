#pragma once
#include <cstdint>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
//  Records what the game sounds like (music, NONG songs, sound effects,
//  click sounds) by listening to the game's final sound mix. Used by the
//  Video Maker's "Record the game" sound option.
// ---------------------------------------------------------------------------
namespace audiocap {

bool start();                 // start listening (samples are kept from now on)
void stop();                  // stop listening
bool running();
double latency();             // how late the recorded sound is compared to the game (seconds)
// Takes everything recorded so far: 16-bit stereo, interleaved.
std::vector<int16_t> take(int& sampleRate);
bool writeWav(std::string const& pathUtf8, std::vector<int16_t> const& samples, int sampleRate);

}  // namespace audiocap
