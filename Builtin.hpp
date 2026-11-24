#pragma once
#include <cstdint>
#include <string>

// ---------------------------------------------------------------------------
//  Built-in renderer: uses Windows' own video encoder (Media Foundation).
//  Nothing to install. Faster than FFmpeg, but lower quality, no music, and
//  the video is always the size of the game window.
// ---------------------------------------------------------------------------
namespace builtin {

struct Info {
    bool ok = false;   // Windows has an H.264 encoder we can use
    bool gpu = false;  // ...and one of them runs on the graphics card
};

// Looks for Windows' H.264 encoders. Safe to call from a background thread.
Info probe();

class Encoder {
public:
    Encoder() = default;
    ~Encoder();
    Encoder(Encoder const&) = delete;
    Encoder& operator=(Encoder const&) = delete;

    // pathUtf8: where the .mp4 goes. width/height must be even.
    bool open(std::string const& pathUtf8, int width, int height, int fps, unsigned bitrate, std::string& err);

    // pixels: BGRA, bottom row first (what glReadPixels gives us).
    // copies: how many video frames this picture should fill.
    bool write(uint8_t const* pixels, int copies, std::string& err);

    // Finishes the file. Returns false if something went wrong.
    bool close();

    bool isOpen() const { return m_impl != nullptr; }

private:
    void* m_impl = nullptr;
};

}  // namespace builtin
