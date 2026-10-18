#pragma once

// Copied from harbour-suuntosync / harbour-edgesync (the same author, MIT), src/fit/fitcrc.*. FIT is FIT:
// nothing in here is specific to a device.
#include <cstddef>
#include <cstdint>

namespace Fit {

// CRC-16/ARC in its nibble-table form, as FIT uses it. `seed` is the running value.
uint16_t crc16(const uint8_t *data, size_t length, uint16_t seed = 0);

} // namespace Fit
