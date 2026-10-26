/*
 * Fooyin
 * Copyright © 2024, Luke Taylor <luket@pm.me>
 *
 * Fooyin is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Echo keeps only the PCM sample-type enum. The format, buffer and converter
 * modules this enum used to live with are not part of the player.
 */

#pragma once

#include <cstdint>

namespace Fooyin {

enum class SampleFormat : uint8_t
{
    Unknown = 0,
    U8,
    S16,
    S24In32,
    S32,
    F32,
    F64,
};

} // namespace Fooyin
