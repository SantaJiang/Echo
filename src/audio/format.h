#pragma once

#include "audio/pcm.h"

#include <QString>
#include <cstdint>

namespace echo {

struct SampleFormat {
    int sampleRate = 0;
    int channels = 0;
    int bitsPerSample = 0;
    int bytesPerSample = 0;
    bool isFloat = false;
    // FFmpeg places 24-bit (and other partial-width) PCM in the high bits of S32.
    bool highAligned = false;
    // PCM sample type. Unknown means infer from the fields above.
    PcmType pcm = PcmType::Unknown;

    quint64 channelMask = 0;

    [[nodiscard]] int bytesPerFrame() const { return bytesPerSample * channels; }
    [[nodiscard]] bool valid() const { return sampleRate > 0 && channels > 0 && bytesPerSample > 0; }
    [[nodiscard]] bool operator==(const SampleFormat& other) const
    {
        return sampleRate == other.sampleRate && channels == other.channels && bitsPerSample == other.bitsPerSample
            && bytesPerSample == other.bytesPerSample && isFloat == other.isFloat && highAligned == other.highAligned
            && pcm == other.pcm && channelMask == other.channelMask;
    }
};

struct DeviceFormat {
    int sampleRate = 0;
    int channels = 0;
    int bitsPerSample = 0;
    int containerBytes = 0;
    bool isFloat = false;
    bool highAligned = false;
    bool exclusive = false;
    int latencyFrames = 0;
    quint64 channelMask = 0;

    [[nodiscard]] int bytesPerFrame() const { return containerBytes * channels; }
    [[nodiscard]] bool valid() const { return sampleRate > 0 && channels > 0 && containerBytes > 0; }
};

struct ReplayGain {
    bool hasTrack = false;
    bool hasAlbum = false;
    double trackDb = 0;
    double albumDb = 0;
    double trackPeak = 1;
    double albumPeak = 1;
};

struct PlaybackItem {
    qint64 token = 0;
    qint64 trackId = 0;
    QString path;
    qint64 startMs = 0;
    qint64 endMs = -1;
    float gainDb = 0;
    [[nodiscard]] bool valid() const { return !path.isEmpty(); }
};

} // namespace echo
