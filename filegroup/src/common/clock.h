#pragma once

#include <cstdint>

namespace filegroup {

/**
 * Get current Unix time in microseconds (wall clock).
 * @return Unix timestamp in microseconds
 */
uint64_t now_us();

/**
 * Get current monotonic time in microseconds (for timeouts/intervals).
 * @return Monotonic timestamp in microseconds
 */
uint64_t now_us_monotonic();

}  // namespace filegroup
