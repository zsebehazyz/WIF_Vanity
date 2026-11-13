#include "vanity/sha256.hpp"
struct Bounds {
    uint8_t first[32], low[40], high[40];
    bool compressed;
};
extern "C" __global__ void scan_wif(Bounds bounds, uint32_t n, uint32_t *matches, uint32_t *found) {
    uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= n)
        return;
    uint8_t key[32];
    uint64_t carry = index;
#pragma unroll
    for (int i = 31; i >= 0; i--) {
        uint64_t sum = bounds.first[i] + (carry & 255);
        key[i] = uint8_t(sum);
        carry = (carry >> 8) + (sum >> 8);
    }
    uint8_t raw[40] = {};
    unsigned len = bounds.compressed ? 38 : 37;
    vanity::wif_bytes(key, bounds.compressed, raw + 40 - len);
    int low = 0, high = 0;
#pragma unroll
    for (unsigned i = 0; i < 40; i++) {
        if (!low && raw[i] != bounds.low[i])
            low = raw[i] > bounds.low[i] ? 1 : -1;
        if (!high && raw[i] != bounds.high[i])
            high = raw[i] > bounds.high[i] ? 1 : -1;
    }
    // One result-counter atomic per warp, followed by coalesced result writes.
    unsigned active = __activemask();
    unsigned mask = __ballot_sync(active, low >= 0 && high <= 0);
    if (mask) {
        unsigned lane = threadIdx.x & 31;
        int leader = __ffs(mask) - 1;
        unsigned base = 0;
        if (lane == unsigned(leader))
            base = atomicAdd(found, unsigned(__popc(mask)));
        base = __shfl_sync(active, base, leader);
        if (low >= 0 && high <= 0)
            matches[base + __popc(mask & ((1u << lane) - 1))] = index;
    }
}
