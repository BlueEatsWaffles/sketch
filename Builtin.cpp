// Built-in renderer: Windows Media Foundation H.264 encoder.
// This file talks only to Windows, so it does not include any Geode headers.
#include "Builtin.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
// initguid.h makes the Media Foundation GUIDs get defined right here,
// so we don't depend on which .lib happens to contain them.
#include <initguid.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include <cstring>
#include <vector>

namespace builtin {

namespace {

template <class T>
void release(T*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}

std::wstring widen(std::string const& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

UINT32 countEncoders(UINT32 flags) {
    MFT_REGISTER_TYPE_INFO outType{MFMediaType_Video, MFVideoFormat_H264};
    IMFActivate** acts = nullptr;
    UINT32 n = 0;
    if (FAILED(MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER, flags, nullptr, &outType, &acts, &n))) return 0;
    for (UINT32 i = 0; i < n; i++) acts[i]->Release();
    CoTaskMemFree(acts);
    return n;
}

struct Impl {
    IMFSinkWriter* writer = nullptr;
    DWORD stream = 0;
    int w = 0, h = 0, fps = 60;
    long long frame = 0;
    bool mfStarted = false;
    bool writing = false;
    bool failed = false;
    std::wstring path;
};

}  // namespace

Info probe() {
    Info info;
    HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_LITE))) {
        info.ok = countEncoders(MFT_ENUM_FLAG_ALL) > 0;
        info.gpu = countEncoders(MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_SORTANDFILTER) > 0;
        MFShutdown();
    }
    if (SUCCEEDED(co)) CoUninitialize();
    return info;
}

Encoder::~Encoder() { close(); }

bool Encoder::open(std::string const& pathUtf8, int width, int height, int fps, unsigned bitrate, std::string& err) {
    close();
    auto* m = new Impl();
    m_impl = m;
    m->w = width & ~1;
    m->h = height & ~1;
    m->fps = fps > 0 ? fps : 60;
    m->path = widen(pathUtf8);

    // The game's main thread normally has COM ready already; this is just in case.
    // (If it was set up differently we get RPC_E_CHANGED_MODE, which is fine.)
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_LITE))) {
        err = "Windows' video encoder isn't available on this PC. Use the FFmpeg renderer.";
        close();
        return false;
    }
    m->mfStarted = true;

    IMFAttributes* attr = nullptr;
    MFCreateAttributes(&attr, 2);
    if (attr) attr->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);  // use the GPU if we can
    HRESULT hr = MFCreateSinkWriterFromURL(m->path.c_str(), nullptr, attr, &m->writer);
    release(attr);
    if (FAILED(hr) || !m->writer) {
        err = "Couldn't create the video file. Is the output folder OK?";
        close();
        return false;
    }

    // what we want out: H.264 video
    IMFMediaType* out = nullptr;
    MFCreateMediaType(&out);
    out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    out->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    out->SetUINT32(MF_MT_AVG_BITRATE, bitrate);
    out->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    MFSetAttributeSize(out, MF_MT_FRAME_SIZE, static_cast<UINT32>(m->w), static_cast<UINT32>(m->h));
    MFSetAttributeRatio(out, MF_MT_FRAME_RATE, static_cast<UINT32>(m->fps), 1);
    MFSetAttributeRatio(out, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    hr = m->writer->AddStream(out, &m->stream);
    release(out);
    if (FAILED(hr)) {
        err = "Windows' encoder doesn't like this video size. Try a smaller game window or use FFmpeg.";
        close();
        return false;
    }

    // what we put in: plain BGRA pictures, top row first
    IMFMediaType* in = nullptr;
    MFCreateMediaType(&in);
    in->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    in->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    in->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    in->SetUINT32(MF_MT_DEFAULT_STRIDE, static_cast<UINT32>(m->w * 4));
    MFSetAttributeSize(in, MF_MT_FRAME_SIZE, static_cast<UINT32>(m->w), static_cast<UINT32>(m->h));
    MFSetAttributeRatio(in, MF_MT_FRAME_RATE, static_cast<UINT32>(m->fps), 1);
    MFSetAttributeRatio(in, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    hr = m->writer->SetInputMediaType(m->stream, in, nullptr);
    release(in);
    if (FAILED(hr)) {
        err = "Windows' encoder can't take this video size/frame rate. Try 30 FPS or use FFmpeg.";
        close();
        return false;
    }

    if (FAILED(m->writer->BeginWriting())) {
        err = "Windows' encoder refused to start. Use the FFmpeg renderer.";
        close();
        return false;
    }
    m->writing = true;
    return true;
}

bool Encoder::write(uint8_t const* pixels, int copies, std::string& err) {
    auto* m = static_cast<Impl*>(m_impl);
    if (!m || !m->writing || m->failed) return false;
    if (copies <= 0) return true;

    DWORD const rowBytes = static_cast<DWORD>(m->w) * 4u;
    DWORD const size = rowBytes * static_cast<DWORD>(m->h);
    IMFMediaBuffer* buf = nullptr;
    if (FAILED(MFCreateMemoryBuffer(size, &buf)) || !buf) {
        err = "Out of memory while making the video.";
        m->failed = true;
        return false;
    }
    BYTE* dst = nullptr;
    if (SUCCEEDED(buf->Lock(&dst, nullptr, nullptr))) {
        // OpenGL gives the bottom row first; video wants the top row first.
        for (int y = 0; y < m->h; y++) {
            std::memcpy(dst + static_cast<size_t>(y) * rowBytes,
                        pixels + static_cast<size_t>(m->h - 1 - y) * rowBytes, rowBytes);
        }
        buf->Unlock();
    }
    buf->SetCurrentLength(size);

    bool ok = true;
    for (int i = 0; i < copies && ok; i++) {
        IMFSample* sample = nullptr;
        MFCreateSample(&sample);
        if (!sample) {
            ok = false;
            break;
        }
        sample->AddBuffer(buf);
        // times are in 100-nanosecond units
        LONGLONG t0 = m->frame * 10000000LL / m->fps;
        LONGLONG t1 = (m->frame + 1) * 10000000LL / m->fps;
        sample->SetSampleTime(t0);
        sample->SetSampleDuration(t1 - t0);
        if (FAILED(m->writer->WriteSample(m->stream, sample))) ok = false;
        release(sample);
        m->frame++;
    }
    release(buf);
    if (!ok) {
        err = "Windows' encoder stopped working. Try the FFmpeg renderer.";
        m->failed = true;
    }
    return ok;
}

bool Encoder::close() {
    auto* m = static_cast<Impl*>(m_impl);
    if (!m) return true;
    m_impl = nullptr;
    bool ok = !m->failed;
    if (m->writer) {
        if (m->writing && FAILED(m->writer->Finalize())) ok = false;
        release(m->writer);
    }
    if (m->mfStarted) MFShutdown();
    if (!m->writing) ok = false;
    delete m;
    return ok;
}

}  // namespace builtin
