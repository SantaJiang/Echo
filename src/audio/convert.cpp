#include "audio/convert.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace echo {
namespace {

float nextUnit(uint32_t* state)
{
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x ? x : 0xA5A5A5A5u;
    return static_cast<float>(*state >> 8) * (1.0f / 16777216.0f);
}

float readSample(const uint8_t* src, const SampleFormat& fmt)
{
    if (fmt.isFloat) {
        float value = 0;
        std::memcpy(&value, src, sizeof(float));
        return value;
    }
    if (fmt.bytesPerSample == 2) {
        int16_t value = 0;
        std::memcpy(&value, src, sizeof(value));
        return static_cast<float>(value) / 32768.0f;
    }
    int32_t value = 0;
    std::memcpy(&value, src, sizeof(value));
    const int shift = std::clamp(32 - fmt.bitsPerSample, 0, 31);
    if (fmt.bitsPerSample > 0 && fmt.bitsPerSample < 32) {
        const int32_t sign = 1 << (fmt.bitsPerSample - 1);
        value &= (sign << 1) - 1;
        if (value & sign) {
            value |= ~((sign << 1) - 1);
        }
    }
    return static_cast<float>(value) / static_cast<float>(1u << std::min(shift == 0 ? 31 : (fmt.bitsPerSample - 1), 31));
}

void writeSample(uint8_t* dst, float sample, const DeviceFormat& fmt, uint32_t* ditherState, bool dither)
{
    sample = std::clamp(sample, -1.0f, 1.0f);
    if (fmt.isFloat) {
        std::memcpy(dst, &sample, sizeof(float));
        return;
    }
    const int bits = std::clamp(fmt.bitsPerSample, 8, 32);
    const double scale = static_cast<double>(1u << std::min(bits - 1, 31));
    double shaped = static_cast<double>(sample);
    if (dither && bits < 24) {
        const float lsb = static_cast<float>(1.0 / scale);
        shaped += static_cast<double>((nextUnit(ditherState) - nextUnit(ditherState)) * lsb);
    }
    const auto maxValue = static_cast<int64_t>(scale) - 1;
    const auto minValue = -static_cast<int64_t>(scale);
    int64_t quantized = std::llround(shaped * scale);
    quantized = std::clamp(quantized, minValue, maxValue);

    if (fmt.containerBytes == 2) {
        const auto value = static_cast<int16_t>(quantized);
        std::memcpy(dst, &value, sizeof(value));
        return;
    }
    if (fmt.containerBytes == 3) {
        const auto value = static_cast<int32_t>(quantized);
        dst[0] = static_cast<uint8_t>(value);
        dst[1] = static_cast<uint8_t>(value >> 8);
        dst[2] = static_cast<uint8_t>(value >> 16);
        return;
    }
    auto value = static_cast<int32_t>(quantized);
    if (fmt.highAligned && bits < 32) {
        value <<= (32 - bits);
    }
    std::memcpy(dst, &value, sizeof(value));
}

bool packingOnly(const SampleFormat& src, const DeviceFormat& dst, float gain)
{
    if (std::fabs(gain - 1.0f) > 0.0001f || src.isFloat || dst.isFloat) {
        return false;
    }
    if (src.channels != dst.channels || src.bitsPerSample != dst.bitsPerSample) {
        return false;
    }
    if (src.bytesPerSample == dst.containerBytes && !dst.highAligned) {
        return true;
    }
    return false;
}

} // namespace

void convertSamples(const uint8_t* src, const SampleFormat& srcFmt, int frames, uint8_t* dst, const DeviceFormat& dstFmt,
                    float gain, uint32_t* ditherState)
{
    if (frames <= 0) {
        return;
    }
    if (packingOnly(srcFmt, dstFmt, gain) && srcFmt.channels == dstFmt.channels) {
        std::memcpy(dst, src, static_cast<size_t>(frames) * static_cast<size_t>(srcFmt.bytesPerFrame()));
        return;
    }
    const bool dither = std::fabs(gain - 1.0f) > 0.0001f || srcFmt.isFloat || srcFmt.sampleRate != dstFmt.sampleRate;
    const int srcStep = srcFmt.bytesPerSample;
    const int dstStep = dstFmt.containerBytes;
    for (int frame = 0; frame < frames; ++frame) {
        for (int channel = 0; channel < dstFmt.channels; ++channel) {
            const int srcChannel = std::min(channel, srcFmt.channels - 1);
            const float sample = readSample(src + srcChannel * srcStep, srcFmt) * gain;
            writeSample(dst + channel * dstStep, sample, dstFmt, ditherState, dither);
        }
        src += srcFmt.bytesPerFrame();
        dst += dstFmt.bytesPerFrame();
    }
}

} // namespace echo
