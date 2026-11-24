#include "AudioCap.hpp"
#include "Util.hpp"

#include <Geode/Geode.hpp>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <mutex>

using namespace geode::prelude;

namespace audiocap {

namespace {

std::mutex g_mx;
std::vector<int16_t> g_samples;  // stereo, interleaved
FMOD::DSP* g_dsp = nullptr;
FMOD::ChannelGroup* g_master = nullptr;
int g_rate = 44100;
double g_latency = 0.0;

int16_t toPcm(float v) {
    v = std::clamp(v, -1.f, 1.f);
    return static_cast<int16_t>(v * 32767.f);
}

// Runs on FMOD's mixer thread: pass the sound through untouched and keep a copy.
FMOD_RESULT F_CALL readCallback(FMOD_DSP_STATE*, float* in, float* out, unsigned int length, int inChannels, int* outChannels) {
    int outCh = outChannels ? *outChannels : inChannels;
    for (unsigned int s = 0; s < length; s++) {
        for (int ch = 0; ch < outCh; ch++) {
            out[s * static_cast<unsigned int>(outCh) + static_cast<unsigned int>(ch)] =
                ch < inChannels ? in[s * static_cast<unsigned int>(inChannels) + static_cast<unsigned int>(ch)] : 0.f;
        }
    }
    if (inChannels <= 0) return FMOD_OK;
    std::lock_guard<std::mutex> lock(g_mx);
    size_t base = g_samples.size();
    g_samples.resize(base + static_cast<size_t>(length) * 2u);
    for (unsigned int s = 0; s < length; s++) {
        float l = in[s * static_cast<unsigned int>(inChannels)];
        float r = inChannels > 1 ? in[s * static_cast<unsigned int>(inChannels) + 1u] : l;
        g_samples[base + s * 2u] = toPcm(l);
        g_samples[base + s * 2u + 1u] = toPcm(r);
    }
    return FMOD_OK;
}

FMOD::System* fmodSystem() {
    auto engine = FMODAudioEngine::sharedEngine();
    return engine ? engine->m_system : nullptr;
}

}  // namespace

bool start() {
    stop();
    auto sys = fmodSystem();
    if (!sys) return false;
    {
        std::lock_guard<std::mutex> lock(g_mx);
        g_samples.clear();
        g_samples.reserve(44100u * 2u * 120u);
    }
    int rate = 0;
    if (sys->getSoftwareFormat(&rate, nullptr, nullptr) == FMOD_OK && rate > 0) g_rate = rate;
    unsigned int bufLen = 0;
    int bufCount = 0;
    g_latency = 0.0;
    if (sys->getDSPBufferSize(&bufLen, &bufCount) == FMOD_OK && g_rate > 0)
        g_latency = static_cast<double>(bufLen) * static_cast<double>(util::maxv(1, bufCount)) / static_cast<double>(g_rate);

    FMOD_DSP_DESCRIPTION desc;
    std::memset(&desc, 0, sizeof(desc));
    desc.pluginsdkversion = FMOD_PLUGIN_SDK_VERSION;
    std::strncpy(desc.name, "sketch sound recorder", sizeof(desc.name) - 1);
    desc.version = 0x00010000;
    desc.numinputbuffers = 1;
    desc.numoutputbuffers = 1;
    desc.read = readCallback;

    FMOD::DSP* dsp = nullptr;
    if (sys->createDSP(&desc, &dsp) != FMOD_OK || !dsp) return false;
    FMOD::ChannelGroup* master = nullptr;
    if (sys->getMasterChannelGroup(&master) != FMOD_OK || !master) {
        dsp->release();
        return false;
    }
    if (master->addDSP(FMOD_CHANNELCONTROL_DSP_HEAD, dsp) != FMOD_OK) {
        dsp->release();
        return false;
    }
    g_dsp = dsp;
    g_master = master;
    return true;
}

void stop() {
    if (g_dsp) {
        if (g_master) g_master->removeDSP(g_dsp);
        g_dsp->release();
    }
    g_dsp = nullptr;
    g_master = nullptr;
}

bool running() { return g_dsp != nullptr; }
double latency() { return g_latency; }

std::vector<int16_t> take(int& sampleRate) {
    sampleRate = g_rate;
    std::lock_guard<std::mutex> lock(g_mx);
    std::vector<int16_t> out;
    out.swap(g_samples);
    return out;
}

bool writeWav(std::string const& pathUtf8, std::vector<int16_t> const& samples, int sampleRate) {
    std::ofstream o(util::u8path(pathUtf8), std::ios::binary | std::ios::trunc);
    if (!o) return false;
    auto u32 = [&](uint32_t v) { o.write(reinterpret_cast<char const*>(&v), 4); };
    auto u16 = [&](uint16_t v) { o.write(reinterpret_cast<char const*>(&v), 2); };
    uint32_t dataBytes = static_cast<uint32_t>(samples.size() * sizeof(int16_t));
    o.write("RIFF", 4);
    u32(36u + dataBytes);
    o.write("WAVE", 4);
    o.write("fmt ", 4);
    u32(16);
    u16(1);  // PCM
    u16(2);  // stereo
    u32(static_cast<uint32_t>(sampleRate));
    u32(static_cast<uint32_t>(sampleRate) * 4u);
    u16(4);
    u16(16);
    o.write("data", 4);
    u32(dataBytes);
    o.write(reinterpret_cast<char const*>(samples.data()), static_cast<std::streamsize>(dataBytes));
    return static_cast<bool>(o);
}

}  // namespace audiocap
