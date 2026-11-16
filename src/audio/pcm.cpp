#include "audio/pcm.h"

extern "C" {
#include <libavutil/samplefmt.h>
}

namespace echo {
namespace {

struct PcmTraits {
    PcmType type;
    int8_t bits;
    int8_t bytes;
};

// Indexed by the packed AVSampleFormat values, which FFmpeg defines as a
// contiguous run from AV_SAMPLE_FMT_U8 (0) to AV_SAMPLE_FMT_DBL (5).
constexpr PcmTraits traitsByAvFormat[] = {
    {PcmType::U8, 8, 1},  // AV_SAMPLE_FMT_U8
    {PcmType::S16, 16, 2}, // AV_SAMPLE_FMT_S16
    {PcmType::S32, 32, 4}, // AV_SAMPLE_FMT_S32
    {PcmType::F32, 32, 4}, // AV_SAMPLE_FMT_FLT
    {PcmType::F64, 64, 8}, // AV_SAMPLE_FMT_DBL
};

constexpr PcmTraits traitsByType[] = {
    {PcmType::Unknown, 0, 0}, {PcmType::U8, 8, 1},      {PcmType::S16, 16, 2},
    {PcmType::S24In32, 24, 4}, {PcmType::S32, 32, 4},   {PcmType::F32, 32, 4},
    {PcmType::F64, 64, 8},
};

} // namespace

PcmType pcmTypeFromFFmpeg(int avSampleFormat, int bitsPerRawSample)
{
    if (avSampleFormat == AV_SAMPLE_FMT_S32 && bitsPerRawSample == 24) {
        return PcmType::S24In32;
    }
    if (avSampleFormat < AV_SAMPLE_FMT_U8 || avSampleFormat > AV_SAMPLE_FMT_DBL) {
        return PcmType::Unknown;
    }
    return traitsByAvFormat[avSampleFormat].type;
}

int pcmBitDepth(PcmType type)
{
    const auto index = static_cast<int>(type);
    return index >= 0 && index <= static_cast<int>(PcmType::F64) ? traitsByType[index].bits : 0;
}

int pcmBytesPerSample(PcmType type)
{
    const auto index = static_cast<int>(type);
    return index >= 0 && index <= static_cast<int>(PcmType::F64) ? traitsByType[index].bytes : 0;
}

} // namespace echo