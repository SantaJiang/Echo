#include "audio/convert.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
extern "C" {
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
}

namespace echo {
namespace {
constexpr int MaxChannels = 64;
template<typename T> T load(const uint8_t* p) { T v; std::memcpy(&v, p, sizeof(v)); return v; }

double readSample(const uint8_t* p, const SampleFormat& f)
{
    if (f.isFloat) return f.bytesPerSample == 8 ? load<double>(p) : load<float>(p);
    if (f.bytesPerSample == 1) return (int(*p) - 128) / 128.;
    if (f.bytesPerSample == 2) return load<int16_t>(p) / 32768.;
    if (f.bytesPerSample == 3) {
        int32_t v = p[0] | (p[1] << 8) | (p[2] << 16);
        if (v & 0x800000) v |= static_cast<int32_t>(0xff000000);
        return v / 8388608.;
    }
    const int bits = f.bitsPerSample > 0 ? f.bitsPerSample : 32;
    return load<int32_t>(p) / std::ldexp(1., f.highAligned ? 31 : bits - 1);
}

double randomUnit(uint32_t* state)
{
    uint32_t x = *state ? *state : 0xC0FFEEU;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    *state = x;
    return x / 4294967296.;
}

void writeSample(uint8_t* p, double value, const DeviceFormat& f, uint32_t* dither)
{
    if (!std::isfinite(value)) value = 0;
    if (f.isFloat) {
        if (f.containerBytes == 8) std::memcpy(p, &value, 8);
        else { const float v = static_cast<float>(value); std::memcpy(p, &v, 4); }
        return;
    }
    const int bits = std::clamp(f.bitsPerSample, 1, f.containerBytes * 8);
    const double scale = std::ldexp(1., bits - 1);
    // TPDF at the actual destination bit depth, before rounding or packing.
    double scaled = std::clamp(value, -1., 1.) * scale;
    if (dither && value != 0) scaled += randomUnit(dither) - randomUnit(dither);
    const int64_t v = static_cast<int64_t>(std::llround(std::clamp(scaled, -scale, scale - 1)));
    uint64_t word = static_cast<uint64_t>(v);
    if (f.highAligned) word <<= f.containerBytes * 8 - bits;
    if (f.containerBytes == 1) word = static_cast<uint64_t>(v + 128);
    for (int b = 0; b < f.containerBytes; ++b) p[b] = static_cast<uint8_t>(word >> (b * 8));
}

struct Matrix {
    int in = 0, out = 0;
    quint64 mask = 0, outMask = 0;
    std::array<double, MaxChannels * MaxChannels> values {};
    void configure(const SampleFormat& f, const DeviceFormat& d)
    {
        if (in == f.channels && out == d.channels && mask == f.channelMask && outMask == d.channelMask) return;
        in = f.channels; out = d.channels; mask = f.channelMask; outMask = d.channelMask;
        values.fill(0);
        if (in == out && (!outMask || !mask || outMask == mask)) {
            for (int c = 0; c < in; ++c) values[c * MaxChannels + c] = 1;
            return;
        }
        if (in == 1 && out == 2) { values[0] = values[MaxChannels] = 1; return; }
        AVChannelLayout a {}, b {};
        if (mask) av_channel_layout_from_mask(&a, mask); else av_channel_layout_default(&a, in);
        if (outMask) av_channel_layout_from_mask(&b, outMask); else av_channel_layout_default(&b, out);
        if (a.nb_channels != in || b.nb_channels != out
            || swr_build_matrix2(&a, &b, 0.7071067811865475244, 0.7071067811865475244, 0., 1., 1., values.data(), MaxChannels,
                                 AV_MATRIX_ENCODING_NONE, nullptr) < 0) {
            values.fill(0);
            for (int c = 0; c < std::min(in, out); ++c) values[c * MaxChannels + c] = 1;
        }
        av_channel_layout_uninit(&a); av_channel_layout_uninit(&b);
    }
};
} // namespace

void convertSamples(const uint8_t* src, const SampleFormat& source, int frames, uint8_t* dst, const DeviceFormat& output,
                    float gain, uint32_t* ditherState)
{
    if (frames <= 0 || source.channels < 1 || output.channels < 1 || source.channels > MaxChannels
        || output.channels > MaxChannels || source.bytesPerSample < 1 || source.bytesPerSample > 8
        || output.containerBytes < 1 || output.containerBytes > 8) return;
    const bool sameLayout = source.channels == output.channels
        && (!output.channelMask || !source.channelMask || source.channelMask == output.channelMask);
    const bool sameStorage = source.isFloat == output.isFloat && source.bytesPerSample == output.containerBytes
        && source.bitsPerSample == output.bitsPerSample
        && source.highAligned == output.highAligned;
    if (gain == 1.f && sameLayout && sameStorage) {
        std::memmove(dst, src, static_cast<size_t>(frames) * source.bytesPerFrame()); return;
    }
    thread_local Matrix matrix;
    matrix.configure(source, output);
    const bool quantizing = source.isFloat || gain != 1.f || !sameLayout || source.bitsPerSample > output.bitsPerSample;
    uint32_t* dither = quantizing ? ditherState : nullptr;
    std::array<double, MaxChannels> samples {};
    for (int frame = 0; frame < frames; ++frame) {
        for (int c = 0; c < source.channels; ++c)
            samples[c] = readSample(src + static_cast<size_t>(frame * source.channels + c) * source.bytesPerSample, source);
        for (int c = 0; c < output.channels; ++c) {
            double value = 0;
            for (int i = 0; i < source.channels; ++i) value += samples[i] * matrix.values[c * MaxChannels + i];
            writeSample(dst + static_cast<size_t>(frame * output.channels + c) * output.containerBytes,
                        value * static_cast<double>(gain), output, dither);
        }
    }
}
} // namespace echo
