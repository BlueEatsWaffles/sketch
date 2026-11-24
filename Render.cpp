#include "Render.hpp"
#include "AudioCap.hpp"
#include "Bot.hpp"
#include "Builtin.hpp"
#include "Config.hpp"
#include "Gui.hpp"
#include "Hacks.hpp"
#include "Overlay.hpp"
#include "Png.hpp"
#include "Predict.hpp"
#include "Util.hpp"

#include <cmath>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <shellapi.h>

#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif

using namespace geode::prelude;

namespace render {

char const* const kEncNames[kEncCount] = {"libx264", "h264_nvenc", "h264_amf", "h264_qsv", "libx265", "hevc_nvenc"};
char const* const kEncLabels[kEncCount] = {
    "H.264 - CPU (works everywhere)", "H.264 - NVIDIA GPU", "H.264 - AMD GPU",
    "H.264 - Intel GPU",              "H.265 - CPU (smaller)", "H.265 - NVIDIA GPU",
};

namespace {

std::mutex g_reportMx;
Report g_report;

struct Session {
    bool active = false;
    int engine = FFmpeg;
    bool realtime = false;   // built-in only: record at normal game speed
    double acc = 0.0;        // built-in real-time: time waiting to become frames
    builtin::Encoder enc;    // built-in only
    void* hIn = nullptr;     // FFmpeg only: pipe we write frames into
    PROCESS_INFORMATION pi{};
    CCRenderTexture* rt = nullptr;
    bool sharp = false;      // FFmpeg: the picture is drawn bigger than the window (1440p, 4K, 8K...)
    int cw = 0, ch = 0;
    std::vector<uint8_t> buf;
    int frames = 0;
    int fps = 60;
    double startTime = 0.0;
    float endCountdown = -1.f;
    std::string ffmpeg, tmpPath, finalPath, song;
    float songOffset = 0.f;
    bool recordAudio = false;  // play the macro again afterwards and record the real sound
};
Session g;

// The second pass that records the game's sound.
struct Pass {
    bool active = false;
    bool levelDone = false;
    float endCountdown = -1.f;
    double started = 0.0;
    double videoSeconds = 0.0;
    PROCESS_INFORMATION pi{};  // FFmpeg still finishing the video
    std::string ff, tmp, fin, wav, song;
    float songOffset = 0.f;
};
Pass g_pass;

std::wstring widen(std::string const& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string narrow(std::wstring const& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::string q(std::string const& s) { return "\"" + s + "\""; }

bool isFile(std::filesystem::path const& p) {
    std::error_code ec;
    return std::filesystem::exists(p, ec) && !std::filesystem::is_directory(p, ec);
}

// Runs a program hidden, collects what it prints, waits for it to finish.
bool runProcess(std::string const& cmdUtf8, std::string* output, DWORD* exitCode, DWORD timeoutMs) {
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return false;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = wr;
    si.hStdError = wr;
    si.hStdInput = nullptr;
    PROCESS_INFORMATION pi{};
    std::wstring cmd = widen(cmdUtf8);
    BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(wr);
    if (!ok) {
        CloseHandle(rd);
        return false;
    }
    std::string out;
    char buf[4096];
    DWORD n = 0;
    while (ReadFile(rd, buf, sizeof(buf), &n, nullptr) && n > 0) {
        if (out.size() < (1u << 20)) out.append(buf, n);
    }
    WaitForSingleObject(pi.hProcess, timeoutMs);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(rd);
    if (output) *output = std::move(out);
    if (exitCode) *exitCode = code;
    return true;
}

int fpsFromIdx(int i) {
    static int const v[] = {30, 60, 120, 144, 240, 360, 480};
    if (i == 7) return util::clamp(cfg().rFpsCustom, 10, 1000);
    return v[util::clamp(i, 0, 6)];
}

void resolution(int& w, int& h) {
    auto const& c = cfg();
    // 4 is "custom" (it was the last one before 5K and 8K were added)
    static int const pw[] = {1280, 1920, 2560, 3840, 0, 5120, 7680};
    static int const ph[] = {720, 1080, 1440, 2160, 0, 2880, 4320};
    if (c.rResIdx >= 0 && c.rResIdx <= 6 && c.rResIdx != 4) {
        w = pw[c.rResIdx];
        h = ph[c.rResIdx];
    } else {
        w = c.rW;
        h = c.rH;
    }
    w = util::maxv(2, w & ~1);
    h = util::maxv(2, h & ~1);
}

// Built-in renderer bitrate: Low / Medium / High, adjusted for size and FPS.
unsigned builtinBitrate(int w, int h, int fps) {
    if (cfg().rRateMode == 1) return static_cast<unsigned>(util::clamp(cfg().rBitrate, 0.5f, 300.f) * 1000000.0);
    static double const mbps[] = {5.0, 10.0, 20.0};
    double base = mbps[util::clamp(cfg().rBQuality, 0, 2)];
    double scale = (static_cast<double>(w) * static_cast<double>(h)) / (1920.0 * 1080.0) * (static_cast<double>(fps) / 60.0);
    double b = util::clamp(base * util::maxv(scale, 0.25), 1.0, 80.0);
    return static_cast<unsigned>(b * 1000000.0);
}

std::string qualityArgs(int enc) {
    auto const& c = cfg();
    static char const* presets[] = {"ultrafast", "superfast", "veryfast", "faster", "fast", "medium", "slow"};
    std::string qv = std::to_string(util::clamp(c.rQuality, 0, 51));
    if (c.rRateMode == 1) {
        // a fixed bitrate (size of the video per second) instead of a quality number
        int kb = static_cast<int>(util::clamp(c.rBitrate, 0.5f, 1000.f) * 1000.f);
        std::string rate = "-b:v " + std::to_string(kb) + "k -maxrate " + std::to_string(kb * 3 / 2) + "k -bufsize " +
                           std::to_string(kb * 2) + "k";
        switch (enc) {
            case 0:
            case 4: return std::string("-preset ") + presets[util::clamp(c.rPresetIdx, 0, 6)] + " " + rate;
            case 1:
            case 5: return "-preset p5 -rc vbr " + rate;
            case 2: return "-quality quality -rc vbr_peak " + rate;
            default: return rate;
        }
    }
    switch (enc) {
        case 0:
        case 4: return std::string("-preset ") + presets[util::clamp(c.rPresetIdx, 0, 6)] + " -crf " + qv;
        case 1:
        case 5: return "-preset p5 -rc vbr -cq " + qv + " -b:v 0";
        case 2: return "-quality quality -rc cqp -qp_i " + qv + " -qp_p " + qv;
        case 3: return "-global_quality " + qv;
        default: return "";
    }
}

}  // namespace

// ---------------------------------------------------------------------------
//  Setup report
// ---------------------------------------------------------------------------

std::string saveDirStr() { return util::pathStr(Mod::get()->getSaveDir()); }

std::string outputDir() {
    if (cfg().renderDir[0]) return std::string(cfg().renderDir);
    return util::pathStr(Mod::get()->getSaveDir() / "renders");
}

std::string findFfmpeg() {
    auto const& c = cfg();
    std::error_code ec;
    if (c.ffmpegPath[0]) {
        auto p = util::u8path(c.ffmpegPath);
        if (isFile(p)) return util::pathStr(p);
        if (std::filesystem::is_directory(p, ec)) {
            if (isFile(p / "ffmpeg.exe")) return util::pathStr(p / "ffmpeg.exe");
            if (isFile(p / "bin" / "ffmpeg.exe")) return util::pathStr(p / "bin" / "ffmpeg.exe");
        }
    }
    auto save = Mod::get()->getSaveDir();
    std::filesystem::path const candidates[] = {
        save / "ffmpeg.exe",
        save / "ffmpeg" / "bin" / "ffmpeg.exe",
        save / "ffmpeg" / "ffmpeg.exe",
        dirs::getGameDir() / "ffmpeg.exe",
    };
    for (auto const& p : candidates)
        if (isFile(p)) return util::pathStr(p);

    wchar_t buf[MAX_PATH] = {};
    if (SearchPathW(nullptr, L"ffmpeg.exe", nullptr, MAX_PATH, buf, nullptr) > 0) return narrow(buf);
    return {};
}

Report report() {
    std::lock_guard<std::mutex> lock(g_reportMx);
    return g_report;
}

void runCheck() {
    {
        std::lock_guard<std::mutex> lock(g_reportMx);
        if (g_report.checking) return;
        g_report.checking = true;
    }
    std::string ff = findFfmpeg();
    std::string out = outputDir();
    std::thread([ff, out] {
        Report r;
        r.checked = true;
        r.ffmpegPath = ff;
        r.ffmpegFound = !ff.empty();
        r.outputDir = out;

        std::error_code ec;
        auto op = util::u8path(out);
        std::filesystem::create_directories(op, ec);
        {
            std::ofstream t(op / "write-test.tmp");
            r.outputOk = static_cast<bool>(t);
        }
        std::filesystem::remove(op / "write-test.tmp", ec);

        auto bi = builtin::probe();
        r.builtinOk = bi.ok;
        r.builtinGpu = bi.gpu;

        if (r.ffmpegFound) {
            std::string o;
            DWORD code = 1;
            if (runProcess(q(ff) + " -hide_banner -version", &o, &code, 8000)) {
                auto nl = o.find_first_of("\r\n");
                r.version = o.substr(0, nl == std::string::npos ? o.size() : nl);
                if (r.version.size() > 90) r.version.resize(90);
            }
            std::string enc;
            runProcess(q(ff) + " -hide_banner -encoders", &enc, &code, 8000);
            for (int i = 0; i < kEncCount; i++) {
                bool listed = enc.find(std::string(" ") + kEncNames[i] + " ") != std::string::npos;
                if (!listed) continue;
                if (i == 0 || i == 4) {
                    r.enc[i] = true;  // CPU encoders always work when listed
                    continue;
                }
                // GPU encoders: really try a tiny video to be sure the GPU supports it
                std::string cmd = q(ff) +
                    " -hide_banner -nostdin -loglevel error -f lavfi -i color=c=black:s=256x256:d=0.2 -frames:v 3 -c:v " +
                    kEncNames[i] + " -f null -";
                DWORD c2 = 1;
                r.enc[i] = runProcess(cmd, nullptr, &c2, 15000) && c2 == 0;
            }
        }
        std::lock_guard<std::mutex> lock(g_reportMx);
        g_report = r;
    }).detach();
}

std::string songPath(PlayLayer* pl) {
    if (!pl || !pl->m_level) return {};
    // Custom songs: ask the song manager. NONG mods like Jukebox answer with
    // the replacement song here (and for main levels, in getAudioFileName).
    if (pl->m_level->m_songID > 0) {
        std::string nong = MusicDownloadManager::sharedState()->pathForSong(pl->m_level->m_songID);
        if (!nong.empty() && isFile(util::u8path(nong))) return util::pathStr(util::u8path(nong));
    }
    std::string name = pl->m_level->getAudioFileName().c_str();
    if (name.empty()) return {};
    auto p = util::u8path(name);
    if (isFile(p)) return util::pathStr(p);
    auto a = dirs::getGameDir() / "Resources" / p;
    if (isFile(a)) return util::pathStr(a);
    std::string wp = CCFileUtils::sharedFileUtils()->getWritablePath().c_str();
    auto b = util::u8path(wp) / p;
    if (isFile(b)) return util::pathStr(b);
    auto c = dirs::getSaveDir() / p;
    if (isFile(c)) return util::pathStr(c);
    return {};
}

void openFolder(std::string const& path) {
    std::error_code ec;
    auto p = util::u8path(path);
    std::filesystem::create_directories(p, ec);
    ShellExecuteW(nullptr, L"open", p.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void openUrl(std::string const& url) {
    std::wstring w = widen(url);
    ShellExecuteW(nullptr, L"open", w.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// ---------------------------------------------------------------------------
//  Rendering
// ---------------------------------------------------------------------------

bool active() { return g.active; }
int engine() { return g.engine; }
int fps() { return g.fps > 0 ? g.fps : 60; }
int fpsFor(int idx) { return fpsFromIdx(idx); }
bool locksTime() { return g.active && !(g.engine == Builtin && g.realtime); }
int frames() { return g.frames; }
float videoSeconds() { return g.fps > 0 ? static_cast<float>(g.frames) / static_cast<float>(g.fps) : 0.f; }
double elapsed() { return g.active ? util::now() - g.startTime : 0.0; }

namespace {

// Draws the game into the big picture. The game thinks the "screen" is the
// size of the picture for a moment (the same trick other bots use), so
// everything, even shader effects and clipping, lines up.
void drawBig(CCScene* scene) {
    auto* director = CCDirector::sharedDirector();
    auto* view = director->getOpenGLView();
    if (!view || !g.rt) return;
    auto pts = director->getWinSize();
    if (pts.width <= 0.f || pts.height <= 0.f) return;

    float oldSX = view->m_fScaleX, oldSY = view->m_fScaleY;
    CCRect oldVP = view->m_obViewPortRect;
    GLint oldFbo = 0;
    GLfloat oldClear[4] = {0.f, 0.f, 0.f, 0.f};
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &oldFbo);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, oldClear);

    view->m_fScaleX = static_cast<float>(g.cw) / pts.width;
    view->m_fScaleY = static_cast<float>(g.ch) / pts.height;
    view->m_obViewPortRect = CCRect(0.f, 0.f, static_cast<float>(g.cw), static_cast<float>(g.ch));

    kmGLMatrixMode(KM_GL_PROJECTION);
    kmGLPushMatrix();
    kmGLMatrixMode(KM_GL_MODELVIEW);
    kmGLPushMatrix();
    glBindFramebuffer(GL_FRAMEBUFFER, g.rt->m_uFBO);
    director->setProjection(director->getProjection());  // camera + viewport for the big picture
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    scene->visit();
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, g.cw, g.ch, GL_RGB, GL_UNSIGNED_BYTE, g.buf.data());
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(oldFbo));

    view->m_fScaleX = oldSX;
    view->m_fScaleY = oldSY;
    view->m_obViewPortRect = oldVP;
    kmGLMatrixMode(KM_GL_PROJECTION);
    kmGLPopMatrix();
    kmGLMatrixMode(KM_GL_MODELVIEW);
    kmGLPopMatrix();
    director->setViewport();
    glClearColor(oldClear[0], oldClear[1], oldClear[2], oldClear[3]);
}

void dropTexture() {
    if (g.rt) {
        g.rt->release();
        g.rt = nullptr;
    }
}

// FFmpeg engine: start ffmpeg.exe hidden, with a pipe we can push frames into.
bool startFfmpeg(std::string const& ff, std::filesystem::path const& dir) {
    auto const& c = cfg();
    int enc = util::clamp(c.rEncIdx, 0, kEncCount - 1);
    {
        auto r = report();
        if (r.checked && !r.enc[enc]) {
            if (enc != 0)
                overlay::toast(std::string(kEncLabels[enc]) + " doesn't work on this PC, using CPU instead.", overlay::Warn);
            enc = 0;
        }
    }

    g.buf.assign(static_cast<size_t>(g.cw) * static_cast<size_t>(g.ch) * 3u, 0);
    int W = 1920, H = 1080;
    resolution(W, H);
    // Graphics-card H.264 encoders stop at about 4096 x 2304. Bigger videos
    // (5K, 8K) need H.265 on the GPU or the CPU encoder.
    if ((enc == 1 || enc == 2 || enc == 3) && (W > 4096 || H > 2304)) {
        auto r = report();
        int to = (enc == 1 && (!r.checked || r.enc[5])) ? 5 : 0;
        overlay::toast(std::string("This size is too big for ") + kEncLabels[enc] + ", using " + kEncLabels[to] + " instead.",
                       overlay::Warn);
        enc = to;
    }
    std::string extra = c.rExtra;
    std::string cmd = q(ff) + " -y -hide_banner -loglevel warning -f rawvideo -pix_fmt rgb24 -s " +
                      std::to_string(g.cw) + "x" + std::to_string(g.ch) + " -r " + std::to_string(g.fps) +
                      " -i - -vf \"vflip,scale=" + std::to_string(W) + ":" + std::to_string(H) +
                      ":flags=lanczos\" -c:v " + kEncNames[enc] + " " + qualityArgs(enc) + " -pix_fmt yuv420p " +
                      extra + " " + q(g.tmpPath);

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE rdh = nullptr, wrh = nullptr;
    if (!CreatePipe(&rdh, &wrh, &sa, 4u * 1024u * 1024u)) {
        overlay::toast("Could not create a pipe for FFmpeg.", overlay::Bad);
        return false;
    }
    SetHandleInformation(wrh, HANDLE_FLAG_INHERIT, 0);
    auto logPath = dir / "ffmpeg-log.txt";
    HANDLE logh = CreateFileW(logPath.wstring().c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    bool hasLog = logh != INVALID_HANDLE_VALUE;

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = rdh;
    si.hStdOutput = hasLog ? logh : nullptr;
    si.hStdError = hasLog ? logh : nullptr;
    PROCESS_INFORMATION pi{};
    std::wstring wcmd = widen(cmd);
    BOOL ok = CreateProcessW(nullptr, wcmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(rdh);
    if (hasLog) CloseHandle(logh);
    if (!ok) {
        CloseHandle(wrh);
        overlay::toast("Could not start FFmpeg. Check the path in the Setup report.", overlay::Bad);
        return false;
    }
    log::info("sketch render (FFmpeg): {}", cmd);
    g.hIn = wrh;
    g.pi = pi;
    g.realtime = false;
    return true;
}

// Built-in engine: Windows' own H.264 encoder, writes the .mp4 directly.
bool startBuiltin() {
    auto const& c = cfg();
    g.cw &= ~1;  // video sizes must be even
    g.ch &= ~1;
    g.buf.assign(static_cast<size_t>(g.cw) * static_cast<size_t>(g.ch) * 4u, 0);
    std::string err;
    unsigned br = builtinBitrate(g.cw, g.ch, g.fps);
    if (!g.enc.open(g.finalPath, g.cw, g.ch, g.fps, br, err)) {
        overlay::toast(err, overlay::Bad);
        return false;
    }
    log::info("sketch render (built-in): {}x{} @ {} fps, {} bit/s", g.cw, g.ch, g.fps, br);
    g.realtime = c.rBRealtime;
    g.acc = 0.0;
    return true;
}

}  // namespace

namespace {

// Waits for FFmpeg to finish the video, then adds the sound (a song or a
// recording) in the background.
void muxLater(PROCESS_INFORMATION pi, std::string ff, std::string tmp, std::string fin, std::string audio, float off,
              bool deleteAudio, bool keep, int frameCount) {
    std::thread([=] {
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD code = 1;
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        std::error_code ec;
        auto tmpP = util::u8path(tmp);
        auto finP = util::u8path(fin);
        auto cleanup = [&] {
            if (deleteAudio && !audio.empty()) std::filesystem::remove(util::u8path(audio), ec);
        };
        if (!keep || frameCount == 0) {
            std::filesystem::remove(tmpP, ec);
            cleanup();
            overlay::toast("Render cancelled.", overlay::Info);
            return;
        }
        if (code != 0 || !isFile(tmpP)) {
            cleanup();
            overlay::toast("FFmpeg failed. Open ffmpeg-log.txt in the output folder to see why.", overlay::Bad);
            return;
        }
        if (!audio.empty() && isFile(util::u8path(audio))) {
            char offs[32];
            std::snprintf(offs, sizeof(offs), "%.3f", off >= 0.f ? off : -off);
            std::string audioIn = (off >= 0.f ? std::string(" -ss ") : std::string(" -itsoffset ")) + offs + " -i " + q(audio);
            // apad + shortest: the sound never cuts the video short
            std::string cmd = q(ff) + " -y -hide_banner -nostdin -loglevel error -i " + q(tmp) + audioIn +
                              " -map 0:v -map 1:a -c:v copy -c:a aac -b:a 192k -af apad -shortest " + q(fin);
            DWORD c2 = 1;
            if (runProcess(cmd, nullptr, &c2, INFINITE) && c2 == 0) {
                std::filesystem::remove(tmpP, ec);
                cleanup();
                overlay::toast("Video saved: " + util::pathStr(finP.filename()), overlay::Good);
                return;
            }
            overlay::toast("Couldn't add the sound, saved the video without it.", overlay::Warn);
        }
        cleanup();
        std::filesystem::rename(tmpP, finP, ec);
        overlay::toast("Video saved: " + util::pathStr(finP.filename()), overlay::Good);
    }).detach();
}

// Starts the sound pass: the same macro plays again at normal speed.
bool beginPass(int frameCount) {
    auto pl = hacks::playLayer();
    if (!pl || bot::macro().inputs.empty()) return false;
    g_pass = Pass{};
    g_pass.pi = g.pi;
    g_pass.ff = g.ffmpeg;
    g_pass.tmp = g.tmpPath;
    g_pass.fin = g.finalPath;
    g_pass.song = g.song;
    g_pass.songOffset = g.songOffset;
    g_pass.videoSeconds = static_cast<double>(frameCount) / static_cast<double>(util::maxv(1, g.fps));
    std::string base = g.tmpPath;
    auto dot = base.rfind(".video.mp4");
    g_pass.wav = (dot != std::string::npos ? base.substr(0, dot) : base) + ".sound.wav";
    g_pass.active = true;
    bot::startPlay();  // restarts the level and plays the macro again
    if (!audiocap::start()) {
        g_pass.active = false;
        overlay::toast("Couldn't record the game's sound. Using the song file instead.", overlay::Warn);
        return false;
    }
    g_pass.started = util::now();
    overlay::toast("Video done! Now recording the sound: the level plays once more at normal speed. Don't touch anything!",
                   overlay::Info);
    return true;
}

// Done (or stopped): put the recording (or the song, if it went wrong) in the video.
void endPass(bool ok, std::string const& why) {
    if (!g_pass.active) return;
    g_pass.active = false;
    int rate = 44100;
    std::vector<int16_t> samples = audiocap::take(rate);
    audiocap::stop();
    if (ok && !samples.empty() && audiocap::writeWav(g_pass.wav, samples, rate)) {
        // the sound is mixed a little after the game asks for it: nudge it back
        float off = static_cast<float>(audiocap::latency() * 0.5) + cfg().rRecDelay;
        muxLater(g_pass.pi, g_pass.ff, g_pass.tmp, g_pass.fin, g_pass.wav, off, true, true, 1);
        overlay::toast("Sound recorded! Putting it into the video...", overlay::Good);
        return;
    }
    if (!why.empty()) overlay::toast(why, overlay::Warn);
    muxLater(g_pass.pi, g_pass.ff, g_pass.tmp, g_pass.fin, g_pass.song, g_pass.songOffset, false, true, 1);
}

}  // namespace

bool start(PlayLayer* pl, bool restartLevel) {
    if (g.active) return true;
    if (g_pass.active) return false;  // still recording the sound of the last video
    if (predictor::active()) {
        overlay::toast("Wait for Predict to finish first.", overlay::Warn);
        return false;
    }
    if (!pl) {
        overlay::toast("Open a level first, then press Start.", overlay::Warn);
        return false;
    }
    auto const& c = cfg();
    int eng = (c.rEngine == Builtin) ? Builtin : FFmpeg;

    std::string ff;
    if (eng == FFmpeg) {
        ff = findFfmpeg();
        if (ff.empty()) {
            overlay::toast("FFmpeg is missing! See Render > Setup report, or pick the Built-in renderer.", overlay::Bad);
            return false;
        }
    }

    std::error_code ec;
    auto dir = util::u8path(outputDir());
    std::filesystem::create_directories(dir, ec);

    std::string level = (pl->m_level) ? std::string(pl->m_level->m_levelName.c_str()) : std::string("level");
    std::time_t tt = std::time(nullptr);
    std::tm tmv = util::localTime(tt);
    char ts[32];
    std::strftime(ts, sizeof(ts), "%Y-%m-%d_%H-%M-%S", &tmv);
    std::string base = util::safeFileName(level) + "_" + ts;

    g.engine = eng;
    g.tmpPath = util::pathStr(dir / util::u8path(base + ".video.mp4"));
    g.finalPath = util::pathStr(dir / util::u8path(base + ".mp4"));
    g.song = (eng == FFmpeg && c.rMusic) ? songPath(pl) : std::string();
    g.songOffset = c.rMusicOffset;
    g.recordAudio = false;
    if (eng == FFmpeg && c.rMusic && c.rAudioMode == 1) {
        if (bot::state() == bot::State::Play) g.recordAudio = true;
        else overlay::toast("\"Record the game\" needs a macro playing (Bot tab). Using the song file instead.", overlay::Warn);
    }
    static int const builtinFps[] = {30, 60, 120, 144, 240};
    g.fps = (eng == Builtin) ? builtinFps[util::clamp(c.rBFpsIdx, 0, 4)] : fpsFromIdx(c.rFpsIdx);
    g.ffmpeg = ff;
    if (eng == FFmpeg && c.rMusic && !g.recordAudio && g.song.empty())
        overlay::toast("Couldn't find the level's song - the video will have no music.", overlay::Warn);

    auto* director = CCDirector::sharedDirector();
    auto winSize = director->getWinSize();
    int mkW = static_cast<int>(winSize.width), mkH = static_cast<int>(winSize.height);
    g.sharp = false;
    if (eng == FFmpeg) {
        // Draw the game at least as big as the video, so 1440p / 4K / 8K are
        // really sharp (not a small picture stretched up). Same shape as the window.
        int W = 1920, H = 1080;
        resolution(W, H);
        float csf = director->getContentScaleFactor();
        if (csf <= 0.f) csf = 1.f;
        double baseW = static_cast<double>(winSize.width) * csf, baseH = static_cast<double>(winSize.height) * csf;
        double s = util::maxv(1.0, util::maxv(W / baseW, H / baseH));
        GLint maxTex = 0;
        GLint maxView[2] = {0, 0};
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTex);
        glGetIntegerv(GL_MAX_VIEWPORT_DIMS, maxView);
        double limW = maxTex > 0 ? maxTex : 8192.0, limH = limW;
        if (maxView[0] > 0) limW = util::minv(limW, static_cast<double>(maxView[0]));
        if (maxView[1] > 0) limH = util::minv(limH, static_cast<double>(maxView[1]));
        double fit = util::minv(limW / (baseW * s), limH / (baseH * s));
        if (fit < 1.0) {
            s *= fit * 0.995;
            char msg[160];
            std::snprintf(msg, sizeof(msg), "Your graphics card can draw up to %d x %d, so this video will be a little softer.",
                          static_cast<int>(baseW * s), static_cast<int>(baseH * s));
            overlay::toast(msg, overlay::Warn);
        }
        if (s > 1.001) {
            mkW = static_cast<int>(std::ceil(winSize.width * s));
            mkH = static_cast<int>(std::ceil(winSize.height * s));
            g.sharp = true;
        }
    }
    g.rt = CCRenderTexture::create(mkW, mkH);
    if (!g.rt) {
        overlay::toast("Could not create the render texture.", overlay::Bad);
        return false;
    }
    g.rt->retain();
    auto px = g.rt->getSprite()->getTexture()->getContentSizeInPixels();
    g.cw = static_cast<int>(px.width);
    g.ch = static_cast<int>(px.height);

    bool ok = (eng == Builtin) ? startBuiltin() : startFfmpeg(ff, dir);
    if (!ok) {
        dropTexture();
        return false;
    }

    g.active = true;
    g.frames = 0;
    g.startTime = util::now();
    g.endCountdown = -1.f;
    if (eng == FFmpeg) overlay::toast("Rendering with FFmpeg... the game will look slow, that's normal!", overlay::Info);
    else if (g.realtime) overlay::toast("Recording with the built-in renderer. Play normally!", overlay::Info);
    else overlay::toast("Rendering with the built-in renderer... the game will look slow.", overlay::Info);
    if (c.rCloseMenu && gui::isOpen()) gui::toggle();
    if (restartLevel) hacks::restartFromStart(pl);
    return true;
}

void capture(float realDt) {
    if (!g.active || !g.rt) return;

    // How much video time passes this frame, and how many video frames to make.
    float advanced = 1.f / static_cast<float>(fps());
    int copies = 1;
    if (g.engine == Builtin && g.realtime) {
        // Real-time: the game runs at normal speed, we grab a picture whenever a
        // video frame is due. Fast, but if the game stutters the video does too.
        advanced = realDt;
        g.acc += realDt;
        double step = 1.0 / static_cast<double>(fps());
        int due = static_cast<int>(g.acc / step);
        copies = 0;
        if (due > 0) {
            g.acc -= static_cast<double>(due) * step;
            copies = util::minv(due, 4);  // after a big hitch, skip ahead instead of freezing
        }
    }

    if (copies > 0) {
        auto scene = CCDirector::sharedDirector()->getRunningScene();
        if (!scene) return;
        bool builtinFmt = (g.engine == Builtin);
        if (g.sharp) {
            drawBig(scene);
        } else {
            g.rt->beginWithClear(0.f, 0.f, 0.f, 1.f);
            scene->visit();
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(0, 0, g.cw, g.ch, builtinFmt ? GL_BGRA : GL_RGB, GL_UNSIGNED_BYTE, g.buf.data());
            g.rt->end();
        }

        if (builtinFmt) {
            std::string err;
            if (!g.enc.write(g.buf.data(), copies, err)) {
                overlay::toast(err, overlay::Bad);
                stop(false);
                return;
            }
        } else {
            DWORD written = 0;
            BOOL ok = WriteFile(static_cast<HANDLE>(g.hIn), g.buf.data(), static_cast<DWORD>(g.buf.size()), &written, nullptr);
            if (!ok) {
                overlay::toast("FFmpeg stopped early. Open ffmpeg-log.txt in the output folder to see why.", overlay::Bad);
                stop(false);
                return;
            }
        }
        g.frames += copies;
    }

    if (g.endCountdown >= 0.f) {
        g.endCountdown -= advanced;
        if (g.endCountdown <= 0.f) stop(true);
    }
}

void stop(bool keep) {
    if (!g.active) return;
    g.active = false;
    dropTexture();

    if (g.engine == Builtin) {
        int frameCount = g.frames;
        bool ok = g.enc.close();  // finishes the .mp4 (takes a moment)
        std::error_code ec;
        auto finP = util::u8path(g.finalPath);
        if (!keep || frameCount == 0) {
            std::filesystem::remove(finP, ec);
            overlay::toast("Render cancelled.", overlay::Info);
            return;
        }
        if (!ok || !isFile(finP)) {
            overlay::toast("The built-in renderer couldn't finish the video. Try FFmpeg.", overlay::Bad);
            return;
        }
        overlay::toast("Video saved: " + util::pathStr(finP.filename()), overlay::Good);
        return;
    }

    HANDLE in = static_cast<HANDLE>(g.hIn);
    g.hIn = nullptr;
    if (in) CloseHandle(in);  // tells FFmpeg "no more frames"

    PROCESS_INFORMATION pi = g.pi;
    int frameCount = g.frames;
    if (keep && frameCount > 0 && g.recordAudio && beginPass(frameCount)) return;
    muxLater(pi, g.ffmpeg, g.tmpPath, g.finalPath, g.song, g.songOffset, false, keep, frameCount);
}

bool screenshot(std::string& savedPath) {
    auto scene = CCDirector::sharedDirector()->getRunningScene();
    if (!scene) return false;
    auto ws = CCDirector::sharedDirector()->getWinSize();
    auto rt = CCRenderTexture::create(static_cast<int>(ws.width), static_cast<int>(ws.height));
    if (!rt) return false;
    auto px = rt->getSprite()->getTexture()->getContentSizeInPixels();
    int w = static_cast<int>(px.width), h = static_cast<int>(px.height);
    std::vector<uint8_t> buf(static_cast<size_t>(w) * static_cast<size_t>(h) * 3u);
    rt->beginWithClear(0.f, 0.f, 0.f, 1.f);
    scene->visit();
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, buf.data());
    rt->end();

    auto bytes = png::encodeRgb(buf.data(), w, h, true);
    std::error_code ec;
    auto dir = Mod::get()->getSaveDir() / "screenshots";
    std::filesystem::create_directories(dir, ec);
    std::string name = "menu";
    if (auto pl = GameManager::sharedState()->m_playLayer; pl && pl->m_level) name = pl->m_level->m_levelName.c_str();
    std::time_t tt = std::time(nullptr);
    std::tm tmv = util::localTime(tt);
    char ts[32];
    std::strftime(ts, sizeof(ts), "%Y-%m-%d_%H-%M-%S", &tmv);
    auto path = dir / util::u8path(util::safeFileName(name) + "_" + ts + ".png");
    std::ofstream o(path, std::ios::binary | std::ios::trunc);
    if (!o) return false;
    o.write(reinterpret_cast<char const*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    savedPath = util::pathStr(path);
    return static_cast<bool>(o);
}

void onLevelComplete() {
    if (g.active) g.endCountdown = util::maxv(0.02f, cfg().rEndPad);
    if (g_pass.active && !g_pass.levelDone) {
        g_pass.levelDone = true;
        g_pass.endCountdown = util::maxv(0.02f, cfg().rEndPad);
    }
}

void onDeath() {
    if (g_pass.active && !g_pass.levelDone)
        endPass(false, "The macro died while recording the sound, so the video got the song file instead.");
}

void tick(float realDt) {
    if (!g_pass.active) return;
    double running = util::now() - g_pass.started;
    if (g_pass.levelDone) {
        g_pass.endCountdown -= realDt;
        if (g_pass.endCountdown <= 0.f) endPass(true, "");
        return;
    }
    if (running > 1.0 && bot::state() != bot::State::Play) {
        endPass(false, "The macro stopped before the end, so the video got the song file instead.");
        return;
    }
    if (running > g_pass.videoSeconds + 30.0) endPass(true, "");  // safety: never record forever
}

bool audioPassActive() { return g_pass.active; }
double audioPassSeconds() { return g_pass.active ? util::now() - g_pass.started : 0.0; }
void cancelAudioPass() { endPass(false, "Sound recording stopped. The video gets the song file instead."); }

void onQuit() {
    g.recordAudio = false;  // the level is closing: no sound pass
    stop(true);
    if (g_pass.active) endPass(false, "Left the level while recording the sound. The video gets the song file instead.");
}

}  // namespace render
