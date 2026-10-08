#include <core/engine/ffmpegpcm.h>

extern "C" {
#include <libavutil/samplefmt.h>
}

namespace Fooyin::Utils {

SampleFormat sampleFormatFromFFmpeg(int avSampleFormat, int bitsPerRawSample)
{
    switch (avSampleFormat) {
    case AV_SAMPLE_FMT_NONE:
    case AV_SAMPLE_FMT_U8:
    case AV_SAMPLE_FMT_U8P:
        return SampleFormat::U8;
    case AV_SAMPLE_FMT_S16:
    case AV_SAMPLE_FMT_S16P:
        return SampleFormat::S16;
    case AV_SAMPLE_FMT_S32:
    case AV_SAMPLE_FMT_S32P:
        return bitsPerRawSample == 24 ? SampleFormat::S24In32 : SampleFormat::S32;
    case AV_SAMPLE_FMT_FLT:
    case AV_SAMPLE_FMT_FLTP:
        return SampleFormat::F32;
    case AV_SAMPLE_FMT_DBL:
    case AV_SAMPLE_FMT_DBLP:
        return SampleFormat::F64;
    default:
        return SampleFormat::Unknown;
    }
}

} // namespace Fooyin::Utils
