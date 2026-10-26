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

int pcmBitDepth(SampleFormat format)
{
    switch (format) {
    case SampleFormat::U8:
        return 8;
    case SampleFormat::S16:
        return 16;
    case SampleFormat::S24In32:
        return 24;
    case SampleFormat::S32:
    case SampleFormat::F32:
        return 32;
    case SampleFormat::F64:
        return 64;
    case SampleFormat::Unknown:
        return 0;
    }
    return 0;
}

int pcmBytesPerSample(SampleFormat format)
{
    switch (format) {
    case SampleFormat::U8:
        return 1;
    case SampleFormat::S16:
        return 2;
    case SampleFormat::S24In32:
    case SampleFormat::S32:
    case SampleFormat::F32:
        return 4;
    case SampleFormat::F64:
        return 8;
    case SampleFormat::Unknown:
        return 0;
    }
    return 0;
}

} // namespace Fooyin::Utils
