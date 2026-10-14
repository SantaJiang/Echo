#include "audio/loudness.h"
#include "audio/convert.h"
#include "audio/decoder.h"

#include <ebur128.h>
#include <algorithm>
#include <cmath>
#include <vector>
extern "C" {
#include <libavutil/channel_layout.h>
}

namespace echo {

float LoudnessResult::gainDb() const
{
    if (!valid || !std::isfinite(lufs) || lufs < -70 || peak <= 0 || !std::isfinite(peak)) {
        return 0;
    }
    // Consistent track loudness, with 1 dB of true-peak headroom.
    return static_cast<float>(std::min(std::clamp(-18.0 - lufs, -60.0, 24.0), -1.0 - 20.0 * std::log10(peak)));
}

LoudnessResult analyzeLoudness(const PlaybackItem& item, const std::function<bool()>& cancelled)
{
    Decoder decoder;
    if (!decoder.open(item.path)) {
        return {};
    }
    const auto format = decoder.format();
    // Respect the decoder's channel layout, particularly LFE exclusion and surround weighting.
    if (format.channels < 1 || format.channels > 8) {
        return {};
    }
    decoder.setRange(item.startMs * format.sampleRate / 1000,
                     item.endMs < 0 ? -1 : item.endMs * format.sampleRate / 1000);
    auto* state = ebur128_init(format.channels, format.sampleRate,
                             EBUR128_MODE_I | EBUR128_MODE_TRUE_PEAK | EBUR128_MODE_HISTOGRAM);
    if (!state) {
        return {};
    }
    AVChannelLayout layout {};
    if (decoder.channelMask()) av_channel_layout_from_mask(&layout, decoder.channelMask());
    else if (format.channels <= 2) av_channel_layout_default(&layout, format.channels);
    else { ebur128_destroy(&state); return {}; }
    bool mapped = true;
    for (int index = 0; index < format.channels; ++index) {
        int role = EBUR128_UNUSED;
        switch (av_channel_layout_channel_from_index(&layout, index)) {
        case AV_CHAN_FRONT_LEFT: role = EBUR128_LEFT; break;
        case AV_CHAN_FRONT_RIGHT: role = EBUR128_RIGHT; break;
        case AV_CHAN_FRONT_CENTER: role = EBUR128_CENTER; break;
        case AV_CHAN_LOW_FREQUENCY: role = EBUR128_UNUSED; break;
        case AV_CHAN_BACK_LEFT: case AV_CHAN_SIDE_LEFT: role = EBUR128_LEFT_SURROUND; break;
        case AV_CHAN_BACK_RIGHT: case AV_CHAN_SIDE_RIGHT: role = EBUR128_RIGHT_SURROUND; break;
        case AV_CHAN_BACK_CENTER: role = EBUR128_Mp180; break;
        default: mapped = false; break;
        }
        ebur128_set_channel(state, index, role);
    }
    av_channel_layout_uninit(&layout);
    if (!mapped) { ebur128_destroy(&state); return {}; }
    DeviceFormat wide;
    wide.sampleRate = format.sampleRate;
    wide.channels = format.channels;
    wide.bitsPerSample = 64;
    wide.containerBytes = 8;
    wide.isFloat = true;
    constexpr int chunk = 8192;
    std::vector<uint8_t> raw(chunk * format.bytesPerFrame());
    std::vector<double> samples(chunk * format.channels);
    bool ok = true;
    while (true) {
        if (cancelled && cancelled()) { ok = false; break; }
        const int frames = decoder.read(raw.data(), chunk);
        if (frames < 0) { ok = false; break; }
        if (frames == 0) break;
        convertSamples(raw.data(), format, frames, reinterpret_cast<uint8_t*>(samples.data()), wide, 1.f, nullptr);
        if (ebur128_add_frames_double(state, samples.data(), frames) != EBUR128_SUCCESS) { ok = false; break; }
    }
    LoudnessResult result;
    if (ok && ebur128_loudness_global(state, &result.lufs) == EBUR128_SUCCESS) {
        for (int channel = 0; channel < format.channels; ++channel) {
            double peak = 0;
            ebur128_true_peak(state, channel, &peak);
            result.peak = std::max(result.peak, peak);
        }
        // Silence is a successful analysis; keep unity gain.
        if (!std::isfinite(result.lufs)) result.lufs = -100;
        result.valid = true;
    }
    ebur128_destroy(&state);
    return result;
}

} // namespace echo
