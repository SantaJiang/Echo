#pragma once

#include "audio/format.h"

#include <cstdint>

namespace echo {

// Converts interleaved PCM. Packing-only conversions stay bit-exact.
// Gain other than 1, or a float intermediate, uses TPDF dither when writing integers.
void convertSamples(const uint8_t* src, const SampleFormat& srcFmt, int frames, uint8_t* dst, const DeviceFormat& dstFmt,
                    float gain, uint32_t* ditherState);

[[nodiscard]] inline float volumeCurve(float slider)
{
    if (slider <= 0.f) {
        return 0.f;
    }
    if (slider >= 1.f) {
        return 1.f;
    }
    return slider * slider;
}

} // namespace echo
