#pragma once

#include "audio/format.h"
#include <functional>

namespace echo {

struct LoudnessResult {
    bool valid = false;
    double lufs = 0;
    double peak = 0;
    [[nodiscard]] float gainDb() const;
};

// Analyze the complete audible range, without modifying the source file.
LoudnessResult analyzeLoudness(const PlaybackItem& item, const std::function<bool()>& cancelled = {});

} // namespace echo
