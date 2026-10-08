#include "audio/convert.h"

#include <core/engine/audioconverter.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace echo {
namespace {

Fooyin::SampleFormat inferPcm(const SampleFormat& format)
{
    if (format.pcm != Fooyin::SampleFormat::Unknown) {
        return format.pcm;
    }
    if (format.isFloat) {
        return format.bytesPerSample >= 8 ? Fooyin::SampleFormat::F64 : Fooyin::SampleFormat::F32;
    }
    if (format.bytesPerSample <= 1) {
        return Fooyin::SampleFormat::U8;
    }
    if (format.bytesPerSample == 2) {
        return Fooyin::SampleFormat::S16;
    }
    if (format.bitsPerSample > 0 && format.bitsPerSample <= 24) {
        return Fooyin::SampleFormat::S24In32;
    }
    return Fooyin::SampleFormat::S32;
}

Fooyin::AudioFormat fooyinSource(const SampleFormat& format)
{
    const int channels = std::max(format.channels, 1);
    const int rate = format.sampleRate > 0 ? format.sampleRate : 48000;
    return {inferPcm(format), rate, channels};
}

Fooyin::SampleFormat devicePcm(const DeviceFormat& format)
{
    if (format.isFloat) {
        return format.containerBytes >= 8 ? Fooyin::SampleFormat::F64 : Fooyin::SampleFormat::F32;
    }
    if (format.containerBytes <= 2 || format.bitsPerSample <= 16) {
        return Fooyin::SampleFormat::S16;
    }
    return Fooyin::SampleFormat::S32;
}

bool needsIntegerPack(const DeviceFormat& format)
{
    if (format.isFloat || format.containerBytes <= 2 || format.bitsPerSample <= 16) {
        return false;
    }
    if (format.containerBytes == 4 && (format.bitsPerSample >= 32 || format.highAligned)) {
        return false;
    }
    return true;
}

void scaleFloat(uint8_t* data, int count, float gain)
{
    auto* samples = reinterpret_cast<float*>(data);
    for (int index = 0; index < count; ++index) {
        samples[index] = std::clamp(samples[index] * gain, -1.0f, 1.0f);
    }
}

void fillMissingChannels(uint8_t* destination, int frames, int sourceChannels, int destChannels, int bytesPerSample)
{
    if (sourceChannels <= 0 || destChannels <= sourceChannels || bytesPerSample <= 0) {
        return;
    }
    const int frameBytes = destChannels * bytesPerSample;
    for (int frame = 0; frame < frames; ++frame) {
        uint8_t* frameData = destination + static_cast<size_t>(frame) * static_cast<size_t>(frameBytes);
        const uint8_t* last = frameData + static_cast<size_t>(sourceChannels - 1) * static_cast<size_t>(bytesPerSample);
        for (int channel = sourceChannels; channel < destChannels; ++channel) {
            std::memcpy(frameData + static_cast<size_t>(channel) * static_cast<size_t>(bytesPerSample), last,
                        static_cast<size_t>(bytesPerSample));
        }
    }
}

void packInteger(const int32_t* source, uint8_t* destination, int count, const DeviceFormat& format)
{
    const int shift = format.bitsPerSample > 0 && format.bitsPerSample < 32 && !format.highAligned ? 32 - format.bitsPerSample : 8;
    for (int index = 0; index < count; ++index) {
        const auto value = static_cast<int32_t>(source[index] >> shift);
        if (format.containerBytes == 3) {
            destination[0] = static_cast<uint8_t>(value);
            destination[1] = static_cast<uint8_t>(value >> 8);
            destination[2] = static_cast<uint8_t>(value >> 16);
            destination += 3;
            continue;
        }
        std::memcpy(destination, &value, sizeof(value));
        destination += 4;
    }
}

} // namespace

void convertSamples(const uint8_t* src, const SampleFormat& srcFmt, int frames, uint8_t* dst, const DeviceFormat& dstFmt,
                    float gain, uint32_t* ditherState)
{
    if (frames <= 0 || srcFmt.channels <= 0 || dstFmt.channels <= 0) {
        return;
    }
    Fooyin::AudioFormat input = fooyinSource(srcFmt);
    input.clearChannelLayout();
    const int outChannels = std::max(dstFmt.channels, 1);
    Fooyin::AudioFormat output {devicePcm(dstFmt), input.sampleRate(), outChannels};
    output.clearChannelLayout();
    const bool applyGain = std::fabs(gain - 1.0f) > 0.0001f;
    const bool dither = ditherState != nullptr && (applyGain || srcFmt.isFloat || srcFmt.sampleRate != dstFmt.sampleRate);
    const auto* inputBytes = reinterpret_cast<const std::byte*>(src);
    Fooyin::AudioFormat from = input;
    std::vector<uint8_t> gained;
    if (applyGain) {
        from = {Fooyin::SampleFormat::F32, input.sampleRate(), input.channelCount()};
        from.clearChannelLayout();
        gained.resize(static_cast<size_t>(frames) * static_cast<size_t>(from.bytesPerFrame()));
        if (!Fooyin::Audio::convert(input, inputBytes, from, reinterpret_cast<std::byte*>(gained.data()), frames, false)) {
            std::memset(dst, 0, static_cast<size_t>(frames) * static_cast<size_t>(std::max(dstFmt.bytesPerFrame(), 1)));
            return;
        }
        scaleFloat(gained.data(), frames * from.channelCount(), gain);
        inputBytes = reinterpret_cast<const std::byte*>(gained.data());
    }

    if (needsIntegerPack(dstFmt)) {
        Fooyin::AudioFormat s32 {Fooyin::SampleFormat::S32, from.sampleRate(), outChannels};
        s32.clearChannelLayout();
        std::vector<int32_t> wide(static_cast<size_t>(frames) * static_cast<size_t>(outChannels));
        if (!Fooyin::Audio::convert(from, inputBytes, s32, reinterpret_cast<std::byte*>(wide.data()), frames, dither)) {
            std::memset(dst, 0, static_cast<size_t>(frames) * static_cast<size_t>(std::max(dstFmt.bytesPerFrame(), 1)));
            return;
        }
        packInteger(wide.data(), dst, frames * outChannels, dstFmt);
        fillMissingChannels(dst, frames, input.channelCount(), outChannels, std::max(dstFmt.containerBytes, 1));
        return;
    }

    std::memset(dst, 0, static_cast<size_t>(frames) * static_cast<size_t>(std::max(output.bytesPerFrame(), 1)));
    Fooyin::Audio::convert(from, inputBytes, output, reinterpret_cast<std::byte*>(dst), frames, dither);
    fillMissingChannels(dst, frames, input.channelCount(), outChannels, std::max(output.bytesPerSample(), 1));
}

} // namespace echo
