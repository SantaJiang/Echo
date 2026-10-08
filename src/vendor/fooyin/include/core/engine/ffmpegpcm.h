#pragma once

#include <core/engine/audioformat.h>

namespace Fooyin::Utils {

// Same mapping as fooyin's FFmpeg decoder: 24-bit PCM arrives as S32 with the
// significant bits in the high end of the word, and is tagged S24In32.
[[nodiscard]] SampleFormat sampleFormatFromFFmpeg(int avSampleFormat, int bitsPerRawSample);

} // namespace Fooyin::Utils
