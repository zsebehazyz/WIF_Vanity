#pragma once
#include <cstdint>
namespace solvan {
struct Interval {
    uint8_t low[32], high[32];
};
struct Prepared {
    uint64_t inner[8], outer[8];
    Interval ranges[4];
    uint32_t range_count = 0;
};
struct KernelParams {
    Prepared prepared;
    uint64_t first;
    uint32_t count;
};
} // namespace solvan
