#pragma once

#include <cstdint>

namespace echo {

// PCM sample types the audio pipeline understands.
enum class PcmType : uint8_t {
    Unknown = 0,
    U8,
    S16,
    S24In32,
    S32,
    F32,
    F64,
};

// Maps a packed FFmpeg sample format (AVSampleFormat) to a PCM type. Planar
// formats must be normalized with av_get_packed_sample_fmt first. When the
// stream carries 24-bit samples, FFmpeg reports S32 with bits_per_raw_sample
// set to 24 and the valid bits left-aligned in each 32-bit word.
[[nodiscard]] PcmType pcmTypeFromFFmpeg(int avSampleFormat, int bitsPerRawSample);
[[nodiscard]] int pcmBitDepth(PcmType type);
[[nodiscard]] int pcmBytesPerSample(PcmType type);

} // namespace echo