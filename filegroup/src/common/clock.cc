#include "common/clock.h"

#include <chrono>

namespace filegroup {

uint64_t now_us() {
    using namespace std::chrono;
    auto now = system_clock::now();
    auto us = duration_cast<microseconds>(now.time_since_epoch());
    return static_cast<uint64_t>(us.count());
}

uint64_t now_us_monotonic() {
    using namespace std::chrono;
    auto now = steady_clock::now();
    auto us = duration_cast<microseconds>(now.time_since_epoch());
    return static_cast<uint64_t>(us.count());
}

}  // namespace filegroup
